// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "app/AppContext.h"
#include "app/document/AudioDocument.h"
#include "app/document/BackgroundJob.h"
#include "core/edit/EditOperations.h"
#include "formats/export/AudioExporter.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** The open file and what the user is doing with it: edit cursor, selection,
        markers, playback, recording, saving and the visible part of the waveform.

        Commands change the workspace; the editor page draws it. Components
        listen with addChangeListener() and are told when the file, the loading
        state, the audio, the selection, the playback state or the view changes.
        Positions that move all the time (playhead, loading progress, recording)
        are polled by the components. Message thread only.

        Every change to the audio or the markers goes through applyEdit(), which
        records it in the undo history (CLAUDE.md, rule 6).
    */
    class Workspace : public juce::ChangeBroadcaster,
                      private juce::Timer
    {
    public:
        explicit Workspace (AppContext& context);
        ~Workspace() override;

        //==============================================================================
        /** Shows the Open dialog (after asking to save unsaved changes). */
        void showOpenDialog();

        /** Opens a file, replacing the current one (after asking to save unsaved
            changes). Returns false and shows a message if it cannot be opened. */
        bool openFile (const juce::File& file);

        /** Closes the file, after asking to save unsaved changes. */
        void closeFile();

        /** If there are unsaved changes, asks whether to save them first. Calls
            `proceed` unless the user cancels (or saving fails). Stops a recording. */
        void askToSaveChanges (std::function<void()> proceed);

        AudioDocument* getDocument() const noexcept { return document.get(); }
        bool hasDocument() const noexcept           { return document != nullptr; }
        bool isReady() const noexcept               { return document != nullptr && document->isReady(); }

        /** True while nothing stops an edit: a file is loaded, nothing is being
            saved or converted and nothing is recording. */
        bool canEdit() const noexcept               { return isReady() && ! isBusy() && ! isRecording(); }

        //==============================================================================
        /** Saves to the document's file (or asks for one). `afterSaved` runs once it has been saved. */
        void save (std::function<void()> afterSaved = nullptr);
        void showSaveAsDialog (std::function<void()> afterSaved = nullptr);
        void showExportDialog (bool selectionOnly);

        /** Exports the whole file without asking (smoke test, later macros). */
        void exportFile (const ExportSettings& settings);

        /** True while a background job (saving, converting, restoring) runs. */
        bool isBusy() const noexcept                { return job != nullptr; }

        /** "Saving... 42%", or empty. */
        juce::String getBusyText() const;

        //==============================================================================
        void togglePlayback();
        void play();
        void stop();
        bool isPlaying() const;

        /** Where playback starts (the edit cursor), in frames. */
        juce::int64 getCursor() const noexcept      { return cursor; }

        /** Moves the edit cursor and clears the selection. While playing, playback jumps there. */
        void setCursor (juce::int64 frame);

        /** The frame being heard while playing, otherwise the cursor. */
        juce::int64 getPlayheadPosition() const;

        void goToStart();
        void goToEnd();

        //==============================================================================
        FrameRange getSelection() const noexcept    { return selection; }
        bool hasSelection() const noexcept          { return ! selection.isEmpty(); }

        /** Selects a range (the cursor moves to its start). */
        void setSelection (FrameRange range);
        void selectAll();
        void selectNone();

        //==============================================================================
        void cut();
        void copy();
        void paste();
        void mixPaste();
        void deleteSelection();
        void trimToSelection();
        void silenceSelection();
        void showInsertSilenceDialog();
        void fadeIn();
        void fadeOut();

        bool hasClipboard() const noexcept          { return ! clipboard.isEmpty(); }

        void undo();
        void redo();

        //==============================================================================
        /** Adds a marker at the playhead (works while playing). */
        void addMarker();
        void showRenameMarkerDialog();
        void deleteMarker();
        void goToNextMarker();
        void goToPreviousMarker();

        /** The marker the user clicked or jumped to last, or -1. */
        int getSelectedMarker() const noexcept      { return selectedMarker; }
        void selectMarker (int index);

        /** Index of the marker the marker commands act on: the selected one, or one at the cursor. */
        int getCurrentMarker() const;

        //==============================================================================
        void toggleRecording();
        bool isRecording() const noexcept           { return recording; }

        /** Where the take that is being recorded goes in the document. */
        FrameRange getRecordingRange() const noexcept { return recordRange; }

        //==============================================================================
        /** Asks whether to restore the work of a run that crashed, found in `runFolder`. */
        void offerRecovery (const juce::File& runFolder);

        //==============================================================================
        /** The visible part of the waveform: first frame on the left and zoom. */
        struct View
        {
            double startFrame = 0.0;
            double framesPerPixel = 1024.0;
        };

        const View& getView() const noexcept { return view; }

        /** Sets the view, keeping it inside the file and inside sensible zoom limits. */
        void setView (View newView);

        /** The editor tells us how wide the waveform is, so zoom commands work. */
        void setViewWidth (int pixels);
        int getViewWidth() const noexcept { return viewWidth; }

        /** Zooms by `factor` (> 1 = closer) keeping `anchorX` (pixels) in place. */
        void zoomBy (double factor, double anchorX);
        void zoomIn()   { zoomBy (2.0, zoomAnchor()); }
        void zoomOut()  { zoomBy (0.5, zoomAnchor()); }
        void zoomToFit();
        void zoomToSelection();

        /** Smallest zoom: this many pixels per frame, so single samples are visible. */
        static constexpr double minFramesPerPixel = 1.0 / 64.0;

        /** Called with a status bar message. */
        std::function<void (const juce::String&)> onStatusMessage;

        /** Formats a frame position as h:mm:ss.mmm. */
        static juce::String formatTime (juce::int64 frame, double sampleRate);

    private:
        class EditAction;

        void timerCallback() override;
        void loadFinished();
        double zoomAnchor() const;
        double getMaxFramesPerPixel() const;
        void setStatus (const juce::String& message);
        void showError (const juce::String& title, const juce::String& message);

        void openFileNow (const juce::File& file);
        void closeFileNow();
        void setDocument (std::unique_ptr<AudioDocument>);

        /** Records an edit in the undo history and applies it. */
        void applyEdit (const juce::String& name, const edits::Result& result);
        void applyState (const AudioState& state, FrameRange newSelection, juce::int64 revision);
        FrameRange getEditRange() const;

        void startRecording();
        void stopRecording();
        void insertTake (const AudioSequence& take);

        void saveAs (const ExportSettings& settings, const AudioSequence& audio, bool becomesDocumentFile,
                     std::function<void()> afterSaved);
        void runJob (const juce::String& title, BackgroundJob::Work work, BackgroundJob::Done done);
        void writeJournal();

        AppContext& context;
        std::unique_ptr<AudioDocument> document;
        std::unique_ptr<juce::FileChooser> chooser;
        std::unique_ptr<BackgroundJob> job;
        juce::String jobError; ///< error of the job that finished last
        juce::int64 cursor = 0;
        FrameRange selection;
        int selectedMarker = -1;
        AudioSequence clipboard;
        juce::int64 nextRevision = 1;
        View view;
        int viewWidth = 1000;
        bool wasPlaying = false;

        bool recording = false;
        FrameRange recordRange;
        int newRecordingCount = 0;

        bool journalDirty = false;
        juce::uint32 lastJournalWrite = 0;

        JUCE_DECLARE_WEAK_REFERENCEABLE (Workspace)
        JUCE_DECLARE_NON_COPYABLE (Workspace)
    };
}
