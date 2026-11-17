// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "app/AppContext.h"
#include "app/document/AudioDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** The open file and what the user is doing with it: edit cursor, playback
        and the visible part of the waveform.

        Commands change the workspace; the editor page draws it. Components
        listen with addChangeListener() and are told when the file, the loading
        state, the playback state or the view changes. Positions that move all
        the time (playhead, loading progress) are polled by the components.
        Message thread only.
    */
    class Workspace : public juce::ChangeBroadcaster,
                      private juce::Timer
    {
    public:
        explicit Workspace (AppContext& context);
        ~Workspace() override;

        //==============================================================================
        /** Shows the Open dialog. */
        void showOpenDialog();

        /** Opens a file, replacing the current one. Returns false and shows a
            message if it cannot be opened. */
        bool openFile (const juce::File& file);

        void closeFile();

        AudioDocument* getDocument() const noexcept { return document.get(); }
        bool hasDocument() const noexcept           { return document != nullptr; }
        bool isReady() const noexcept               { return document != nullptr && document->isReady(); }

        //==============================================================================
        void togglePlayback();
        void play();
        void stop();
        bool isPlaying() const;

        /** Where playback starts (the edit cursor), in frames. */
        juce::int64 getCursor() const noexcept      { return cursor; }

        /** Moves the edit cursor. While playing, playback jumps there. */
        void setCursor (juce::int64 frame);

        /** The frame being heard while playing, otherwise the cursor. */
        juce::int64 getPlayheadPosition() const;

        void goToStart();
        void goToEnd();

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

        /** Smallest zoom: this many pixels per frame, so single samples are visible. */
        static constexpr double minFramesPerPixel = 1.0 / 64.0;

        /** Called with a status bar message. */
        std::function<void (const juce::String&)> onStatusMessage;

        /** Formats a frame position as h:mm:ss.mmm. */
        static juce::String formatTime (juce::int64 frame, double sampleRate);

    private:
        void timerCallback() override;
        void loadFinished();
        double zoomAnchor() const;
        double getMaxFramesPerPixel() const;
        void setStatus (const juce::String& message);

        AppContext& context;
        std::unique_ptr<AudioDocument> document;
        std::unique_ptr<juce::FileChooser> chooser;
        juce::int64 cursor = 0;
        View view;
        int viewWidth = 1000;
        bool wasPlaying = false;

        JUCE_DECLARE_NON_COPYABLE (Workspace)
    };
}
