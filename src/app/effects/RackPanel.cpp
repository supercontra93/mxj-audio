// SPDX-License-Identifier: AGPL-3.0-only
#include "app/effects/RackPanel.h"
#include "app/theme/Fonts.h"
#include "app/theme/Palette.h"
#include "app/ui/Dialogs.h"

namespace mxj
{
    namespace
    {
        constexpr int padding = 10;
        int rowHeight()    { return fonts::scaled (28); }
        int buttonHeight() { return fonts::scaled (26); }
        int titleHeight()  { return fonts::scaled (30); }
    }

    int RackPanel::preferredWidth()
    {
        return fonts::scaled (330);
    }

    //==============================================================================
    /** One effect in the rack: on/off switch, name (click to show its controls),
        move up, move down, remove. */
    class RackPanel::SlotRow : public juce::Component
    {
    public:
        SlotRow (EffectRack& r, int i) : rack (r), index (i)
        {
            enabled.setToggleState (rack[index].enabled->load(), juce::dontSendNotification);
            enabled.setTooltip (TRANS ("Switch this effect on or off"));
            enabled.onClick = [this] { rack.setEnabled (index, enabled.getToggleState()); };
            addAndMakeVisible (enabled);

            for (auto* b : { &up, &down, &remove })
            {
                b->setWantsKeyboardFocus (false);
                addAndMakeVisible (b);
            }

            up.setButtonText (juce::String::charToString (0x25b2));
            up.setTooltip (TRANS ("Move up"));
            up.onClick = [this] { rack.move (index, index - 1); };
            up.setEnabled (index > 0);

            down.setButtonText (juce::String::charToString (0x25bc));
            down.setTooltip (TRANS ("Move down"));
            down.onClick = [this] { rack.move (index, index + 1); };
            down.setEnabled (index < rack.size() - 1);

            remove.setButtonText (juce::String::charToString (0x2715));
            remove.setTooltip (TRANS ("Remove from the rack"));
            remove.onClick = [this] { rack.remove (index); };
        }

        void paint (juce::Graphics& g) override
        {
            const bool isSelected = rack.getSelectedSlot() == index;
            auto area = getLocalBounds();
            g.setColour (isSelected ? palette::panelRaised : palette::panel);
            g.fillRoundedRectangle (area.toFloat(), palette::cornerRadius);

            if (isSelected)
            {
                g.setColour (palette::accent);
                g.fillRect (area.removeFromLeft (3));
            }

            g.setColour (rack[index].enabled->load() ? palette::text : palette::textDim);
            g.setFont (fonts::sans (13.0f));
            g.drawFittedText (juce::String (index + 1) + ".  " + TRANS (rack[index].type->name), nameArea, juce::Justification::centredLeft, 1);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (4, 2);
            enabled.setBounds (area.removeFromLeft (area.getHeight() + 4));
            const auto b = area.getHeight();
            remove.setBounds (area.removeFromRight (b));
            area.removeFromRight (2);
            down.setBounds (area.removeFromRight (b));
            area.removeFromRight (2);
            up.setBounds (area.removeFromRight (b));
            nameArea = area.withTrimmedLeft (2);
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            rack.setSelectedSlot (index);
        }

    private:
        EffectRack& rack;
        const int index;
        juce::ToggleButton enabled;
        juce::TextButton up, down, remove;
        juce::Rectangle<int> nameArea;
    };

    //==============================================================================
    RackPanel::RackPanel (Workspace& w, PresetStore& p) : workspace (w), rack (w.getRack()), presets (p)
    {
        listenButton.setButtonText (TRANS ("Listen Live"));
        listenButton.setClickingTogglesState (true);
        listenButton.setTooltip (TRANS ("Hear the rack while the file plays. The file itself stays unchanged until you apply the rack."));
        listenButton.onClick = [this] { rack.setMonitoring (listenButton.getToggleState()); };
        addAndMakeVisible (listenButton);

        addButton.setButtonText ("+ " + TRANS ("Add Effect"));
        addButton.onClick = [this] { showAddMenu(); };
        addAndMakeVisible (addButton);

        presetButton.setButtonText (TRANS ("Presets") + "...");
        presetButton.onClick = [this] { showPresetMenu(); };
        addAndMakeVisible (presetButton);

        applyButton.setButtonText (TRANS ("Apply Rack"));
        applyButton.setTooltip (TRANS ("Apply the switched-on effects to the selection, or to the whole file"));
        applyButton.setColour (juce::TextButton::buttonColourId, palette::accent);
        applyButton.setColour (juce::TextButton::textColourOffId, palette::onAccent);
        applyButton.onClick = [this] { workspace.applyRack(); };
        addAndMakeVisible (applyButton);

        parameterView.setScrollBarsShown (true, false);
        addAndMakeVisible (parameterView);

        rack.addChangeListener (this);
        workspace.addChangeListener (this);
        rebuild();
    }

