// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "engine/recording/Recorder.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** Shown over the waveform while recording: elapsed time, input level per
        channel (with a red warning when it clips) and the take as it grows. */
    class RecordingPanel : public juce::Component,
                           private juce::Timer
    {
    public:
        explicit RecordingPanel (Recorder& recorder);

        void paint (juce::Graphics&) override;
        void visibilityChanged() override;

    private:
        void timerCallback() override;
        void paintMeter (juce::Graphics&, juce::Rectangle<float> area, int channel);

        Recorder& recorder;
        float levels[2] {};       ///< shown level, falls slowly
        float holds[2] {};        ///< peak hold
        int holdTicks[2] {};
        bool clipped[2] {};

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecordingPanel)
    };
}
