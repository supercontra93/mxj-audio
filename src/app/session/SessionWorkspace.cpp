// SPDX-License-Identifier: AGPL-3.0-only
#include "app/session/SessionWorkspace.h"
#include "app/editor/ExportDialog.h"
#include "app/editor/NewFileDialog.h"
#include "app/ui/Dialogs.h"
#include "dsp/convert/Resample.h"
#include "formats/import/AudioImporter.h"
#include "formats/import/FileSampleSource.h"
#include "services/logging/Log.h"
#include "services/settings/SettingKeys.h"

namespace mxj
{
    //==============================================================================
    /** One undo step: the whole session before and after (block lists, never audio). */
    class SessionWorkspace::EditAction final : public juce::UndoableAction
    {
    public:
        EditAction (SessionWorkspace& w, Session before, Session after, juce::int64 revBefore, juce::int64 revAfter)
            : workspace (w), stateBefore (std::move (before)), stateAfter (std::move (after)),
              revisionBefore (revBefore), revisionAfter (revAfter) {}

        bool perform() override { workspace.applyState (stateAfter, revisionAfter); return true; }
        bool undo() override    { workspace.applyState (stateBefore, revisionBefore); return true; }
        int getSizeInUnits() override { return 1; }

    private:
        SessionWorkspace& workspace;
        Session stateBefore, stateAfter;
        juce::int64 revisionBefore, revisionAfter;
    };

    namespace
    {
        /** Playback reads this far ahead (device frames): mixer moves are heard within about 0.15 s. */
        constexpr int sessionLookahead = 8192;

        juce::String trackLabel (const Session& s, Track::Kind kind)
        {
            int count = 1;
            for (const auto& t : s.tracks)
                if (t.kind == kind)
                    ++count;

            return (kind == Track::Kind::bus ? TRANS ("Bus") : TRANS ("Track")) + " " + juce::String (count);
        }
    }

    //==============================================================================
    SessionWorkspace::SessionWorkspace (AppContext& ctx, Workspace& w)
        : context (ctx), waveform (w), mixer (std::make_shared<SessionMixer>())
    {
        snapping = context.settings.getBool (keys::sessionSnapping);
        startTimerHz (30);
    }

    SessionWorkspace::~SessionWorkspace()
    {
        job = nullptr;

        if (recording)
            context.audio.stopRecording();

        context.audio.getSessionPlayback().setSource (nullptr);
        history.clear();
    }

    void SessionWorkspace::setStatus (const juce::String& message)
    {
        if (onStatusMessage != nullptr)
            onStatusMessage (message);
    }

    void SessionWorkspace::showError (const juce::String& title, const juce::String& message)
    {
        Log::warning (title + ": " + message);
        dialogs::showWarning (title, message);
    }

    juce::String SessionWorkspace::getDisplayName() const
    {
        if (session == nullptr)
            return {};

        return file != juce::File() ? file.getFileNameWithoutExtension()
                                    : TRANS ("Untitled Session") + " " + juce::String (untitledCount);
    }

    void SessionWorkspace::setActive (bool shouldBeActive)
    {
        shouldBeActive = shouldBeActive && session != nullptr;
        if (active == shouldBeActive)
            return;

        // One view plays at a time.
        stop();
        waveform.stop();
        active = shouldBeActive;
        sendChangeMessage();
        waveform.sendChangeMessage();
    }

    //==============================================================================
    void SessionWorkspace::askToSaveChanges (std::function<void()> proceed)
    {
        if (isBusy())
        {
            setStatus (TRANS ("Please wait until the current task has finished."));
            return;
        }

        if (recording)
            stopRecording();

        if (! isModified())
        {
            proceed();
            return;
        }

        dialogs::ask (TRANS ("Save changes?"),
                      TRANS ("Do you want to save the changes to this session?") + "\n\n" + getDisplayName(),
                      { TRANS ("Save"), TRANS ("Don't Save"), TRANS ("Cancel") },
                      [weak = juce::WeakReference<SessionWorkspace> (this), proceed] (int result)
        {
            if (weak == nullptr)
                return;

            if (result == 2)
                proceed();
            else if (result == 1)
                weak->save (proceed);
        });
    }

    void SessionWorkspace::showNewSessionDialog()
    {
        askToSaveChanges ([this]
        {
            NewFileDialog::show (context.settings, nullptr, [this] (double rate, int) { newSession (rate); }, NewFileDialog::Kind::session);
        });
    }

    void SessionWorkspace::newSession (double sampleRate)
    {
        auto s = std::make_unique<Session>();
        s->sampleRate = sampleRate;

        for (int i = 0; i < 4; ++i)
            sessionEdits::addTrack (*s, Track::Kind::audio, trackLabel (*s, Track::Kind::audio));

        ++untitledCount;
        setSessionNow (std::move (s), {});
        Log::info ("New session: " + juce::String (sampleRate) + " Hz");
        setStatus (TRANS ("New multitrack session") + " (" + juce::String (juce::roundToInt (sampleRate)) + " Hz)");
    }

