// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "app/Workspace.h"
#include "app/effects/ParameterPanel.h"
#include "services/presets/PresetStore.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** The window of one effect: presets, its controls, live preview, and
        Apply (or Insert, for generators). Remembers the last settings of each
        effect while the program runs. */
    class EffectDialog : public juce::Component,
                         private juce::ChangeListener
    {
    public:
        static void show (Workspace& workspace, PresetStore& presets, const dsp::EffectType& type, juce::Component* parent);

        EffectDialog (Workspace& workspace, PresetStore& presets, const dsp::EffectType& type);
        ~EffectDialog() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void showPresetMenu();
        void applyPreset (const std::vector<float>& presetValues, const juce::String& name);
        void savePreset();
        void apply();
        void closeDialog();
        void updateButtons();
        juce::String describeTarget() const;

        Workspace& workspace;
        PresetStore& presets;
        const dsp::EffectType& type;
        dsp::ParameterValuesPtr values;
        ParameterPanel panel;
        juce::Viewport viewport;
        juce::TextButton presetButton, previewButton, addToRackButton, resetButton, cancelButton, applyButton;
        juce::String presetName;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectDialog)
    };
}
