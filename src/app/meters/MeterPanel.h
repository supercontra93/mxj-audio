// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "dsp/analysis/LevelMeter.h"
#include "dsp/effects/Parameters.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** Level meters on the right of the editor (master plan, module G): peak
        and RMS bars for left and right with peak hold and clip lights, true
        peak, phase correlation and loudness (momentary, short-term,
        integrated, loudness range). Shows what is heard, after the effects.
        Click the panel to reset the holds and the loudness measurement. */
    class MeterPanel : public juce::Component,
                       public juce::SettableTooltipClient,
                       private juce::Timer
    {
    public:
        explicit MeterPanel (dsp::LevelMeter& meter);

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

        static int preferredWidth();

    private:
        void timerCallback() override;

        struct Channel
        {
            float peakDb = -100.0f;     ///< falling bar
            float rmsDb = -100.0f;
            float holdDb = -100.0f;     ///< peak hold line
            int holdFrames = 0;
            bool clipped = false;
        };

        dsp::LevelMeter& meter;
        std::array<Channel, 2> channels;
        float truePeakDb = -100.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterPanel)
    };
}
