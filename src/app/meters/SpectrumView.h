// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "dsp/analysis/Spectrum.h"
#include "engine/playback/PlaybackEngine.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** Spectrum analyser under the waveform (master plan, module G): the
        frequencies of what is heard right now, 20 Hz to 20 kHz on a log scale,
        with a slowly falling peak line. */
    class SpectrumView : public juce::Component,
                         private juce::Timer
    {
    public:
        explicit SpectrumView (PlaybackEngine& playback);

        void paint (juce::Graphics&) override;

        static int preferredHeight();

    private:
        void timerCallback() override;

        PlaybackEngine& playback;
        dsp::Spectrum spectrum { 12 };
        std::vector<float> history, scratch, magnitudes, smoothed, peaks;
        bool active = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumView)
    };
}
