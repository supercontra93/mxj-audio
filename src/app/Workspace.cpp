// SPDX-License-Identifier: AGPL-3.0-only
#include "app/Workspace.h"
#include "app/document/RecoveryJournal.h"
#include "app/editor/ExportDialog.h"
#include "app/editor/NewFileDialog.h"
#include "app/ui/Dialogs.h"
#include "dsp/convert/Resample.h"
#include "dsp/process/Render.h"
#include "dsp/repair/NoiseReduction.h"
#include "dsp/repair/Restoration.h"
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
        const auto storedRack = juce::JSON::parse (context.settings.getString (keys::effectRack));
        rack.setSettings (dsp::chainFromVar (storedRack));
        rack.onChainChanged = [this] { updateLiveChain(); };

        startTimerHz (30);
    }

    Workspace::~Workspace()
    {
        job = nullptr;

        if (recording)
            context.audio.stopRecording();

        rack.onChainChanged = nullptr;
        context.audio.getPlayback().setSequence ({});
        context.audio.getPlayback().setEffectChain (nullptr);
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
        band = {};
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

    void Workspace::showNewFileDialog()
    {
        askToSaveChanges ([weak = juce::WeakReference<Workspace> (this)]
        {
            if (weak == nullptr)
                return;

            NewFileDialog::show (weak->context.settings, juce::TopLevelWindow::getActiveTopLevelWindow(),
                                 [weak] (double sampleRate, int numChannels)
            {
                if (weak != nullptr)
                    weak->newFile (sampleRate, numChannels);
            });
        });
    }

    void Workspace::newFile (double sampleRate, int numChannels)
    {
        if (isBusy() || recording || sampleRate <= 0.0 || numChannels <= 0)
            return;

        if (document != nullptr && document->isModified())
        {
            askToSaveChanges ([this, sampleRate, numChannels] { newFileNow (sampleRate, numChannels); });
            return;
        }

        newFileNow (sampleRate, numChannels);
    }

    void Workspace::newFileNow (double sampleRate, int numChannels)
    {
        const auto name = TRANS ("Untitled") + " " + juce::String (++newFileCount);
        Log::info ("New file: " + juce::String (numChannels) + " ch, " + juce::String (sampleRate) + " Hz");
        setDocument (AudioDocument::createEmpty (name, numChannels, sampleRate));
        zoomToFit();
        setStatus (TRANS ("New file") + ": " + name + " (" + juce::String (juce::roundToInt (sampleRate)) + " Hz, "
                   + (numChannels == 1 ? TRANS ("Mono") : TRANS ("Stereo")) + ")");
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
        band = {};

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
        band = {};
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

        const bool wasEmpty = document->getAudio().isEmpty();
        document->setAudioState (state, revision);

        if (audioChanged)
            context.audio.getPlayback().setSequence (state.audio);

        const auto newRange = newSelection.getIntersectionWith ({ 0, state.audio.getNumFrames() });
        if (newRange != selection)
            band = {}; // a frequency band only stays with the same time selection
        selection = newRange;
        cursor = selection.getStart();
        if (selectedMarker >= (int) state.markers.size())
            selectedMarker = -1;

        if (wasEmpty && ! state.audio.isEmpty())
            zoomToFit(); // the first audio in a new file
        else
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
        if (hasSpectralSelection())
        {
            deleteSelectedFrequencies();
            return;
        }

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
        if (hasSpectralSelection())
        {
            deleteSelectedFrequencies();
            return;
        }

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
        playback.collectRetiredChain();

        // The device changed its sample rate: the live chain must be prepared again.
        if (liveChainWanted && playback.getDeviceSampleRate() > 0.0
            && ! juce::approximatelyEqual (playback.getDeviceSampleRate(), playback.getEffectChainSampleRate()))
            updateLiveChain();

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
    // Effects (phase 3)

    FrameRange Workspace::getEffectRange() const
    {
        if (document == nullptr)
            return {};

        return hasSelection() ? selection : FrameRange (0, document->getLength());
    }

    void Workspace::processRange (const juce::String& name, FrameRange range,
                                  std::function<AudioSequence (const AudioSequence&, BlockStore&, BackgroundJob&)> make,
                                  std::function<void()> afterApplied, bool mayUseBand)
    {
        if (! canEdit() || range.isEmpty())
            return;

        const auto& audio = document->getAudio();
        auto result = std::make_shared<AudioSequence>();
        auto& store = context.blocks();

        // A frequency band is selected: process a little more than the selection,
        // then keep the processed sound inside the band only (master plan, module F).
        const bool useBand = mayUseBand && hasSpectralSelection() && range == selection;
        const dsp::SpectralRegion region { range, band.getStart(), band.getEnd() };
        const auto editRange = useBand ? dsp::spectral::editRange (audio, range) : range;
        auto input = audio.getRange (editRange.getStart(), editRange.getLength());
        const auto keepSelection = selection;

        runJob (name, [input, result, &store, make, useBand, audio, region, editRange] (BackgroundJob& j)
        {
            *result = make (input, store, j);

            if (useBand && result->getNumChannels() > 0 && j.shouldContinue())
            {
                if (result->getNumFrames() != editRange.getLength())
                {
                    j.error = TRANS ("This effect changes the length of the audio, so it cannot work on a frequency band.");
                    *result = {};
                    return false;
                }

                auto processed = audio;
                processed.replace (editRange.getStart(), editRange.getLength(), *result);
                *result = dsp::spectral::blend (audio, processed, region, store,
                                                { [&j] { return j.shouldContinue(); }, &j.progress, 0.5, 1.0 });
            }

            if (result->getNumChannels() == 0 && j.error.isEmpty() && j.shouldContinue())
                j.error = TRANS ("The audio could not be written. Is the disk full?");

            return result->getNumChannels() > 0;
        },
        [this, name, editRange, result, afterApplied, useBand, keepSelection] (bool ok)
        {
            if (! ok || document == nullptr)
            {
                if (jobError.isNotEmpty())
                    showError (name, jobError);
                return;
            }

            auto edit = edits::replaceRange (document->getAudioState(), editRange, *result);
            if (useBand)
                edit.selection = keepSelection; // the spectral selection stays
            applyEdit (name, edit);

            if (afterApplied != nullptr)
                afterApplied();
        });
    }

    void Workspace::applySpectral (const juce::String& name,
                                   std::function<AudioSequence (const AudioSequence&, const dsp::SpectralRegion&, BlockStore&,
                                                                const dsp::WorkControl&)> edit)
    {
        if (! canEdit() || ! hasSelection())
            return;

        const auto audio = document->getAudio();
        const dsp::SpectralRegion region { selection, band.getStart(), band.getEnd() };
        const auto editRange = dsp::spectral::editRange (audio, selection);
        const auto keepSelection = selection;
        auto result = std::make_shared<AudioSequence>();
        auto& store = context.blocks();

        runJob (name, [audio, region, result, &store, edit] (BackgroundJob& j)
        {
            *result = edit (audio, region, store, { [&j] { return j.shouldContinue(); }, &j.progress });

            if (result->getNumChannels() == 0 && j.error.isEmpty() && j.shouldContinue())
                j.error = TRANS ("The audio could not be written. Is the disk full?");

            return result->getNumChannels() > 0;
        },
        [this, name, editRange, result, keepSelection] (bool ok)
        {
            if (! ok || document == nullptr)
            {
                if (jobError.isNotEmpty())
                    showError (name, jobError);
                return;
            }

            auto e = edits::replaceRange (document->getAudioState(), editRange, *result);
            e.selection = keepSelection;
            applyEdit (name, e);
        });
    }

    void Workspace::setSpectralSelection (FrameRange range, juce::Range<double> bandHz)
    {
        setSelection (range);
        if (hasSelection() && document != nullptr)
        {
            const auto nyquist = document->getSampleRate() * 0.5;
            bandHz = bandHz.getIntersectionWith ({ 0.0, nyquist });
            // A band that covers (nearly) everything is a plain time selection.
            band = (bandHz.getLength() >= nyquist * 0.999) ? juce::Range<double>() : bandHz;
            sendChangeMessage();
        }
    }

    void Workspace::healSelection()
    {
        if (! canEdit() || ! hasSelection())
            return;

        applySpectral (TRANS ("Heal"), [] (const AudioSequence& audio, const dsp::SpectralRegion& region, BlockStore& store,
                                           const dsp::WorkControl& control)
        {
            return dsp::spectral::heal (audio, region, store, control);
        });
    }

    void Workspace::changeSelectionLevel (float gainDb)
    {
        if (! canEdit() || ! hasSelection())
            return;

        const auto name = gainDb <= -100.0f ? TRANS ("Delete Frequencies") : TRANS ("Change Level");
        applySpectral (name, [gainDb] (const AudioSequence& audio, const dsp::SpectralRegion& region, BlockStore& store,
                                       const dsp::WorkControl& control)
        {
            return dsp::spectral::changeGain (audio, region, gainDb, store, control);
        });
    }

    void Workspace::showChangeLevelDialog()
    {
        if (! canEdit() || ! hasSelection())
            return;

        const auto what = hasSpectralSelection() ? TRANS ("Raise or lower the selected frequencies by (dB, minus = lower):")
                                                 : TRANS ("Raise or lower the selection by (dB, minus = lower):");
        dialogs::askForText (TRANS ("Change Level"), what, "-12",
                             [weak = juce::WeakReference<Workspace> (this)] (const juce::String& text)
        {
            if (weak == nullptr)
                return;

            const auto value = text.trim().replaceCharacter (',', '.').getFloatValue();
            if (! text.trim().containsAnyOf ("0123456789") || std::abs (value) > 100.0f)
            {
                dialogs::showWarning (TRANS ("Change Level"), TRANS ("Please type a number between -100 and 100."));
                return;
            }

            weak->changeSelectionLevel (value);
        });
    }

    void Workspace::deleteSelectedFrequencies()
    {
        changeSelectionLevel (-200.0f);
    }

    void Workspace::captureNoisePrint()
    {
        if (! canEdit() || ! hasSelection())
            return;

        const auto range = selection;
        if (range.getLength() < (juce::int64) (document->getSampleRate() * 0.1))
        {
            dialogs::showWarning (TRANS ("Capture Noise Print"), TRANS ("Select at least a tenth of a second that holds only the background noise."));
            return;
        }

        auto input = document->getAudio().getRange (range.getStart(), range.getLength());
        auto profile = std::make_shared<dsp::NoiseProfile>();

        runJob (TRANS ("Capture Noise Print"), [input, profile] (BackgroundJob& j)
        {
            *profile = dsp::NoiseProfile::capture (input, { [&j] { return j.shouldContinue(); }, &j.progress });
            return profile->isValid();
        },
        [this, profile] (bool ok)
        {
            if (! ok)
                return;

            dsp::noisePrint::set (profile);
            Log::info ("Noise print captured: " + juce::String (profile->seconds, 2) + " s");
            setStatus (TRANS ("Noise print captured") + " (" + juce::String (profile->seconds, 1) + " s). "
                       + TRANS ("Now select the part to clean and choose Noise Reduction."));
        });
    }

    bool Workspace::hasNoisePrint() const
    {
        const auto p = dsp::noisePrint::get();
        return p != nullptr && p->isValid();
    }

    void Workspace::applyEffect (const dsp::EffectSettings& settings)
    {
        const auto* type = dsp::catalogue::find (settings.typeId);
        if (type == nullptr || ! canEdit())
            return;

        const auto name = TRANS (type->name);
        auto control = [] (BackgroundJob& j) { return dsp::WorkControl { [&j] { return j.shouldContinue(); }, &j.progress }; };

        if (type->kind == dsp::EffectType::Kind::generator)
        {
            const auto durationIndex = type->indexOf ("duration");
            const auto seconds = durationIndex >= 0 ? (double) settings.values[(size_t) durationIndex] : 1.0;
            const auto rate = document->getSampleRate();
            const auto channels = document->getNumChannels();
            const auto frames = (juce::int64) std::llround (seconds * rate);
            auto result = std::make_shared<AudioSequence>();
            auto& store = context.blocks();

            runJob (name, [settings, channels, rate, frames, result, &store, control] (BackgroundJob& j)
            {
                *result = dsp::generate (settings, channels, rate, frames, store, control (j));
                return result->getNumChannels() > 0;
            },
            [this, name, result] (bool ok)
            {
                if (ok && document != nullptr)
                    applyEdit (name, edits::paste (document->getAudioState(), getEditRange(), *result));
            });
            return;
        }

        const auto range = getEffectRange();

        if (type->id == "normalize")
        {
            const auto peakDb = settings.values[0];
            const bool separate = settings.values[1] > 0.5f;

            processRange (name, range, [peakDb, separate, control] (const AudioSequence& input, BlockStore& store, BackgroundJob& j)
            {
                const auto stats = dsp::analyse (input, control (j).part (0.0, 0.5));
                const auto gains = dsp::normalizeGains (stats, peakDb, separate);
                if (gains.empty())
                    return input; // silence stays silence

                const bool allSame = std::all_of (gains.begin(), gains.end(), [&] (float g) { return g == gains[0]; });
                if (allSame)
                    return derived::gainRamp (input, gains[0], gains[0], GainRampSource::Curve::linear); // no copy needed

                return dsp::applyChannelGains (input, gains, store, control (j).part (0.5, 1.0));
            });
            return;
        }

        if (type->id == "matchLoudness")
        {
            const auto target = settings.values[0];
            const auto peakLimit = settings.values[1];
            const bool useLimiter = settings.values[2] > 0.5f;
            auto report = std::make_shared<dsp::LoudnessMatch>();

            processRange (name, range, [=] (const AudioSequence& input, BlockStore& store, BackgroundJob& j)
            {
                *report = dsp::matchLoudness (input, target, peakLimit, useLimiter, store, control (j));
                auto audio = report->audio;
                report->audio = {}; // keep only the numbers
                return audio;
            },
            [this, report, name]
            {
                const auto& a = report->after;
                auto message = TRANS ("Loudness") + ": " + juce::String (report->before.integratedLufs, 1) + " LUFS -> "
                             + juce::String (a.integratedLufs, 1) + " LUFS,  " + TRANS ("true peak") + " "
                             + juce::String (a.truePeakDb, 1) + " dBTP";
                if (report->limited)
                    message << "  (" << TRANS ("limiter used") << ")";

                setStatus (message);

                if (report->before.integratedLufs <= dsp::LoudnessMeter::silence + 1.0f)
                    dialogs::showWarning (name, TRANS ("The selection is too short or too quiet to measure its loudness (at least half a second of sound is needed)."));
                else if (report->peakLimitReached && ! report->limited)
                    dialogs::showInfo (name, message + "\n\n" + TRANS ("The target was not reached: the peaks would pass the true peak limit. Switch on the limiter option to reach it."));
            });
            return;
        }

        if (type->id == "speed")
        {
            const auto percent = (double) settings.values[0];
            processRange (name, range, [percent, control] (const AudioSequence& input, BlockStore& store, BackgroundJob& j)
            {
                return dsp::changeSpeed (input, percent, store, control (j));
            }, nullptr, false);
            return;
        }

        if (type->id == "clickRemoval" || type->id == "clipRepair")
        {
            const bool clicks = type->id == "clickRemoval";
            const auto a = settings.values[0];
            const bool b = settings.values.size() > 1 && settings.values[1] > 0.5f;
            const auto c = settings.values.size() > 1 ? settings.values[1] : 2.0f;
            auto report = std::make_shared<dsp::restore::Report>();

            processRange (name, range, [=] (const AudioSequence& input, BlockStore& store, BackgroundJob& j)
            {
                return clicks ? dsp::restore::removeClicks (input, a, c, store, control (j), report.get())
                              : dsp::restore::repairClipping (input, a, b, store, control (j), report.get());
            },
            [this, report, clicks]
            {
                auto message = (clicks ? TRANS ("Clicks removed") : TRANS ("Clipped peaks repaired")) + ": " + juce::String (report->repaired);
                if (std::abs (report->gainDb) > 0.01f)
                    message << "  (" << TRANS ("level") << " " << juce::String (report->gainDb, 1) << " dB)";
                setStatus (message);
                Log::info (message);
            });
            return;
        }

        if (type->create == nullptr)
            return;

        // A real-time effect: render it. The audio after the range feeds look-ahead effects.
        auto following = document->getAudio().getRange (range.getEnd(), (juce::int64) document->getSampleRate());
        processRange (name, range, [settings, following, control] (const AudioSequence& input, BlockStore& store, BackgroundJob& j)
        {
            auto chain = dsp::ProcessorChain::create ({ settings });
            return dsp::render (input, *chain, store, following, control (j));
        });
    }

    void Workspace::applyRack()
    {
        if (! rack.hasActiveEffects())
        {
            setStatus (TRANS ("The effect rack has no switched-on effects."));
            return;
        }

        // Applying the rack: listening live to it afterwards would apply it twice.
        const auto chainSettings = rack.getSettings();
        const auto range = getEffectRange();
        auto following = document != nullptr ? document->getAudio().getRange (range.getEnd(), (juce::int64) document->getSampleRate())
                                             : AudioSequence();

        processRange (TRANS ("Effect Rack"), range, [chainSettings, following] (const AudioSequence& input, BlockStore& store, BackgroundJob& j)
        {
            auto chain = dsp::ProcessorChain::create (chainSettings);
            return dsp::render (input, *chain, store, following, { [&j] { return j.shouldContinue(); }, &j.progress });
        },
        [this]
        {
            if (rack.isMonitoring())
            {
                rack.setMonitoring (false);
                setStatus (TRANS ("Effect rack applied. Live listening was switched off, so the effects are not heard twice."));
            }
        });
    }

    void Workspace::reverseAudio()
    {
        processRange (TRANS ("Reverse"), getEffectRange(), [] (const AudioSequence& input, BlockStore& store, BackgroundJob& j)
        {
            return dsp::reverse (input, store, { [&j] { return j.shouldContinue(); }, &j.progress });
        }, nullptr, false);
    }

    void Workspace::invertAudio()
    {
        if (canEdit())
            applyEdit (TRANS ("Invert"), edits::gain (document->getAudioState(), getEffectRange(), -1.0f));
    }

    void Workspace::analyse (std::function<void (const dsp::AudioStats&)> onDone)
    {
        if (! isReady() || isBusy() || isRecording())
            return;

        const auto range = getEffectRange();
        auto input = document->getAudio().getRange (range.getStart(), range.getLength());
        auto stats = std::make_shared<dsp::AudioStats>();

        runJob (TRANS ("Analysing"), [input, stats] (BackgroundJob& j)
        {
            *stats = dsp::analyse (input, { [&j] { return j.shouldContinue(); }, &j.progress });
            return stats->valid;
        },
        [stats, onDone] (bool ok)
        {
            if (ok && onDone != nullptr)
                onDone (*stats);
        });
    }

    void Workspace::startPreview (const dsp::EffectType& type, dsp::ParameterValuesPtr values)
    {
        previewType = &type;
        previewValues = std::move (values);
        updateLiveChain();

        if (! isPlaying())
            play();
    }

    void Workspace::stopPreview()
    {
        if (previewType == nullptr)
            return;

        previewType = nullptr;
        previewValues = nullptr;
        stop();
        updateLiveChain();
    }

    void Workspace::updateLiveChain()
    {
        auto& playback = context.audio.getPlayback();

        if (previewType != nullptr && previewType->create != nullptr)
        {
            auto chain = std::make_unique<dsp::ProcessorChain>();
            chain->add (previewType->create (previewValues));
            playback.setEffectChain (std::move (chain));
            liveChainWanted = true;
        }
        else if (rack.isMonitoring() && rack.size() > 0)
        {
            playback.setEffectChain (rack.createLiveChain());
            liveChainWanted = true;
        }
        else
        {
            playback.setEffectChain (nullptr);
            liveChainWanted = false;
        }
    }

    void Workspace::storeSettings()
    {
        context.settings.set (keys::effectRack, juce::JSON::toString (dsp::chainToVar (rack.getSettings()), true));
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
