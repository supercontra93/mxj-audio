// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "app/AppContext.h"
#include "app/Workspace.h"
#include "app/document/BackgroundJob.h"
#include "app/session/ProjectFile.h"
#include "core/session/SessionEdits.h"
#include "engine/mix/SessionMixer.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** The multitrack session (phase 5) and what the user does with it:
        tracks, clips, buses, the mixer, automation, playback, recording,
        saving the project and the mixdown.

        Like the Workspace of the waveform editor: commands change it, the
        session page draws it and listens for change messages. Every change to
        the session is an undo step in the session's own history (CLAUDE.md,
        rule 6). Changes made by dragging (clips, fades, faders, automation
        points) are shown and heard while the mouse moves and recorded as one
        step when it is released: beginGesture(), updateGesture(), endGesture().
        Message thread only.
    */
    class SessionWorkspace : public juce::ChangeBroadcaster,
                             private juce::Timer
    {
    public:
        SessionWorkspace (AppContext& context, Workspace& waveformWorkspace);
        ~SessionWorkspace() override;

        //==============================================================================
        /** True while the multitrack view is shown (instead of the waveform editor). */
        bool isActive() const noexcept              { return active; }
        void setActive (bool shouldBeActive);

        bool hasSession() const noexcept            { return session != nullptr; }
        const Session& getSession() const           { return *session; }
        double getSampleRate() const noexcept       { return session != nullptr ? session->sampleRate : 48000.0; }

        juce::File getFile() const                  { return file; }
        juce::String getDisplayName() const;
        bool isModified() const noexcept            { return session != nullptr && revision != savedRevision; }

        /** True while nothing stops an edit (no saving, loading, mixdown or recording). */
        bool canEdit() const noexcept               { return hasSession() && ! isBusy() && ! recording; }

        //==============================================================================
        void showNewSessionDialog();
        void newSession (double sampleRate);

        /** Opens a .mxj project (after asking to save the open session). */
        void openProject (const juce::File& projectFile);
        void showSaveAsDialog (std::function<void()> afterSaved = nullptr);
        void save (std::function<void()> afterSaved = nullptr);

        /** Saves without asking (smoke test). */
        void saveAs (const juce::File& projectFile, std::function<void()> afterSaved = nullptr);
        void closeSession();

        /** If the session has unsaved changes, asks whether to save them first. */
        void askToSaveChanges (std::function<void()> proceed);

        bool isBusy() const noexcept                { return job != nullptr; }
        juce::String getBusyText() const;

        //==============================================================================
        /** Multitrack > Import Files: audio files become clips at the cursor,
            on the selected track or new tracks below it. */
        void showImportDialog();
        void importFiles (const juce::Array<juce::File>& files, int trackId, juce::int64 position);

        void addTrack();
        void addBus();
        void removeSelectedTrack();
        void moveSelectedTrack (int delta);

        int getSelectedTrack() const noexcept       { return selectedTrack; }
        void selectTrack (int trackId);

        //==============================================================================
        // Mixer controls. `commit` false = live while dragging, true = undo step.
        void setTrackName (int trackId, const juce::String& name);
        void setTrackVolume (int trackId, float db, bool commit);
        void setTrackPan (int trackId, float pan, bool commit);
        void setSendLevel (int trackId, float db, bool commit);
        void setTrackOutput (int trackId, int busId);
        void setSendBus (int trackId, int busId);   ///< 0 = no send
        void toggleMute (int trackId);
        void toggleSolo (int trackId);
        void toggleArm (int trackId);
        void toggleAutomation (int trackId);
        void clearAutomation (int trackId);
        void setTrackEffects (int trackId, const juce::var& effects, bool commit);

        //==============================================================================
        // Clips
        const std::vector<int>& getSelectedClips() const noexcept { return selectedClips; }
        bool isClipSelected (int clipId) const;
        void selectClip (int clipId, bool addToSelection);
        void selectNone();

        void splitAtCursor();
        void deleteSelection (bool ripple);
        void copyClips();
        void pasteClips();
        void toggleClipMute();
        void showClipGainDialog();
        void showRenameClipDialog();

        bool hasClipboard() const noexcept          { return ! clipboard.empty(); }

        //==============================================================================
        /** Drag edits: the session changes while the mouse moves, one undo step at the end. */
        void beginGesture();
        void updateGesture (const Session& changed);
        void endGesture (const juce::String& undoName);
        bool isInGesture() const noexcept           { return gestureStart != nullptr; }

        /** Records any change as one undo step. */
        void applyEdit (const juce::String& name, const Session& changed);

        bool canUndo() const;
        bool canRedo() const;
        void undo();
        void redo();

        //==============================================================================
        juce::int64 getCursor() const noexcept      { return cursor; }
        void setCursor (juce::int64 frame);
        FrameRange getTimeSelection() const noexcept { return timeSelection; }
        void setTimeSelection (FrameRange range);
        bool hasSelection() const noexcept          { return ! timeSelection.isEmpty() || ! selectedClips.empty(); }

        bool isSnapping() const noexcept            { return snapping; }
        void toggleSnapping();

        /** Snaps a frame to clip edges, markers and the cursor (within a few pixels). */
        juce::int64 snap (juce::int64 frame, const std::vector<int>& ignoreClips = {}) const;

        //==============================================================================
        void togglePlayback();
        void play();
        void stop();
        bool isPlaying() const;
        juce::int64 getPlayheadPosition() const;
        void goToStart();
        void goToEnd();

        void toggleRecording();
        bool isRecording() const noexcept           { return recording; }
        juce::int64 getRecordingStart() const noexcept { return recordStart; }
        int getRecordingTrack() const noexcept      { return recordTrack; }

        /** Levels (gain) heard now on a track, bus or the master (0). */
        std::pair<float, float> getLevel (int trackId) const;

        //==============================================================================
        /** File > Export > Multitrack Mixdown: the whole session (or the time
            selection) as an audio file. */
        void showExportMixdownDialog();
        void exportMixdown (const ExportSettings& settings);

        /** Multitrack > Mixdown to New File: opens the mix in the waveform editor. */
        void mixdownToNewFile();

        //==============================================================================
        struct View
        {
            double startFrame = 0.0;
            double framesPerPixel = 2048.0;
            int scrollY = 0;
        };

        const View& getView() const noexcept        { return view; }
        void setView (View newView);
        void setViewWidth (int pixels)              { viewWidth = juce::jmax (1, pixels); }
        void zoomBy (double factor, double anchorX);
        void zoomIn()                               { zoomBy (2.0, zoomAnchor()); }
        void zoomOut()                              { zoomBy (0.5, zoomAnchor()); }
        void zoomToFit();

        double frameToX (juce::int64 frame) const   { return ((double) frame - view.startFrame) / view.framesPerPixel; }
        juce::int64 xToFrame (double x) const       { return juce::jmax ((juce::int64) 0, (juce::int64) std::llround (view.startFrame + x * view.framesPerPixel)); }

        /** Visible length of the time line in frames (session plus room to work). */
        juce::int64 getTimelineLength() const;

        SessionMixer& getMixer() noexcept           { return *mixer; }

        /** Called with a status bar message. */
        std::function<void (const juce::String&)> onStatusMessage;

    private:
        class EditAction;

        void timerCallback() override;
        void setStatus (const juce::String&);
        void showError (const juce::String& title, const juce::String& message);
        void setSessionNow (std::unique_ptr<Session> newSession, const juce::File& newFile);
        void applyState (const Session& state, juce::int64 newRevision);
        void sessionChanged();
        using JobDone = std::function<void (bool succeeded, const juce::String& error)>;
        void runJob (const juce::String& title, BackgroundJob::Work work, JobDone done);
        double zoomAnchor() const;
        void startRecording();
        void stopRecording();
        void addTake (const AudioSequence& take, int trackId, juce::int64 position);
        std::vector<int> targetTracks() const;

        AppContext& context;
        Workspace& waveform;
        History history;
        std::unique_ptr<Session> session;
        std::unique_ptr<Session> gestureStart;
        bool gestureChanged = false;
        std::shared_ptr<SessionMixer> mixer;
        juce::File file;
        projectFile::CopiedFiles copiedFiles;
        juce::int64 revision = 0, savedRevision = 0, nextRevision = 1;
        int untitledCount = 0;

        bool active = false;
        std::unique_ptr<BackgroundJob> job;
        std::unique_ptr<juce::FileChooser> chooser;

        int selectedTrack = 0;
        std::vector<int> selectedClips;
        std::vector<Clip> clipboard; ///< positions relative to the first clip
        juce::int64 cursor = 0;
        FrameRange timeSelection;
        bool snapping = true;
        View view;
        int viewWidth = 1000;
        bool wasPlaying = false;

        bool recording = false;
        juce::int64 recordStart = 0;
        int recordTrack = 0;

        JUCE_DECLARE_WEAK_REFERENCEABLE (SessionWorkspace)
        JUCE_DECLARE_NON_COPYABLE (SessionWorkspace)
    };
}
