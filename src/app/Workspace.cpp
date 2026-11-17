// SPDX-License-Identifier: AGPL-3.0-only
#include "app/Workspace.h"
#include "app/document/RecoveryJournal.h"
#include "app/editor/ExportDialog.h"
#include "app/ui/Dialogs.h"
#include "dsp/convert/Resample.h"
#include "formats/import/FileSampleSource.h"
#include "services/logging/Log.h"
#include "services/settings/SettingKeys.h"

namespace mxj
{
    //==============================================================================
    /** One undo step: the whole state (block lists and markers, never audio)
        before and after an edit. */
    class Workspace::EditAction final : public juce::UndoableAction
    {
    public:
        EditAction (Workspace& w, AudioState before, AudioState after, FrameRange selBefore, FrameRange selAfter,
                    juce::int64 revBefore, juce::int64 revAfter)
            : workspace (w), stateBefore (std::move (before)), stateAfter (std::move (after)),
              selectionBefore (selBefore), selectionAfter (selAfter),
              revisionBefore (revBefore), revisionAfter (revAfter) {}

        bool perform() override
        {
            workspace.applyState (stateAfter, selectionAfter, revisionAfter);
            return true;
        }

        bool undo() override
        {
            workspace.applyState (stateBefore, selectionBefore, revisionBefore);
            return true;
        }

        int getSizeInUnits() override { return 1; }

    private:
        Workspace& workspace;
        AudioState stateBefore, stateAfter;
        FrameRange selectionBefore, selectionAfter;
        juce::int64 revisionBefore, revisionAfter;
    };

    //==============================================================================
    Workspace::Workspace (AppContext& ctx) : context (ctx)
    {
        startTimerHz (30);
    }

    Workspace::~Workspace()
    {
        job = nullptr;

        if (recording)
            context.audio.stopRecording();

        context.audio.getPlayback().setSequence ({});
        context.history.clear();
        document = nullptr;
    }

    void Workspace::setStatus (const juce::String& message)
    {
        if (onStatusMessage != nullptr)
            onStatusMessage (message);
    }

    void Workspace::showError (const juce::String& title, const juce::String& message)
    {
        Log::warning (title + ": " + message);
        dialogs::showWarning (title, message);
    }

    //==============================================================================
    void Workspace::askToSaveChanges (std::function<void()> proceed)
    {
        if (isBusy())
        {
            setStatus (TRANS ("Please wait until the current task has finished."));
            return;
        }

        if (recording)
            stopRecording();

        if (isBusy()) // converting the take: try again afterwards
        {
            setStatus (TRANS ("Please wait until the current task has finished."));
            return;
        }

        if (document == nullptr || ! document->isModified())
        {
            proceed();
            return;
        }

        dialogs::ask (TRANS ("Save changes?"),
                      TRANS ("Do you want to save the changes to this file?") + "\n\n" + document->getDisplayName(),
                      { TRANS ("Save"), TRANS ("Don't Save"), TRANS ("Cancel") },
                      [weak = juce::WeakReference<Workspace> (this), proceed] (int result)
        {
            if (weak == nullptr)
                return;

            if (result == 2)
                proceed();
            else if (result == 1)
                weak->save (proceed);
        });
    }