    RackPanel::~RackPanel()
    {
        rack.removeChangeListener (this);
        workspace.removeChangeListener (this);
    }

    void RackPanel::changeListenerCallback (juce::ChangeBroadcaster* source)
    {
        if (source == &rack)
            rebuild();
        else
            updateButtons();
    }

    void RackPanel::updateButtons()
    {
        applyButton.setEnabled (workspace.canEdit() && rack.hasActiveEffects());
        listenButton.setToggleState (rack.isMonitoring(), juce::dontSendNotification);
        listenButton.setColour (juce::TextButton::buttonOnColourId, palette::playhead);
    }

    void RackPanel::rebuild()
    {
        rows.clear();
        for (int i = 0; i < rack.size(); ++i)
            addAndMakeVisible (rows.add (new SlotRow (rack, i)));

        // Keep the controls if the same effect stays selected (dragging a slider must not rebuild them).
        const auto selected = rack.getSelectedSlot();
        const void* values = selected >= 0 ? rack[selected].values.get() : nullptr;

        if (values != shownValues)
        {
            parameterView.setViewedComponent (nullptr, false);
            parameters = nullptr;

            if (selected >= 0)
            {
                const auto rate = workspace.getDocument() != nullptr ? workspace.getDocument()->getSampleRate() : 48000.0;
                parameters = std::make_unique<ParameterPanel> (*rack[selected].type, rack[selected].values, rate);
                parameterView.setViewedComponent (parameters.get(), false);
            }

            shownValues = values;
        }

        shownSlot = selected;
        updateButtons();
        resized();
        repaint();
    }

