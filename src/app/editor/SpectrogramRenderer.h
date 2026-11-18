// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/audio/AudioSequence.h"
#include "dsp/spectral/Stft.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** Draws spectrogram pictures of the visible part of the file on a
        background thread, so the window never waits for the FFTs (phase 4).

        The editor asks for a picture of the current view; while it is being
        made, the last picture is stretched to fit, and the new one shows up a
        moment later (in parts while it is drawn). */
    class SpectrogramRenderer : private juce::Thread,
                                private juce::AsyncUpdater
    {
    public:
        struct Request
        {
            AudioSequence audio;
            double startFrame = 0.0, framesPerPixel = 1.0;
            int width = 0, height = 0;   ///< pixels of one channel lane
            int fftSize = 2048;
            dsp::FrequencyScale::Kind scale = dsp::FrequencyScale::Kind::logarithmic;
            juce::String colours = "amber";

            bool isSameAs (const Request& other) const;
        };

        struct Picture
        {
            Request request;
            std::vector<juce::Image> channels;
            int columnsDone = 0; ///< columns drawn so far (request.width when complete)

            bool isComplete() const noexcept { return columnsDone >= request.width; }
        };

        SpectrogramRenderer();
        ~SpectrogramRenderer() override;

        /** Asks for a new picture (does nothing if it is the one being made or shown). Message thread. */
        void request (const Request&);

        /** The newest picture (maybe still being drawn), or nullptr. Message thread. */
        const Picture* getPicture() const noexcept { return picture.channels.empty() ? nullptr : &picture; }

        /** The newest complete picture, or nullptr. Message thread. */
        const Picture* getCompletePicture() const noexcept { return complete.channels.empty() ? nullptr : &complete; }

        /** Drops the picture and stops working (no file, or another file). */
        void clear();

        /** Called on the message thread when a new (or more complete) picture is ready. */
        std::function<void()> onPictureChanged;

        /** The colour for a level from 0 (quiet) to 1 (loud), for a colour map name. */
        static juce::Colour colourFor (const juce::String& colours, float level);

    private:
        void run() override;
        void handleAsyncUpdate() override;
        bool render (const Request&);

        juce::CriticalSection lock;
        Request wanted;          ///< guarded by lock
        bool hasWanted = false;  ///< guarded by lock
        std::atomic<int> generation { 0 };
        Picture ready;           ///< guarded by lock: made by the thread, not yet taken
        bool hasReady = false;   ///< guarded by lock

        Request lastRequested;   ///< message thread
        bool requestedAny = false;
        Picture picture;         ///< message thread
        Picture complete;        ///< message thread

        JUCE_DECLARE_NON_COPYABLE (SpectrogramRenderer)
    };
}
