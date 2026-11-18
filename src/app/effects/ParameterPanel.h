// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "dsp/effects/Effect.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** The controls of one effect, drawn automatically from its parameter list
        (master plan, section 8): a slider for numbers, a list for choices, a
        switch for on/off values. VST3 and CLAP plugins get a button that opens
        their own window. Above them a frequency response curve for
        equalisers and filters, below them a gain reduction meter for dynamics.

        Moving a control writes the shared ParameterValues at once, so the sound
        changes while it plays. Double-click a slider to reset it.
    */
    class ParameterPanel : public juce::Component,
                           private juce::Timer
    {
    public:
        ParameterPanel (const dsp::EffectType& type, dsp::ParameterValuesPtr values, double sampleRate);
        ~ParameterPanel() override;

        /** Reads the values again (after a preset was chosen). */
        void refreshFromValues();

        /** Height that shows every control. */
        int getIdealHeight() const;

        /** Called after the user changed a value. */
        std::function<void()> onChange;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        class ResponseCurve;
        class ReductionMeter;

        struct Row
        {
            const dsp::ParameterInfo* info = nullptr;
            int index = 0;
            std::unique_ptr<juce::Slider> slider;
            std::unique_ptr<juce::ComboBox> choice;
            std::unique_ptr<juce::ToggleButton> toggle;
        };

        void timerCallback() override;
        void valueChanged (int index, float value);

        const dsp::EffectType& type;
        dsp::ParameterValuesPtr values;
        std::vector<Row> rows;
        std::unique_ptr<ResponseCurve> curve;
        std::unique_ptr<ReductionMeter> reduction;

        // VST3 and CLAP plugins: their controls are in their own window.
        std::unique_ptr<juce::TextButton> windowButton;
        bool pluginWasEdited = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterPanel)
    };
}