    void SessionWorkspace::setSessionNow (std::unique_ptr<Session> newSession, const juce::File& newFile)
    {
        stop();
        history.clear();
        gestureStart = nullptr;
        session = std::move (newSession);
        file = newFile;
        copiedFiles.clear();
        revision = savedRevision = 0;
        cursor = 0;
        timeSelection = {};
        selectedClips.clear();
        selectedTrack = session != nullptr && ! session->tracks.empty() ? session->tracks.front().id : 0;
        view = {};

        auto& playback = context.audio.getSessionPlayback();
        if (session != nullptr)
        {
            mixer->setSession (*session);
            playback.setLookahead (sessionLookahead);
            playback.setSource (mixer);
            zoomToFit();
        }
        else
        {
            playback.setSource (nullptr);
        }

        active = session != nullptr;
        if (active)
            waveform.stop();

        sendChangeMessage();
        waveform.sendChangeMessage();
    }

    void SessionWorkspace::closeSession()
    {
        askToSaveChanges ([this]
        {
            setSessionNow (nullptr, {});
            setStatus (TRANS ("Ready"));
        });
    }

    //==============================================================================
    void SessionWorkspace::runJob (const juce::String& title, BackgroundJob::Work work, JobDone done)
    {
        stop();
        job = std::make_unique<BackgroundJob> (title, std::move (work),
                                               [this, done] (bool ok)
        {
            auto finished = std::move (job); // deleted after `done`
            const auto error = finished->error;
            done (ok && error.isEmpty(), error);
            sendChangeMessage();
        });
        sendChangeMessage();
    }

    juce::String SessionWorkspace::getBusyText() const
    {
        if (job == nullptr)
            return {};

        return job->getName() + "... " + juce::String (juce::roundToInt (job->progress.load() * 100.0)) + "%";
    }

    void SessionWorkspace::openProject (const juce::File& projectFile)
    {
        askToSaveChanges ([this, projectFile]
        {
            auto loaded = std::make_shared<Session>();
            auto missing = std::make_shared<juce::StringArray>();
            auto& store = context.blocks();
            const auto peaks = context.paths.getCacheFolder().getChildFile ("peaks");
            Log::info ("Opening project " + projectFile.getFullPathName());

            runJob (TRANS ("Opening"), [projectFile, loaded, missing, &store, peaks] (BackgroundJob& j)
            {
                return projectFile::load (projectFile, store, peaks, *loaded, *missing, j);
            },
            [this, projectFile, loaded, missing] (bool ok, const juce::String& error)
            {
                if (! ok)
                {
                    showError (TRANS ("Could not open the project"), projectFile.getFullPathName() + "\n\n" + error);
                    return;
                }

                setSessionNow (std::make_unique<Session> (*loaded), projectFile);
                context.settings.set (keys::lastOpenFolder, projectFile.getParentDirectory().getFullPathName());
                setStatus (TRANS ("Opened") + " " + projectFile.getFileName());
                Log::info ("Project opened: " + juce::String (session->tracks.size()) + " tracks");

                if (! missing->isEmpty())
                    showError (TRANS ("Some audio files are missing"),
                               TRANS ("These files were not found. Their clips stay silent until the files are back in place:")
                                   + "\n\n" + missing->joinIntoString ("\n"));
            });
        });
    }

    void SessionWorkspace::save (std::function<void()> afterSaved)
    {
        if (! canEdit())
            return;

        if (file == juce::File())
            showSaveAsDialog (afterSaved);
        else
            saveAs (file, afterSaved);
    }

    void SessionWorkspace::showSaveAsDialog (std::function<void()> afterSaved)
    {
        if (! canEdit())
            return;

        auto folder = juce::File (context.settings.getString (keys::lastSaveFolder));
        if (! folder.isDirectory())
            folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory);