    void RackPanel::showAddMenu()
    {
        juce::PopupMenu menu;
        const auto& all = dsp::catalogue::all();

        for (const auto& category : dsp::catalogue::categories())
        {
            juce::PopupMenu sub;
            for (size_t i = 0; i < all.size(); ++i)
                if (all[i].category == category && all[i].isRealtime())
                    sub.addItem ((int) i + 1, TRANS (all[i].name));

            if (sub.getNumItems() > 0)
                menu.addSubMenu (TRANS (category), sub);
        }

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addButton),
                            [safe = juce::Component::SafePointer<RackPanel> (this)] (int result)
        {
            if (safe != nullptr && result > 0)
                safe->rack.add (dsp::catalogue::all()[(size_t) result - 1].id);
        });
    }

    void RackPanel::showPresetMenu()
    {
        juce::PopupMenu menu;
        const auto racks = presets.getRackPresets();

        // Built-in starting points.
        menu.addSectionHeader (TRANS ("Built-in presets"));
        menu.addItem (1, TRANS ("Podcast voice"));
        menu.addItem (2, TRANS ("Clean up a voice recording"));
        menu.addItem (3, TRANS ("Radio voice"));

        if (! racks.empty())
        {
            menu.addSectionHeader (TRANS ("My presets"));
            for (size_t i = 0; i < racks.size(); ++i)
                menu.addItem (1000 + (int) i, racks[i].name);
        }

        menu.addSeparator();
        menu.addItem (9001, TRANS ("Save Rack as Preset..."), rack.size() > 0);
        menu.addItem (9002, TRANS ("Empty the Rack"), rack.size() > 0);

        if (! racks.empty())
        {
            juce::PopupMenu remove;
            for (size_t i = 0; i < racks.size(); ++i)
                remove.addItem (2000 + (int) i, racks[i].name);
            menu.addSubMenu (TRANS ("Delete Preset"), remove);
        }

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetButton),
                            [safe = juce::Component::SafePointer<RackPanel> (this), racks] (int result)
        {
            if (safe == nullptr || result == 0)
                return;

            auto& self = *safe;
            auto factory = [] (const char* id, std::initializer_list<std::pair<const char*, float>> changes)
            {
                auto s = dsp::EffectSettings::defaultsFor (id);
                const auto* type = dsp::catalogue::find (id);
                for (const auto& [param, value] : changes)
                    s.values[(size_t) type->indexOf (param)] = value;
                return s;
            };

            if (result == 1)
                self.rack.setSettings ({ factory ("parametricEq", { { "hpOn", 1 }, { "hpFreq", 80 }, { "b1Gain", -2 }, { "b2Freq", 3000 }, { "b2Gain", 2 }, { "highGain", 2 }, { "highFreq", 10000 } }),
                                         factory ("deEsser", {}),
                                         factory ("compressor", { { "threshold", -20 }, { "ratio", 3 }, { "attack", 8 }, { "makeup", 4 } }),
                                         factory ("limiter", { { "ceiling", -1 } }) });
            else if (result == 2)
                self.rack.setSettings ({ factory ("parametricEq", { { "hpOn", 1 }, { "hpFreq", 90 } }),
                                         factory ("gate", { { "threshold", -50 }, { "reduction", 12 } }),
                                         factory ("deEsser", {}) });
            else if (result == 3)
                self.rack.setSettings ({ factory ("parametricEq", { { "hpOn", 1 }, { "hpFreq", 100 }, { "lowGain", 3 }, { "b2Freq", 2500 }, { "b2Gain", 3 } }),
                                         factory ("compressor", { { "threshold", -26 }, { "ratio", 6 }, { "attack", 3 }, { "makeup", 8 } }),
                                         factory ("limiter", { { "ceiling", -1 }, { "inputGain", 3 } }) });
            else if (result >= 1000 && result < 1000 + (int) racks.size())
                self.rack.setSettings (racks[(size_t) result - 1000].chain);
            else if (result >= 2000 && result < 2000 + (int) racks.size())
                self.presets.deleteRackPreset (racks[(size_t) result - 2000].name);
            else if (result == 9002)
                self.rack.clear();
            else if (result == 9001)
                dialogs::askForText (TRANS ("Save Preset"), TRANS ("Name of the preset:"), {},
                                     [safe] (const juce::String& name)
                {
                    if (safe != nullptr && name.trim().isNotEmpty()
                        && ! safe->presets.saveRackPreset (name, safe->rack.getSettings()))
                        dialogs::showWarning (TRANS ("Save Preset"), TRANS ("The preset could not be saved."));
                });
        });
    }

    void RackPanel::resized()
    {
        auto area = getLocalBounds().reduced (padding);
        area.removeFromTop (titleHeight());

        auto top = area.removeFromTop (buttonHeight());
        listenButton.setBounds (top.removeFromLeft (top.getWidth() / 2 - 3));
        top.removeFromLeft (6);
        presetButton.setBounds (top);
        area.removeFromTop (8);

        applyButton.setBounds (area.removeFromBottom (buttonHeight() + 4));
        area.removeFromBottom (8);

        for (auto* row : rows)
        {
            row->setBounds (area.removeFromTop (rowHeight()));
            area.removeFromTop (3);
        }

        addButton.setBounds (area.removeFromTop (buttonHeight()));
        area.removeFromTop (12);

        parameterView.setBounds (area);
        if (parameters != nullptr)
        {
            const auto h = parameters->getIdealHeight();
            parameters->setSize (area.getWidth() - (h > area.getHeight() ? parameterView.getScrollBarThickness() + 2 : 0), h);
        }
    }

    void RackPanel::paint (juce::Graphics& g)
    {
        g.fillAll (palette::panel);
        g.setColour (palette::line);
        g.fillRect (getWidth() - 1, 0, 1, getHeight());

        auto area = getLocalBounds().reduced (padding);
        g.setColour (palette::text);
        g.setFont (fonts::sansBold (14.0f));
        g.drawText (TRANS ("Effect Rack"), area.removeFromTop (titleHeight()), juce::Justification::centredLeft);

        if (rack.size() == 0)
        {
            area.removeFromTop (buttonHeight() + 8 + buttonHeight() + 12);
            g.setColour (palette::textDim);
            g.setFont (fonts::sans (12.5f));
            g.drawFittedText (TRANS ("Add effects to build a chain. Switch on Listen Live and play to hear it, then apply it to the selection or the whole file."),
                              area.removeFromTop (fonts::scaled (90)), juce::Justification::topLeft, 5);
        }
    }
}
