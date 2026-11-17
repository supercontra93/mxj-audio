// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "app/Workspace.h"
#include "app/effects/ParameterPanel.h"
#include "services/presets/PresetStore.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** The effect rack panel on the left of the editor: the list of effects
        (switch, name, move, remove), the controls of the selected one, "Listen
        live", rack presets and "Apply to selection". */
    class RackPanel : public juce::Component,
                      private juce::ChangeListener
    {
    public:
        RackPanel (Workspace& workspace, PresetStore& presets);
        ~RackPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        /** Preferred width (grows with the text size). */
        static int preferredWidth();

    private:
        class SlotRow;

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void rebuild();
        void showAddMenu();
        void showPresetMenu();
        void updateButtons();

        Workspace& workspace;
        EffectRack& rack;
        PresetStore& presets;

        juce::TextButton listenButton, addButton, presetButton, applyButton;
        juce::OwnedArray<SlotRow> rows;
        std::unique_ptr<ParameterPanel> parameters;
        juce::Viewport parameterView;
        int shownSlot = -1;
        const void* shownValues = nullptr;
        int shownSize = -1;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackPanel)
    };
}