        const auto suggested = file != juce::File() ? file : folder.getChildFile (getDisplayName() + projectFile::extension);
        chooser = std::make_unique<juce::FileChooser> (TRANS ("Save Session As"), suggested, "*.mxj", true);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, afterSaved] (const juce::FileChooser& fc)
        {
            auto target = fc.getResult();
            if (target == juce::File())
                return;

            if (! target.hasFileExtension (projectFile::extension))
                target = target.withFileExtension (projectFile::extension);

            context.settings.set (keys::lastSaveFolder, target.getParentDirectory().getFullPathName());
            saveAs (target, afterSaved);
        });
    }

    void SessionWorkspace::saveAs (const juce::File& target, std::function<void()> afterSaved)
    {
        if (! canEdit())
            return;

        auto snapshot = std::make_shared<Session> (*session);
        auto copied = std::make_shared<projectFile::CopiedFiles> (copiedFiles);
        const auto savedAt = revision;
        Log::info ("Saving project " + target.getFullPathName());

        runJob (TRANS ("Saving"), [snapshot, copied, target] (BackgroundJob& j)
        {
            return projectFile::save (*snapshot, target, *copied, j);
        },
        [this, target, copied, savedAt, afterSaved] (bool ok, const juce::String& error)
        {
            if (! ok)
            {
                showError (TRANS ("Could not save the session"), target.getFullPathName() + "\n\n" + error);
                return;
            }

            copiedFiles = *copied;
            file = target;
            savedRevision = savedAt;
            setStatus (TRANS ("Saved") + " " + target.getFileName());
            Log::info ("Project saved");

            if (afterSaved != nullptr)
                afterSaved();
        });
    }

    //==============================================================================
    void SessionWorkspace::showImportDialog()
    {
        if (! canEdit())
            return;

        auto folder = juce::File (context.settings.getString (keys::lastOpenFolder));
        if (! folder.isDirectory())
            folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory);

        chooser = std::make_unique<juce::FileChooser> (TRANS ("Import audio files"), folder, audioFormats::getWildcard(), true);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& fc)
        {
            const auto files = fc.getResults();
            if (files.isEmpty())
                return;

            context.settings.set (keys::lastOpenFolder, files[0].getParentDirectory().getFullPathName());
            importFiles (files, selectedTrack, cursor);
        });
    }

    void SessionWorkspace::importFiles (const juce::Array<juce::File>& files, int trackId, juce::int64 position)
    {
        if (! canEdit() || files.isEmpty())
            return;

        struct Imported
        {
            juce::File file;
            AudioSequence audio;
        };

        auto results = std::make_shared<std::vector<Imported>>();
        auto& store = context.blocks();
        const auto peaks = context.paths.getCacheFolder().getChildFile ("peaks");
        const auto rate = session->sampleRate;

        runJob (TRANS ("Importing"), [files, results, &store, peaks, rate] (BackgroundJob& j)
        {
            for (int i = 0; i < files.size(); ++i)
            {
                juce::String error;
                auto importer = AudioImporter::create (files[i], store, peaks, error);
                if (importer == nullptr)
                {
                    j.error = files[i].getFileName() + ": " + error;
                    return false;
                }

                if (importer->run ([&j] { return j.shouldContinue(); }) != AudioImporter::Result::ok)
                {
                    j.error = j.shouldContinue() ? files[i].getFileName() + ": " + importer->getError() : juce::String();
                    return false;
                }

                auto audio = importer->getSequence();

                // Clips play at the session's sample rate.
                if (! juce::approximatelyEqual (audio.getSampleRate(), rate))
                {
                    audio = dsp::resample (audio, rate, store, [&j] { return j.shouldContinue(); });
                    if (audio.getNumChannels() == 0)
                        return false;
                }

                results->push_back ({ files[i], audio });
                j.progress = (double) (i + 1) / (double) files.size();
            }

            return true;
        },
        [this, results, trackId, position] (bool ok, const juce::String& error)
        {
            if (! ok)
            {
                if (error.isNotEmpty())
                    showError (TRANS ("Could not import the file"), error);
                return;
            }

            auto s = *session;
            auto target = trackId;
            if (const auto* t = s.findTrack (target); t == nullptr || ! t->isAudio())
                target = 0;

            selectedClips.clear();
            for (size_t i = 0; i < results->size(); ++i)
            {
                const auto& r = (*results)[i];

                // The first file goes onto the chosen track, the others onto new tracks below it.
                if (target == 0 || i > 0)
                {
                    const auto index = target != 0 ? s.indexOfTrack (target) + 1 : -1;
                    target = sessionEdits::addTrack (s, Track::Kind::audio, r.file.getFileNameWithoutExtension(), index);
                }

                const auto id = sessionEdits::addClip (s, target, sessionEdits::makeClip (s, r.file.getFileNameWithoutExtension(), r.audio, position));
                selectedClips.push_back (id);
                Log::info ("Imported " + r.file.getFullPathName() + " (" + juce::String (r.audio.getLengthInSeconds(), 1) + " s)");
            }

            selectedTrack = target;
            applyEdit (TRANS ("Import"), s);

            if (session->getLength() > (juce::int64) (view.startFrame + view.framesPerPixel * viewWidth))
                zoomToFit();

            setStatus (TRANS ("Imported") + " " + juce::String ((int) results->size()) + " " + TRANS ("file(s)"));
        });
    }

    //==============================================================================
    void SessionWorkspace::addTrack()
    {
        if (! canEdit())
            return;

        auto s = *session;
        const auto index = selectedTrack != 0 ? s.indexOfTrack (selectedTrack) + 1 : -1;
        selectedTrack = sessionEdits::addTrack (s, Track::Kind::audio, trackLabel (s, Track::Kind::audio), index);
        applyEdit (TRANS ("Add Track"), s);
    }

    void SessionWorkspace::addBus()
    {
        if (! canEdit())
            return;

        auto s = *session;
        selectedTrack = sessionEdits::addTrack (s, Track::Kind::bus, trackLabel (s, Track::Kind::bus));
        applyEdit (TRANS ("Add Bus"), s);
    }

    void SessionWorkspace::removeSelectedTrack()
    {
        if (! canEdit() || session->findTrack (selectedTrack) == nullptr || selectedTrack == 0)
            return;

        auto s = *session;
        const auto index = s.indexOfTrack (selectedTrack);
        sessionEdits::removeTrack (s, selectedTrack);
        selectedTrack = s.tracks.empty() ? 0 : s.tracks[(size_t) juce::jmin (index, (int) s.tracks.size() - 1)].id;
        applyEdit (TRANS ("Remove Track"), s);
    }

    void SessionWorkspace::moveSelectedTrack (int delta)
    {
        if (! canEdit() || selectedTrack == 0)
            return;

        auto s = *session;
        if (sessionEdits::moveTrack (s, selectedTrack, s.indexOfTrack (selectedTrack) + delta))
            applyEdit (TRANS ("Move Track"), s);
    }

    void SessionWorkspace::selectTrack (int trackId)
    {
        if (selectedTrack != trackId)
        {
            selectedTrack = trackId;
            sendChangeMessage();
        }
    }

    //==============================================================================
    void SessionWorkspace::setTrackName (int trackId, const juce::String& name)
    {
        if (! canEdit() || name.trim().isEmpty())
            return;

        auto s = *session;
        if (auto* t = s.findTrack (trackId); t != nullptr && trackId != 0)
        {
            t->name = name.trim();
            applyEdit (TRANS ("Rename Track"), s);
        }
    }

    void SessionWorkspace::setTrackVolume (int trackId, float db, bool commit)
    {
        if (session == nullptr)
            return;

        db = juce::jlimit (-96.0f, 12.0f, db);
        mixer->setTrackVolume (trackId, db);

        if (commit && canEdit())
        {
            auto s = *session;
            if (auto* t = s.findTrack (trackId); t != nullptr && ! juce::exactlyEqual (t->volumeDb, db))
            {
                t->volumeDb = db;
                applyEdit (TRANS ("Change Volume"), s);
            }
        }
    }

    void SessionWorkspace::setTrackPan (int trackId, float pan, bool commit)
    {
        if (session == nullptr)
            return;

        pan = juce::jlimit (-1.0f, 1.0f, pan);
        mixer->setTrackPan (trackId, pan);

        if (commit && canEdit())
        {
            auto s = *session;
            if (auto* t = s.findTrack (trackId); t != nullptr && ! juce::exactlyEqual (t->pan, pan))
            {
                t->pan = pan;
                applyEdit (TRANS ("Change Pan"), s);
            }
        }
    }

    void SessionWorkspace::setSendLevel (int trackId, float db, bool commit)
    {
        const auto* track = session != nullptr ? session->findTrack (trackId) : nullptr;
        if (track == nullptr || track->sends.empty())
            return;

        db = juce::jlimit (-96.0f, 12.0f, db);
        mixer->setSendLevel (trackId, track->sends.front().busId, db);

        if (commit && canEdit())
        {
            auto s = *session;
            s.findTrack (trackId)->sends.front().levelDb = db;
            applyEdit (TRANS ("Change Send Level"), s);
        }
    }

    void SessionWorkspace::setTrackOutput (int trackId, int busId)
    {
        if (! canEdit())
            return;

        auto s = *session;
        auto* t = s.findTrack (trackId);
        const auto* bus = s.findTrack (busId);
        if (t == nullptr || ! t->isAudio() || (busId != 0 && (bus == nullptr || ! bus->isBus())) || t->outputId == busId)
            return;

        t->outputId = busId;
        applyEdit (TRANS ("Change Output"), s);
    }

    void SessionWorkspace::setSendBus (int trackId, int busId)
    {
        if (! canEdit())
            return;

        auto s = *session;
        auto* t = s.findTrack (trackId);
        if (t == nullptr || ! t->isAudio())
            return;

        const auto level = t->sends.empty() ? -6.0f : t->sends.front().levelDb;
        t->sends.clear();
        if (const auto* bus = s.findTrack (busId); busId != 0 && bus != nullptr && bus->isBus())
            t->sends.push_back ({ busId, level });

        applyEdit (TRANS ("Change Send"), s);
    }

    void SessionWorkspace::toggleMute (int trackId)
    {
        if (! canEdit())
            return;

        auto s = *session;
        if (auto* t = s.findTrack (trackId))
        {
            t->mute = ! t->mute;
            applyEdit (t->mute ? TRANS ("Mute") : TRANS ("Unmute"), s);
        }
    }

    void SessionWorkspace::toggleSolo (int trackId)
    {
        if (! canEdit() || trackId == 0)
            return;

        auto s = *session;
        if (auto* t = s.findTrack (trackId))
        {
            t->solo = ! t->solo;
            applyEdit (TRANS ("Solo"), s);
        }
    }

    void SessionWorkspace::toggleArm (int trackId)
    {
        if (! canEdit())
            return;

        auto s = *session;
        auto* t = s.findTrack (trackId);
        if (t == nullptr || ! t->isAudio())
            return;

        // One input, one take: arming a track disarms the others.
        const auto arm = ! t->armed;
        for (auto& other : s.tracks)
            other.armed = false;

        t->armed = arm;
        applyEdit (TRANS ("Arm for Recording"), s);
    }

    void SessionWorkspace::toggleAutomation (int trackId)
    {
        if (! canEdit())
            return;

        auto s = *session;
        if (auto* t = s.findTrack (trackId); t != nullptr && trackId != 0)
        {
            t->showAutomation = ! t->showAutomation;
            applyEdit (TRANS ("Show Automation"), s);
        }
    }

    void SessionWorkspace::clearAutomation (int trackId)
    {
        if (! canEdit())
            return;

        auto s = *session;
        if (auto* t = s.findTrack (trackId))
        {
            t->volumeAutomation = {};
            t->panAutomation = {};
            applyEdit (TRANS ("Clear Automation"), s);
        }
    }

    void SessionWorkspace::setTrackEffects (int trackId, const juce::var& effects, bool commit)
    {
        if (session == nullptr)
            return;

        auto s = *session;
        auto* t = s.findTrack (trackId);
        if (t == nullptr)
            return;

        t->effects = effects;

        if (commit && canEdit())
        {
            applyEdit (TRANS ("Change Track Effects"), s);
        }
        else
        {
            // Heard at once; the undo step follows when the change is committed.
            mixer->setSession (s);
        }
    }

    //==============================================================================
    bool SessionWorkspace::isClipSelected (int clipId) const
    {
        return std::find (selectedClips.begin(), selectedClips.end(), clipId) != selectedClips.end();
    }

    void SessionWorkspace::selectClip (int clipId, bool addToSelection)
    {
        if (! addToSelection)
            selectedClips.clear();

        if (! isClipSelected (clipId))
            selectedClips.push_back (clipId);
        else if (addToSelection)
            selectedClips.erase (std::find (selectedClips.begin(), selectedClips.end(), clipId));

        if (const auto* t = session != nullptr ? session->findTrackOfClip (clipId) : nullptr)
            selectedTrack = t->id;

        timeSelection = {};
        sendChangeMessage();
    }

    void SessionWorkspace::selectNone()
    {
        selectedClips.clear();
        timeSelection = {};
        sendChangeMessage();
    }

    std::vector<int> SessionWorkspace::targetTracks() const
    {
        // Edits of a time range act on the selected track if one is selected, else on all.
        if (const auto* t = session->findTrack (selectedTrack); t != nullptr && t->isAudio())
            return { selectedTrack };

        return {};
    }

    void SessionWorkspace::splitAtCursor()
    {
        if (! canEdit())
            return;

        auto s = *session;
        int count = 0;

        if (! selectedClips.empty())
        {
            for (auto id : selectedClips)
                if (sessionEdits::splitClip (s, id, cursor) != 0)
                    ++count;
        }
        else
        {
            count = sessionEdits::splitAt (s, cursor, targetTracks());
        }

        if (count == 0)
        {
            setStatus (TRANS ("Place the cursor inside a clip to split it."));
            return;
        }

        applyEdit (TRANS ("Split"), s);
    }

    void SessionWorkspace::deleteSelection (bool ripple)
    {
        if (! canEdit())
            return;

        auto s = *session;

        if (! selectedClips.empty())
        {
            sessionEdits::deleteClips (s, selectedClips, ripple);
            selectedClips.clear();
        }
        else if (! timeSelection.isEmpty())
        {
            sessionEdits::deleteRange (s, timeSelection, ripple, targetTracks());
            if (ripple)
                timeSelection = {};
        }
        else
        {
            return;
        }

        applyEdit (ripple ? TRANS ("Ripple Delete") : TRANS ("Delete"), s);
    }

    void SessionWorkspace::copyClips()
    {
        if (session == nullptr || selectedClips.empty())
            return;

        clipboard.clear();
        juce::int64 first = std::numeric_limits<juce::int64>::max();

        for (auto id : selectedClips)
            if (const auto* c = session->findClip (id))
            {
                clipboard.push_back (*c);
                first = juce::jmin (first, c->position);
            }

        for (auto& c : clipboard)
            c.position -= first;

        setStatus (TRANS ("Copied") + " " + juce::String ((int) clipboard.size()) + " " + TRANS ("clip(s)"));
    }

    void SessionWorkspace::pasteClips()
    {
        if (! canEdit() || clipboard.empty())
            return;

        auto s = *session;
        auto target = selectedTrack;
        if (const auto* t = s.findTrack (target); t == nullptr || ! t->isAudio())
        {
            target = 0;
            for (const auto& other : s.tracks)
                if (other.isAudio())
                {
                    target = other.id;
                    break;
                }
        }

        if (target == 0)
            target = sessionEdits::addTrack (s, Track::Kind::audio, trackLabel (s, Track::Kind::audio));

        selectedClips.clear();
        juce::int64 end = cursor;
        for (auto c : clipboard)
        {
            c.id = 0;
            c.position += cursor;
            end = juce::jmax (end, c.getEnd());
            selectedClips.push_back (sessionEdits::addClip (s, target, c));
        }

        applyEdit (TRANS ("Paste"), s);
        setCursor (end);
    }

    void SessionWorkspace::toggleClipMute()
    {
        if (! canEdit() || selectedClips.empty())
            return;

        auto s = *session;
        for (auto id : selectedClips)
            if (auto* t = s.findTrackOfClip (id))
                t->findClip (id)->muted = ! t->findClip (id)->muted;

        applyEdit (TRANS ("Mute Clip"), s);
    }

    void SessionWorkspace::showClipGainDialog()
    {
        if (! canEdit() || selectedClips.empty())
            return;

        const auto* clip = session->findClip (selectedClips.front());
        if (clip == nullptr)
            return;

        dialogs::askForText (TRANS ("Clip Volume"), TRANS ("Volume of the selected clips in dB (-96 to +24):"),
                             juce::String (clip->gainDb, 1),
                             [weak = juce::WeakReference<SessionWorkspace> (this)] (const juce::String& text)
        {
            if (weak == nullptr || ! weak->canEdit())
                return;

            auto s = *weak->session;
            for (auto id : weak->selectedClips)
                sessionEdits::setClipGain (s, id, text.trim().getFloatValue());

            weak->applyEdit (TRANS ("Clip Volume"), s);
        });
    }

    void SessionWorkspace::showRenameClipDialog()
    {
        if (! canEdit() || selectedClips.empty())
            return;

        const auto id = selectedClips.front();
        const auto* clip = session->findClip (id);
        if (clip == nullptr)
            return;

        dialogs::askForText (TRANS ("Rename Clip"), TRANS ("Name of the clip:"), clip->name,
                             [weak = juce::WeakReference<SessionWorkspace> (this), id] (const juce::String& text)
        {
            if (weak == nullptr || ! weak->canEdit() || text.trim().isEmpty())
                return;

            auto s = *weak->session;
            if (auto* t = s.findTrackOfClip (id))
            {
                t->findClip (id)->name = text.trim();
                weak->applyEdit (TRANS ("Rename Clip"), s);
            }
        });
    }

    //==============================================================================
    void SessionWorkspace::beginGesture()
    {
        if (canEdit() && gestureStart == nullptr)
        {
            gestureStart = std::make_unique<Session> (*session);
            gestureChanged = false;
        }
    }

    void SessionWorkspace::updateGesture (const Session& changed)
    {
        if (gestureStart == nullptr || session == nullptr)
            return;

        *session = changed;
        gestureChanged = true;
        mixer->setSession (*session);
        sendChangeMessage();
    }

    void SessionWorkspace::endGesture (const juce::String& undoName)
    {
        if (gestureStart == nullptr)
            return;

        auto before = std::move (gestureStart);
        auto after = *session;
        *session = *before; // the undo step applies `after` again

        if (gestureChanged)
            applyEdit (undoName, after);
        else
            applyState (*session, revision);
    }

    void SessionWorkspace::applyEdit (const juce::String& name, const Session& changed)
    {
        if (session == nullptr)
            return;

        const auto newRevision = nextRevision++;
        history.beginStep (name);
        history.perform (new EditAction (*this, *session, changed, revision, newRevision));
    }

    void SessionWorkspace::applyState (const Session& state, juce::int64 newRevision)
    {
        if (session == nullptr)
            return;

        *session = state;
        revision = newRevision;

        // Forget selections that point at nothing any more.
        selectedClips.erase (std::remove_if (selectedClips.begin(), selectedClips.end(),
                                             [this] (int id) { return session->findClip (id) == nullptr; }),
                             selectedClips.end());
        if (selectedTrack != 0 && session->findTrack (selectedTrack) == nullptr)
            selectedTrack = session->tracks.empty() ? 0 : session->tracks.front().id;

        sessionChanged();
    }

    void SessionWorkspace::sessionChanged()
    {
        mixer->setSession (*session);
        sendChangeMessage();
    }

    bool SessionWorkspace::canUndo() const { return canEdit() && history.canUndo(); }
    bool SessionWorkspace::canRedo() const { return canEdit() && history.canRedo(); }

    void SessionWorkspace::undo()
    {
        if (! canUndo())
            return;

        const auto name = history.getUndoName();
        history.undo();
        setStatus (TRANS ("Undo") + ": " + name);
    }

    void SessionWorkspace::redo()
    {
        if (! canRedo())
            return;

        const auto name = history.getRedoName();
        history.redo();
        setStatus (TRANS ("Redo") + ": " + name);
    }

    //==============================================================================
    void SessionWorkspace::setCursor (juce::int64 frame)
    {
        cursor = juce::jmax ((juce::int64) 0, frame);
        timeSelection = {};

        if (isPlaying())
            play();

        sendChangeMessage();
    }

    void SessionWorkspace::setTimeSelection (FrameRange range)
    {
        timeSelection = range.getStart() < 0 ? FrameRange() : range;
        cursor = timeSelection.getStart();
        selectedClips.clear();
        sendChangeMessage();
    }

    void SessionWorkspace::toggleSnapping()
    {
        snapping = ! snapping;
        context.settings.set (keys::sessionSnapping, snapping);
        setStatus (snapping ? TRANS ("Snapping on") : TRANS ("Snapping off"));
        sendChangeMessage();
    }

    juce::int64 SessionWorkspace::snap (juce::int64 frame, const std::vector<int>& ignoreClips) const
    {
        if (! snapping || session == nullptr)
            return frame;

        const auto tolerance = (juce::int64) (8.0 * view.framesPerPixel);
        return sessionEdits::snap (*session, frame, tolerance, ignoreClips, { cursor });
    }

    //==============================================================================
    void SessionWorkspace::togglePlayback()
    {
        if (isPlaying())
            stop();
        else
            play();
    }

    void SessionWorkspace::play()
    {
        if (session == nullptr || isBusy())
            return;

        waveform.stop();
        auto& playback = context.audio.getSessionPlayback();
        const auto end = timeSelection.isEmpty() ? (juce::int64) -1 : timeSelection.getEnd();

        if (! playback.play (cursor, end))
        {
            if (session->getLength() == 0)
                setStatus (TRANS ("The session is empty. Import audio files first (Multitrack > Import Files)."));
            else if (! context.audio.hasRunningDevice())
                setStatus (TRANS ("No audio device. Choose one in Edit > Audio Device Settings."));
            return;
        }

        wasPlaying = true;
        sendChangeMessage();
    }

    void SessionWorkspace::stop()
    {
        auto& playback = context.audio.getSessionPlayback();
        const bool was = playback.isPlaying() || wasPlaying;
        playback.stop();
        wasPlaying = false;

        if (was)
            sendChangeMessage();
    }

    bool SessionWorkspace::isPlaying() const
    {
        return context.audio.getSessionPlayback().isPlaying();
    }

    juce::int64 SessionWorkspace::getPlayheadPosition() const
    {
        if (isPlaying())
            return context.audio.getSessionPlayback().getPosition();

        return cursor;
    }

    void SessionWorkspace::goToStart() { setCursor (0); }

    void SessionWorkspace::goToEnd()
    {
        if (session != nullptr)
            setCursor (session->getLength());
    }

    std::pair<float, float> SessionWorkspace::getLevel (int trackId) const
    {
        if (! isPlaying())
            return { 0.0f, 0.0f };

        return mixer->getLevel (trackId, getPlayheadPosition());
    }

    //==============================================================================
    void SessionWorkspace::toggleRecording()
    {
        if (recording)
            stopRecording();
        else
            startRecording();
    }

    void SessionWorkspace::startRecording()
    {
        if (! canEdit())
            return;

        // The armed track, else the selected audio track, else a new one.
        int target = 0;
        for (const auto& t : session->tracks)
            if (t.armed && t.isAudio())
                target = t.id;

        if (target == 0)
            if (const auto* t = session->findTrack (selectedTrack); t != nullptr && t->isAudio())
                target = selectedTrack;

        if (target == 0)
        {
            auto s = *session;
            target = sessionEdits::addTrack (s, Track::Kind::audio, trackLabel (s, Track::Kind::audio));
            applyEdit (TRANS ("Add Track"), s);
        }

        stop();
        waveform.stop();

        AudioEngine::InputChoice input;
        input.deviceName = context.settings.getString (keys::recordInputDevice);
        input.firstChannel = (int) context.settings.get (keys::recordFirstChannel);
        input.numChannels = juce::jlimit (1, 2, (int) context.settings.get (keys::recordChannels));

        const auto error = context.audio.startRecording (context.blocks(), input);
        if (error.isNotEmpty())
        {
            showError (TRANS ("Recording could not start"), error);
            return;
        }

        recording = true;
        recordTrack = target;
        recordStart = cursor;
        selectedTrack = target;

        // The other tracks play while recording (overdub).
        context.audio.getSessionPlayback().play (cursor);

        Log::info ("Session recording on track " + juce::String (target) + ": " + context.audio.describeDevice());
        setStatus (TRANS ("Recording") + "...");
        sendChangeMessage();
    }

    void SessionWorkspace::stopRecording()
    {
        if (! recording)
            return;

        auto& playback = context.audio.getSessionPlayback();
        const auto sync = playback.getSyncValueAtStart();
        const auto deviceRate = playback.getDeviceSampleRate();

        int latency = 0;
        if (auto* device = context.audio.getDeviceManager().getCurrentAudioDevice())
            latency = device->getInputLatencyInSamples() + device->getOutputLatencyInSamples();

        playback.stop();
        wasPlaying = false;
        const auto dropped = context.audio.getRecorder().getDroppedFrames();
        auto take = context.audio.stopRecording();
        recording = false;

        Log::info ("Recorded " + juce::String (take.getLengthInSeconds(), 1) + " s, sync " + juce::String (sync)
                   + ", latency " + juce::String (latency) + " frames");

        if (dropped > 0)
            showError (TRANS ("Recording"), TRANS ("The computer could not keep up and a little audio was lost. "
                                                   "Close other programs or choose a larger buffer size."));

        if (take.isEmpty() || session == nullptr)
        {
            setStatus (TRANS ("Stopped"));
            sendChangeMessage();
            return;
        }

        // Line the take up with what played: take frame `sync` was captured while
        // playback frame 0 (the cursor) went out, and the sound came back `latency` later.
        juce::int64 skip = 0;
        if (sync >= 0 && deviceRate > 0)
            skip = juce::jmax ((juce::int64) 0, sync + latency);

        const auto position = recordStart;
        if (skip > 0 && skip < take.getNumFrames())
            take = take.getRange (skip, take.getNumFrames() - skip);

        const auto trackId = recordTrack;

        if (! juce::approximatelyEqual (take.getSampleRate(), session->sampleRate))
        {
            const auto rate = session->sampleRate;
            auto converted = std::make_shared<AudioSequence>();
            auto& store = context.blocks();

            runJob (TRANS ("Converting"), [take, rate, converted, &store] (BackgroundJob& j)
            {
                *converted = dsp::resample (take, rate, store, [&j] { return j.shouldContinue(); }, &j.progress);
                return ! converted->isEmpty();
            },
            [this, converted, trackId, position] (bool ok, const juce::String&)
            {
                if (ok)
                    addTake (*converted, trackId, position);
            });
            return;
        }

        addTake (take, trackId, position);
    }

    void SessionWorkspace::addTake (const AudioSequence& take, int trackId, juce::int64 position)
    {
        if (session == nullptr)
            return;

        auto s = *session;
        if (s.findTrack (trackId) == nullptr)
            trackId = sessionEdits::addTrack (s, Track::Kind::audio, trackLabel (s, Track::Kind::audio));

        const auto* track = s.findTrack (trackId);
        auto clip = sessionEdits::makeClip (s, track->name + " " + TRANS ("Take"), take, position);
        const auto id = sessionEdits::addClip (s, trackId, clip);
        selectedClips = { id };
        applyEdit (TRANS ("Record"), s);

        cursor = position + take.getNumFrames();
        setStatus (TRANS ("Recorded") + " " + Workspace::formatTime (take.getNumFrames(), session->sampleRate));
        sendChangeMessage();
    }

    //==============================================================================
    void SessionWorkspace::showExportMixdownDialog()
    {
        if (! canEdit() || session->getLength() == 0)
            return;

        auto folder = juce::File (context.settings.getString (keys::lastSaveFolder));
        if (! folder.isDirectory())
            folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory);

        const auto suggested = folder.getChildFile (getDisplayName() + " " + TRANS ("Mixdown") + ".wav");
        ExportDialog::show (context.settings, TRANS ("Export Multitrack Mixdown"), suggested, session->sampleRate, nullptr,
                            [this] (const ExportSettings& settings)
        {
            context.settings.set (keys::lastSaveFolder, settings.file.getParentDirectory().getFullPathName());
            exportMixdown (settings);
        });
    }

    void SessionWorkspace::exportMixdown (const ExportSettings& settings)
    {
        if (! canEdit())
            return;

        auto snapshot = std::make_shared<Session> (*session);
        const auto range = timeSelection.isEmpty() ? FrameRange (0, session->getLength()) : timeSelection;
        auto& store = context.blocks();
        Log::info ("Exporting mixdown to " + settings.file.getFullPathName());

        runJob (TRANS ("Mixing down"), [snapshot, range, settings, &store] (BackgroundJob& j)
        {
            dsp::WorkControl control { [&j] { return j.shouldContinue(); }, &j.progress, 0.0, 0.7 };
            const auto mix = SessionMixer::mixdown (*snapshot, range, store, control);
            if (mix.getNumChannels() == 0)
                return false;

            if (! FileSampleSource::relocateAllReading (settings.file, [&store, &settings]
                                                        { return store.createFileName (settings.file.getFileExtension()); }))
            {
                j.error = TRANS ("Could not make a temporary copy of the original file. Is the disk full?");
                return false;
            }

            std::atomic<double> writeProgress { 0.0 };
            const auto result = audioExport::write (mix, settings, [&j] { return j.shouldContinue(); }, &writeProgress, j.error);
            j.progress = 1.0;
            return result == audioExport::Result::ok;
        },
        [this, settings] (bool ok, const juce::String& error)
        {
            if (! ok)
            {
                if (error.isNotEmpty())
                    showError (TRANS ("Could not export the mixdown"), settings.file.getFullPathName() + "\n\n" + error);
                return;
            }

            Log::info ("Mixdown exported");
            setStatus (TRANS ("Exported") + " " + settings.file.getFileName());
        });
    }

    void SessionWorkspace::mixdownToNewFile()
    {
        if (! canEdit() || session->getLength() == 0)
            return;

        auto snapshot = std::make_shared<Session> (*session);
        const auto range = timeSelection.isEmpty() ? FrameRange (0, session->getLength()) : timeSelection;
        auto result = std::make_shared<AudioSequence>();
        auto& store = context.blocks();
        const auto name = getDisplayName() + " " + TRANS ("Mixdown");

        runJob (TRANS ("Mixing down"), [snapshot, range, result, &store] (BackgroundJob& j)
        {
            *result = SessionMixer::mixdown (*snapshot, range, store, { [&j] { return j.shouldContinue(); }, &j.progress });
            return result->getNumChannels() > 0;
        },
        [this, result, name] (bool ok, const juce::String&)
        {
            if (! ok)
                return;

            setActive (false);
            waveform.openRendered (name, *result);
        });
    }

    //==============================================================================
    juce::int64 SessionWorkspace::getTimelineLength() const
    {
        const auto length = session != nullptr ? session->getLength() : 0;
        const auto rate = getSampleRate();
        return juce::jmax ((juce::int64) (rate * 60.0), length + (juce::int64) (rate * 30.0));
    }

    void SessionWorkspace::setView (View newView)
    {
        const auto maxFpp = (double) getTimelineLength() / (double) juce::jmax (1, viewWidth / 2);
        newView.framesPerPixel = juce::jlimit (0.5, juce::jmax (1.0, maxFpp), newView.framesPerPixel);
        newView.startFrame = juce::jlimit (0.0, juce::jmax (0.0, (double) getTimelineLength() - newView.framesPerPixel * viewWidth * 0.5),
                                           newView.startFrame);
        newView.scrollY = juce::jmax (0, newView.scrollY);

        if (! juce::exactlyEqual (newView.startFrame, view.startFrame) || ! juce::exactlyEqual (newView.framesPerPixel, view.framesPerPixel)
            || newView.scrollY != view.scrollY)
        {
            view = newView;
            sendChangeMessage();
        }
    }

    double SessionWorkspace::zoomAnchor() const
    {
        const auto x = frameToX (getPlayheadPosition());
        return x >= 0 && x <= viewWidth ? x : viewWidth * 0.5;
    }

    void SessionWorkspace::zoomBy (double factor, double anchorX)
    {
        auto v = view;
        const auto anchorFrame = v.startFrame + anchorX * v.framesPerPixel;
        v.framesPerPixel /= factor;
        v.framesPerPixel = juce::jmax (0.5, v.framesPerPixel);
        v.startFrame = anchorFrame - anchorX * v.framesPerPixel;
        setView (v);
    }

    void SessionWorkspace::zoomToFit()
    {
        auto v = view;
        const auto length = session != nullptr && session->getLength() > 0 ? session->getLength()
                                                                            : (juce::int64) (getSampleRate() * 60.0);
        v.startFrame = 0.0;
        v.framesPerPixel = (double) length * 1.05 / (double) juce::jmax (1, viewWidth);
        setView (v);
    }

    //==============================================================================
    void SessionWorkspace::timerCallback()
    {
        auto& playback = context.audio.getSessionPlayback();

        if (! recording && (playback.takeFinishedFlag() || (wasPlaying && ! playback.isPlaying())))
            stop();

        if (recording && context.audio.getRecorder().takeDeviceStoppedFlag())
        {
            stopRecording();
            showError (TRANS ("Recording"), TRANS ("The audio device stopped. The recording so far has been kept."));
        }

        if (job != nullptr)
            setStatus (getBusyText());

        if (isPlaying() && active)
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
    }
}