    void Workspace::showOpenDialog()
    {
        askToSaveChanges ([this]
        {
            auto folder = juce::File (context.settings.getString (keys::lastOpenFolder));
            if (! folder.isDirectory())
                folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory);

            chooser = std::make_unique<juce::FileChooser> (TRANS ("Open audio file"), folder,
                                                           audioFormats::getWildcard(), true);

            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();
                if (file != juce::File())
                {
                    context.settings.set (keys::lastOpenFolder, file.getParentDirectory().getFullPathName());
                    openFileNow (file);
                }
            });
        });
    }

    bool Workspace::openFile (const juce::File& file)
    {
        if (document != nullptr && document->isModified())
        {
            askToSaveChanges ([this, file] { openFileNow (file); });
            return true;
        }

        if (isBusy() || recording)
            return false;

        openFileNow (file);
        return document != nullptr;
    }

    void Workspace::setDocument (std::unique_ptr<AudioDocument> newDocument)
    {
        stop();
        context.audio.getPlayback().setSequence ({});
        context.history.clear();
        document = std::move (newDocument);
        cursor = 0;
        selection = {};
        selectedMarker = -1;
        view = {};
        journalDirty = true;

        if (document != nullptr && document->isReady())
            context.audio.getPlayback().setSequence (document->getAudio());
    }

    void Workspace::openFileNow (const juce::File& file)
    {
        setDocument (nullptr);

        juce::String error;
        auto doc = AudioDocument::open (file, context.blocks(), context.paths.getCacheFolder().getChildFile ("peaks"), error);

        if (doc == nullptr)
        {
            setStatus (TRANS ("Could not open the file."));
            showError (TRANS ("Could not open the file"), file.getFileName() + "\n\n" + error);
            sendChangeMessage();
            return;
        }

        const auto& info = doc->getInfo();
        Log::info ("Opening " + file.getFullPathName() + " (" + info.formatName + ", "
                   + juce::String (info.numChannels) + " ch, " + juce::String (info.sampleRate) + " Hz, "
                   + juce::String (info.numFrames) + " frames)");

        doc->onLoadFinished = [this] { loadFinished(); };
        setDocument (std::move (doc));
        zoomToFit();
        setStatus (TRANS ("Opening") + " " + file.getFileName() + "...");
        sendChangeMessage();
    }

    void Workspace::loadFinished()
    {
        if (document == nullptr)
            return;

        if (document->isReady())
        {
            context.audio.getPlayback().setSequence (document->getAudio());
            Log::info ("Opened in " + juce::String (document->getLoadSeconds(), 2) + " s"
                       + (document->usedCachedPeaks() ? " (peaks from cache)" : "")
                       + (document->getMarkers().empty() ? juce::String() : ", " + juce::String ((int) document->getMarkers().size()) + " markers"));
            setStatus (TRANS ("Opened") + " " + document->getDisplayName()
                       + " (" + juce::String (document->getLoadSeconds(), 1) + " s)");
        }
        else
        {
            setStatus (TRANS ("Could not open the file."));
            showError (TRANS ("Could not open the file"), document->getDisplayName() + "\n\n" + document->getError());
        }

        sendChangeMessage();
    }

    void Workspace::closeFile()
    {
        askToSaveChanges ([this] { closeFileNow(); });
    }

    void Workspace::closeFileNow()
    {
        if (document == nullptr)
            return;

        setDocument (nullptr);
        setStatus (TRANS ("Ready"));
        sendChangeMessage();
    }

    //==============================================================================
    void Workspace::runJob (const juce::String& title, BackgroundJob::Work work, BackgroundJob::Done done)
    {
        stop();
        job = std::make_unique<BackgroundJob> (title, std::move (work),
                                               [this, done] (bool ok)
        {
            auto finished = std::move (job); // deleted after `done`
            jobError = finished->error;
            done (ok);
            sendChangeMessage();
        });
        sendChangeMessage();
    }

    juce::String Workspace::getBusyText() const
    {
        if (job == nullptr)
            return {};

        return job->getName() + "... " + juce::String (juce::roundToInt (job->progress.load() * 100.0)) + "%";
    }

    void Workspace::save (std::function<void()> afterSaved)
    {
        if (! canEdit())
            return;

        const auto target = document->getFile();
        const auto format = audioExport::formatForFile (target);

        // New recordings and files we cannot write (MP3) are saved under a new name.
        if (! document->hasFile() || ! format.has_value())
        {
            showSaveAsDialog (afterSaved);
            return;
        }

        const auto& info = document->getInfo();
        ExportSettings settings;
        settings.file = target;
        settings.format = *format;
        settings.bitsPerSample = info.bitsPerSample <= 16 ? 16 : info.bitsPerSample <= 24 ? 24 : 32;
        settings.floatingPoint = settings.bitsPerSample == 32 && (info.usesFloat || *format == ExportFormat::wav);
        if (settings.bitsPerSample == 32 && *format != ExportFormat::wav)
            settings.bitsPerSample = 24;
        settings.markers = document->getMarkers();

        saveAs (settings, document->getAudio(), true, afterSaved);
    }

    void Workspace::showSaveAsDialog (std::function<void()> afterSaved)
    {
        if (! canEdit())
            return;

        auto folder = juce::File (context.settings.getString (keys::lastSaveFolder));
        if (! folder.isDirectory())
            folder = document->hasFile() ? document->getFile().getParentDirectory()
                                         : juce::File::getSpecialLocation (juce::File::userMusicDirectory);

        const auto name = document->hasFile() ? document->getFile().getFileNameWithoutExtension() : document->getDisplayName();

        ExportDialog::show (context.settings, TRANS ("Save As"), folder.getChildFile (name), document->getSampleRate(),
                            juce::TopLevelWindow::getActiveTopLevelWindow(),
                            [weak = juce::WeakReference<Workspace> (this), afterSaved] (const ExportSettings& chosen)
        {
            if (weak == nullptr || ! weak->canEdit())
                return;

            auto settings = chosen;
            settings.markers = weak->document->getMarkers();
            weak->saveAs (settings, weak->document->getAudio(), true, afterSaved);
        });
    }

    void Workspace::showExportDialog (bool selectionOnly)
    {
        if (! canEdit() || (selectionOnly && ! hasSelection()))
            return;

        auto folder = juce::File (context.settings.getString (keys::lastSaveFolder));
        if (! folder.isDirectory())
            folder = document->hasFile() ? document->getFile().getParentDirectory()
                                         : juce::File::getSpecialLocation (juce::File::userMusicDirectory);

        auto name = document->hasFile() ? document->getFile().getFileNameWithoutExtension() : document->getDisplayName();
        if (selectionOnly)
            name += " - " + TRANS ("selection");

        const auto range = selectionOnly ? selection : FrameRange (0, document->getLength());

        ExportDialog::show (context.settings, selectionOnly ? TRANS ("Export Selection") : TRANS ("Export"),
                            folder.getChildFile (name), document->getSampleRate(),
                            juce::TopLevelWindow::getActiveTopLevelWindow(),
                            [weak = juce::WeakReference<Workspace> (this), range] (const ExportSettings& chosen)
        {
            if (weak == nullptr || ! weak->canEdit())
                return;

            // Only the markers inside the range, relative to its start.
            auto state = edits::trim (weak->document->getAudioState(), range);
            const auto& audio = state.changed ? state.state.audio : weak->document->getAudio();
            const auto& markers = state.changed ? state.state.markers : weak->document->getMarkers();

            auto settings = chosen;
            settings.markers = markers;
            weak->saveAs (settings, audio, false, nullptr);
        });
    }

    void Workspace::exportFile (const ExportSettings& settings)
    {
        if (canEdit())
            saveAs (settings, document->getAudio(), false, nullptr);
    }

    void Workspace::saveAs (const ExportSettings& settings, const AudioSequence& audio, bool becomesDocumentFile,
                            std::function<void()> afterSaved)
    {
        auto& store = context.blocks();
        const auto revision = document->getRevision();
        const auto start = juce::Time::getMillisecondCounter();
        Log::info ("Saving " + settings.file.getFullPathName() + " (" + audioExport::getDisplayName (settings.format) + ")");

        runJob (TRANS ("Saving"), [settings, audio, &store] (BackgroundJob& j)
        {
            // The file may be the one we read the audio from: read a copy instead (docs/adr/0006).
            if (! FileSampleSource::relocateAllReading (settings.file, [&store, &settings]
                                                        { return store.createFileName (settings.file.getFileExtension()); }))
            {
                j.error = TRANS ("Could not make a temporary copy of the original file. Is the disk full?");
                return false;
            }

            return audioExport::write (audio, settings, [&j] { return j.shouldContinue(); }, &j.progress, j.error)
                   == audioExport::Result::ok;
        },
        [this, settings, becomesDocumentFile, revision, start, afterSaved] (bool ok)
        {
            if (! ok)
            {
                showError (TRANS ("Could not save the file"), settings.file.getFullPathName() + "\n\n" + jobError);
                return;
            }

            Log::info ("Saved in " + juce::String ((juce::Time::getMillisecondCounter() - start) / 1000.0, 2) + " s");
            setStatus (TRANS ("Saved") + " " + settings.file.getFileName());

            if (becomesDocumentFile && document != nullptr)
            {
                AudioFileInfo info;
                juce::String ignored;
                if (! audioFormats::readInfo (settings.file, info, ignored))
                    info.file = settings.file;

                document->markSaved (info, revision);
                journalDirty = true;
            }

            if (afterSaved != nullptr)
                afterSaved();
        });
    }

    //==============================================================================
    void Workspace::togglePlayback()
    {
        if (isPlaying())
            stop();
        else
            play();
    }

    void Workspace::play()
    {
        if (! isReady() || recording || isBusy())
            return;

        auto& playback = context.audio.getPlayback();
        const auto length = document->getAudio().getNumFrames();
        auto start = cursor;
        juce::int64 end = -1;

        // A selection plays on its own, as in Audacity.
        if (hasSelection())
        {
            start = selection.getStart();
            end = selection.getEnd();
        }
        else if (start >= length)
        {
            start = 0;
        }

        if (! playback.play (start, end))
        {
            setStatus (TRANS ("Cannot play: no audio output device is running. Check Edit > Audio Device Settings."));
            return;
        }

        wasPlaying = true;
        setStatus (TRANS ("Playing"));
        sendChangeMessage();
    }

    void Workspace::stop()
    {
        auto& playback = context.audio.getPlayback();
        const bool playing = playback.isPlaying() || wasPlaying;
        playback.stop();

        if (playing)
        {
            wasPlaying = false;

            if (playback.getUnderrunCount() > 0)
                Log::warning ("Playback had " + juce::String (playback.getUnderrunCount()) + " underruns");

            setStatus (TRANS ("Stopped"));
            sendChangeMessage();
        }
    }

    bool Workspace::isPlaying() const
    {
        return context.audio.getPlayback().isPlaying();
    }

    void Workspace::setCursor (juce::int64 frame)
    {
        if (document == nullptr)
            return;

        cursor = juce::jlimit ((juce::int64) 0, document->getLength(), frame);
        selection = { cursor, cursor };

        if (isPlaying())
            play();

        sendChangeMessage();
    }

    juce::int64 Workspace::getPlayheadPosition() const
    {
        return isPlaying() ? context.audio.getPlayback().getPosition() : cursor;
    }

    void Workspace::goToStart()
    {
        selectedMarker = -1;
        setCursor (0);
        auto v = view;
        v.startFrame = 0;
        setView (v);
    }

    void Workspace::goToEnd()
    {
        if (document == nullptr)
            return;

        stop();
        selectedMarker = -1;
        setCursor (document->getLength());
        auto v = view;
        v.startFrame = (double) document->getLength() - v.framesPerPixel * viewWidth * 0.9;
        setView (v);
    }

    //==============================================================================
    void Workspace::setSelection (FrameRange range)
    {
        if (document == nullptr)
            return;

        range = range.getIntersectionWith ({ 0, document->getLength() });
        const bool restart = isPlaying();
        stop();
        selection = range;
        cursor = range.getStart();

        if (restart && hasSelection())
            play();

        sendChangeMessage();
    }

    void Workspace::selectAll()
    {
        if (isReady())
            setSelection ({ 0, document->getLength() });
    }

    void Workspace::selectNone()
    {
        if (document != nullptr)
            setSelection ({ cursor, cursor });
    }

    FrameRange Workspace::getEditRange() const
    {
        return hasSelection() ? selection : FrameRange (cursor, cursor);
    }

    //==============================================================================
    void Workspace::applyEdit (const juce::String& name, const edits::Result& result)
    {
        if (! result.changed || document == nullptr)
            return;

        const auto before = document->getAudioState();
        const auto revisionBefore = document->getRevision();

        context.history.beginStep (name);
        context.history.perform (new EditAction (*this, before, result.state, selection, result.selection,
                                                 revisionBefore, nextRevision++));
        setStatus (name);
    }

    void Workspace::applyState (const AudioState& state, FrameRange newSelection, juce::int64 revision)
    {
        if (document == nullptr)
            return;

        // Marker changes leave the audio alone: playback goes on.
        const bool audioChanged = ! state.audio.isSameAs (document->getAudio());
        if (audioChanged)
            stop();

        document->setAudioState (state, revision);

        if (audioChanged)
            context.audio.getPlayback().setSequence (state.audio);

        selection = newSelection.getIntersectionWith ({ 0, state.audio.getNumFrames() });
        cursor = selection.getStart();
        if (selectedMarker >= (int) state.markers.size())
            selectedMarker = -1;

        setView (view); // the length may have changed
        journalDirty = true;
        sendChangeMessage();
    }

    void Workspace::undo()
    {
        if (canEdit() && context.history.canUndo())
        {
            const auto name = context.history.getUndoName();
            context.history.undo();
            setStatus (TRANS ("Undone") + ": " + name);
        }
    }

    void Workspace::redo()
    {
        if (canEdit() && context.history.canRedo())
        {
            const auto name = context.history.getRedoName();
            context.history.redo();
            setStatus (TRANS ("Redone") + ": " + name);
        }
    }

    void Workspace::cut()
    {
        if (! canEdit() || ! hasSelection())
            return;

        clipboard = document->getAudio().getRange (selection.getStart(), selection.getLength());
        applyEdit (TRANS ("Cut"), edits::erase (document->getAudioState(), selection));
    }

    void Workspace::copy()
    {
        if (! canEdit() || ! hasSelection())
            return;

        clipboard = document->getAudio().getRange (selection.getStart(), selection.getLength());
        setStatus (TRANS ("Copied") + " " + formatTime (clipboard.getNumFrames(), clipboard.getSampleRate()));
        sendChangeMessage();
    }

    void Workspace::paste()
    {
        if (! canEdit() || clipboard.isEmpty())
            return;

        if (! juce::approximatelyEqual (clipboard.getSampleRate(), document->getSampleRate()))
        {
            // Audio from a file with another sample rate: convert it first.
            const auto rate = document->getSampleRate();
            auto source = clipboard;
            auto converted = std::make_shared<AudioSequence>();
            auto& store = context.blocks();

            runJob (TRANS ("Converting"), [source, rate, converted, &store] (BackgroundJob& j)
            {
                *converted = dsp::resample (source, rate, store, [&j] { return j.shouldContinue(); }, &j.progress);
                return ! converted->isEmpty();
            },
            [this, converted] (bool ok)
            {
                if (ok && document != nullptr)
                {
                    clipboard = *converted;
                    applyEdit (TRANS ("Paste"), edits::paste (document->getAudioState(), getEditRange(), clipboard));
                }
            });
            return;
        }

        applyEdit (TRANS ("Paste"), edits::paste (document->getAudioState(), getEditRange(), clipboard));
    }

    void Workspace::mixPaste()
    {
        if (! canEdit() || clipboard.isEmpty())
            return;

        if (! juce::approximatelyEqual (clipboard.getSampleRate(), document->getSampleRate()))
        {
            setStatus (TRANS ("Paste the audio once to convert its sample rate, then mix paste it."));
            return;
        }

        applyEdit (TRANS ("Mix Paste"), edits::mixPaste (document->getAudioState(), getEditRange(), clipboard));
    }

    void Workspace::deleteSelection()
    {
        if (canEdit() && hasSelection())
            applyEdit (TRANS ("Delete"), edits::erase (document->getAudioState(), selection));
    }

    void Workspace::trimToSelection()
    {
        if (canEdit() && hasSelection())
            applyEdit (TRANS ("Trim"), edits::trim (document->getAudioState(), selection));
    }

    void Workspace::silenceSelection()
    {
        if (canEdit() && hasSelection())
            applyEdit (TRANS ("Silence"), edits::silence (document->getAudioState(), selection));
    }

    void Workspace::showInsertSilenceDialog()
    {
        if (! canEdit() || document->getNumChannels() <= 0)
            return;

        dialogs::askForText (TRANS ("Insert Silence"), TRANS ("Length of the silence in seconds:"), "1.0",
                             [weak = juce::WeakReference<Workspace> (this)] (const juce::String& text)
        {
            const auto seconds = text.replaceCharacter (',', '.').getDoubleValue();
            if (weak == nullptr || ! weak->canEdit() || seconds <= 0.0 || seconds > 36000.0)
                return;

            const auto frames = (juce::int64) std::llround (seconds * weak->document->getSampleRate());
            weak->applyEdit (TRANS ("Insert Silence"), edits::insertSilence (weak->document->getAudioState(), weak->getEditRange(), frames));
        });
    }

    void Workspace::fadeIn()
    {
        if (canEdit() && hasSelection())
            applyEdit (TRANS ("Fade In"), edits::fadeIn (document->getAudioState(), selection));
    }

    void Workspace::fadeOut()
    {
        if (canEdit() && hasSelection())
            applyEdit (TRANS ("Fade Out"), edits::fadeOut (document->getAudioState(), selection));
    }

    //==============================================================================
    void Workspace::addMarker()
    {
        if (! canEdit())
            return;

        // Uses the playhead, so markers can be set while listening (playback goes on).
        edits::Result r { document->getAudioState(), selection, true };
        const auto position = juce::jlimit ((juce::int64) 0, document->getLength(), getPlayheadPosition());
        const auto index = edits::addMarker (r.state.markers, { position, edits::nextMarkerName (r.state.markers, TRANS ("Marker")) });
        applyEdit (TRANS ("Add Marker"), r);
        selectedMarker = (int) index;
        sendChangeMessage();
    }

    int Workspace::getCurrentMarker() const
    {
        if (document == nullptr)
            return -1;

        const auto& markers = document->getMarkers();
        if (juce::isPositiveAndBelow (selectedMarker, (int) markers.size()))
            return selectedMarker;

        for (int i = 0; i < (int) markers.size(); ++i)
            if (markers[(size_t) i].position == cursor)
                return i;

        return -1;
    }

    void Workspace::selectMarker (int index)
    {
        if (document == nullptr || ! juce::isPositiveAndBelow (index, (int) document->getMarkers().size()))
            return;

        selectedMarker = index;
        setCursor (document->getMarkers()[(size_t) index].position);
    }

    void Workspace::showRenameMarkerDialog()
    {
        const auto index = getCurrentMarker();
        if (! canEdit() || index < 0)
            return;

        dialogs::askForText (TRANS ("Rename Marker"), TRANS ("New name:"), document->getMarkers()[(size_t) index].name,
                             [weak = juce::WeakReference<Workspace> (this), index] (const juce::String& text)
        {
            if (weak == nullptr || ! weak->canEdit() || text.trim().isEmpty()
                || ! juce::isPositiveAndBelow (index, (int) weak->document->getMarkers().size()))
                return;

            edits::Result r { weak->document->getAudioState(), weak->selection, true };
            r.state.markers[(size_t) index].name = text.trim();
            weak->applyEdit (TRANS ("Rename Marker"), r);
            weak->selectedMarker = index;
        });
    }

    void Workspace::deleteMarker()
    {
        const auto index = getCurrentMarker();
        if (! canEdit() || index < 0)
            return;

        edits::Result r { document->getAudioState(), selection, true };
        r.state.markers.erase (r.state.markers.begin() + index);
        selectedMarker = -1;
        applyEdit (TRANS ("Delete Marker"), r);
    }

    void Workspace::goToNextMarker()
    {
        if (document == nullptr)
            return;

        const auto& markers = document->getMarkers();
        const auto from = getPlayheadPosition();
        for (int i = 0; i < (int) markers.size(); ++i)
            if (markers[(size_t) i].position > from)
                return selectMarker (i);
    }

    void Workspace::goToPreviousMarker()
    {
        if (document == nullptr)
            return;

        const auto& markers = document->getMarkers();
        // While playing, a short grace period lets a double press go back one more.
        const auto from = getPlayheadPosition() - (isPlaying() ? (juce::int64) (0.5 * document->getSampleRate()) : 0);
        for (int i = (int) markers.size(); --i >= 0;)
            if (markers[(size_t) i].position < from)
                return selectMarker (i);
    }

    //==============================================================================
    void Workspace::toggleRecording()
    {
        if (recording)
            stopRecording();
        else
            startRecording();
    }

    void Workspace::startRecording()
    {
        if (recording || isBusy() || (document != nullptr && ! document->isReady()))
            return;

        stop();

        AudioEngine::InputChoice input;
        input.deviceName = context.settings.getString (keys::recordInputDevice);
        input.firstChannel = (int) context.settings.get (keys::recordFirstChannel);
        input.numChannels = juce::jlimit (1, 2, (int) context.settings.get (keys::recordChannels));

        const bool newDocument = document == nullptr;
        recordRange = newDocument ? FrameRange() : getEditRange();

        const auto error = context.audio.startRecording (context.blocks(), input);
        if (error.isNotEmpty())
        {
            showError (TRANS ("Recording could not start"), error);
            return;
        }

        auto& recorder = context.audio.getRecorder();

        if (newDocument)
        {
            setDocument (AudioDocument::createEmpty (TRANS ("Recording") + " " + juce::String (++newRecordingCount),
                                                     recorder.getNumChannels(), recorder.getSampleRate()));
            zoomToFit();
        }

        Log::info ("Recording: " + context.audio.describeDevice() + ", " + juce::String (recorder.getNumChannels()) + " ch");
        recording = true;
        journalDirty = true;
        setStatus (TRANS ("Recording") + "...");
        sendChangeMessage();
    }

    void Workspace::stopRecording()
    {
        if (! recording)
            return;

        const auto dropped = context.audio.getRecorder().getDroppedFrames();
        auto take = context.audio.stopRecording();
        recording = false;

        Log::info ("Recorded " + juce::String (take.getLengthInSeconds(), 1) + " s"
                   + (dropped > 0 ? ", " + juce::String (dropped) + " frames lost" : juce::String()));

        if (dropped > 0)
            showError (TRANS ("Recording"), TRANS ("The computer could not keep up and a little audio was lost. "
                                                   "Close other programs or choose a larger buffer size."));

        if (take.isEmpty() || document == nullptr)
        {
            setStatus (TRANS ("Stopped"));
            sendChangeMessage();
            return;
        }

        if (! juce::approximatelyEqual (take.getSampleRate(), document->getSampleRate()))
        {
            const auto rate = document->getSampleRate();
            auto converted = std::make_shared<AudioSequence>();
            auto& store = context.blocks();

            runJob (TRANS ("Converting"), [take, rate, converted, &store] (BackgroundJob& j)
            {
                *converted = dsp::resample (take, rate, store, [&j] { return j.shouldContinue(); }, &j.progress);
                return ! converted->isEmpty();
            },
            [this, converted] (bool ok)
            {
                if (ok)
                    insertTake (*converted);
            });
            return;
        }

        insertTake (take);
    }

    void Workspace::insertTake (const AudioSequence& take)
    {
        if (document == nullptr)
            return;

        const bool wasEmpty = document->getAudio().isEmpty();
        auto result = edits::paste (document->getAudioState(), recordRange, take);

        // After recording, the cursor waits at the end of the take, ready for the next one.
        const auto end = result.selection.getEnd();
        result.selection = { end, end };
        applyEdit (TRANS ("Record"), result);

        if (wasEmpty)
            zoomToFit();

        setStatus (TRANS ("Recorded") + " " + formatTime (take.getNumFrames(), take.getSampleRate()));
    }

    //==============================================================================
    void Workspace::writeJournal()
    {
        journalDirty = false;
        lastJournalWrite = juce::Time::getMillisecondCounter();

        const auto file = context.blocks().getFolder().getChildFile (BlockStore::journalFileName);
        const bool needed = document != nullptr && document->isReady() && (document->isModified() || recording);

        if (! needed)
        {
            file.deleteFile();
            return;
        }

        recovery::TakeInfo take;
        if (recording)
        {
            auto& recorder = context.audio.getRecorder();
            take.files = recorder.getSegmentFiles();
            take.numChannels = recorder.getNumChannels();
            take.sampleRate = recorder.getSampleRate();
            take.insertAt = recordRange;
        }

        if (! recovery::write (file, *document, recording ? &take : nullptr))
            Log::warning ("Could not write the recovery journal");
    }

    void Workspace::offerRecovery (const juce::File& runFolder)
    {
        const auto name = recovery::readName (runFolder);

        dialogs::ask (TRANS ("Restore unsaved work?"),
                      TRANS ("MXJ AUDIO did not close properly last time. Your unsaved work can be restored:")
                          + "\n\n" + name,
                      { TRANS ("Restore"), TRANS ("Discard") },
                      [weak = juce::WeakReference<Workspace> (this), runFolder] (int result)
        {
            if (weak == nullptr)
                return;

            if (result != 1)
            {
                Log::info ("Recovery discarded");
                runFolder.deleteRecursively();
                return;
            }

            auto restored = std::make_shared<recovery::Restored>();
            auto& store = weak->context.blocks();

            weak->runJob (TRANS ("Restoring"), [runFolder, restored, &store] (BackgroundJob& j)
            {
                return recovery::restore (runFolder, store, *restored, j);
            },
            [weak, runFolder, restored] (bool ok)
            {
                runFolder.deleteRecursively();

                if (weak == nullptr)
                    return;

                if (! ok)
                {
                    weak->showError (TRANS ("Could not restore the work"), weak->jobError);
                    return;
                }

                Log::info ("Restored " + restored->name);
                weak->setDocument (AudioDocument::createRestored (restored->name, restored->info, restored->state));
                weak->zoomToFit();
                weak->setStatus (TRANS ("Restored") + " " + restored->name);
            });
        });
    }

    //==============================================================================
    void Workspace::timerCallback()
    {
        auto& playback = context.audio.getPlayback();

        if (playback.takeFinishedFlag() || (wasPlaying && ! playback.isPlaying()))
            stop(); // reached the end, or the device changed

        if (recording && context.audio.getRecorder().takeDeviceStoppedFlag())
        {
            stopRecording();
            showError (TRANS ("Recording"), TRANS ("The audio device stopped. The recording so far has been kept."));
        }

        if (job != nullptr)
            setStatus (getBusyText());

        // Follow the playhead page by page, like Audition and Audacity.
        if (isPlaying())
        {
            const auto pos = (double) playback.getPosition();
            const auto right = view.startFrame + view.framesPerPixel * viewWidth;
            if (pos >= right || pos < view.startFrame)
            {
                auto v = view;
                v.startFrame = pos - view.framesPerPixel * viewWidth * 0.05;
                setView (v);
            }
        }

        // Crash recovery: write the journal shortly after changes, and regularly while recording.
        const auto now = juce::Time::getMillisecondCounter();
        if (! isBusy() && ((journalDirty && now - lastJournalWrite > 1500) || (recording && now - lastJournalWrite > 5000)))
            writeJournal();
    }

    //==============================================================================
    double Workspace::getMaxFramesPerPixel() const
    {
        const auto frames = document != nullptr ? (double) document->getLength() : 48000.0;
        return juce::jmax (1.0, frames / juce::jmax (1, viewWidth));
    }

    void Workspace::setView (View v)
    {
        v.framesPerPixel = juce::jlimit (minFramesPerPixel, getMaxFramesPerPixel(), v.framesPerPixel);

        const auto frames = document != nullptr ? (double) document->getLength() : 0.0;
        const auto visible = v.framesPerPixel * viewWidth;
        v.startFrame = juce::jlimit (0.0, juce::jmax (0.0, frames - visible), v.startFrame);

        if (! juce::approximatelyEqual (v.startFrame, view.startFrame)
            || ! juce::approximatelyEqual (v.framesPerPixel, view.framesPerPixel))
        {
            view = v;
            sendChangeMessage();
        }
    }

    void Workspace::setViewWidth (int pixels)
    {
        viewWidth = juce::jmax (1, pixels);
        setView (view);
    }

    double Workspace::zoomAnchor() const
    {
        // Zoom around the playhead when it is visible, otherwise around the middle.
        const auto x = ((double) getPlayheadPosition() - view.startFrame) / view.framesPerPixel;
        return (x >= 0 && x <= viewWidth) ? x : viewWidth * 0.5;
    }

    void Workspace::zoomBy (double factor, double anchorX)
    {
        const auto anchorFrame = view.startFrame + anchorX * view.framesPerPixel;
        View v;
        v.framesPerPixel = juce::jlimit (minFramesPerPixel, getMaxFramesPerPixel(), view.framesPerPixel / factor);
        v.startFrame = anchorFrame - anchorX * v.framesPerPixel;
        setView (v);
    }

    void Workspace::zoomToFit()
    {
        setView ({ 0.0, getMaxFramesPerPixel() });
    }

    void Workspace::zoomToSelection()
    {
        if (! hasSelection())
            return;

        // A little room on both sides.
        const auto length = (double) selection.getLength();
        setView ({ (double) selection.getStart() - length * 0.05, length * 1.1 / juce::jmax (1, viewWidth) });
    }

    juce::String Workspace::formatTime (juce::int64 frame, double sampleRate)
    {
        if (sampleRate <= 0)
            return "0:00:00.000";

        const auto totalMs = (juce::int64) std::floor ((double) frame * 1000.0 / sampleRate);
        const auto ms = (int) (totalMs % 1000);
        const auto s = (int) ((totalMs / 1000) % 60);
        const auto m = (int) ((totalMs / 60000) % 60);
        const auto h = (int) (totalMs / 3600000);

        return juce::String (h) + ":" + juce::String (m).paddedLeft ('0', 2) + ":"
             + juce::String (s).paddedLeft ('0', 2) + "." + juce::String (ms).paddedLeft ('0', 3);
    }
}
