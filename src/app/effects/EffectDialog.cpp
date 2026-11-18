// SPDX-License-Identifier: AGPL-3.0-only
#include "app/effects/EffectDialog.h"
#include "app/plugins/PluginWindow.h"
#include "app/theme/Fonts.h"
#include "app/theme/Palette.h"
#include "app/ui/Dialogs.h"

#include <map>

namespace mxj
{
    namespace
    {
        constexpr int padding = 16;
        int buttonHeight() { return fonts::scaled (28); }
        int headerHeight() { return fonts::scaled (52); }

        /** The settings each effect had when its window closed (this run only). */
        std::map<juce::String, std::vector<float>>& lastValues()
        {
            static std::map<juce::String, std::vector<float>> values;
            return values;
        }
    }

    void EffectDialog::show (Workspace& workspace, PresetStore& presets, const dsp::EffectType& type, juce::Component* parent)
    {
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (new EffectDialog (workspace, presets, type));
        options.dialogTitle = TRANS (type.name);
        options.dialogBackgroundColour = palette::panel;
        options.componentToCentreAround = parent;
        options.useNativeTitleBar = true;
        options.resizable = false;
        options.escapeKeyTriggersCloseButton = true;
        options.launchAsync();
    }

    EffectDialog::EffectDialog (Workspace& w, PresetStore& p, const dsp::EffectType& t)
        : workspace (w), presets (p), type (t), values (t.createValues()),
          panel (t, values, w.getDocument() != nullptr ? w.getDocument()->getSampleRate() : 48000.0)
    {
        if (const auto it = lastValues().find (type.id); it != lastValues().end())
            values->setAll (it->second);
        panel.refreshFromValues();
        panel.onChange = [this] { presetName.clear(); presetButton.setButtonText (TRANS ("Presets") + "..."); };

        viewport.setViewedComponent (&panel, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);

        presetButton.setButtonText (TRANS ("Presets") + "...");
        presetButton.onClick = [this] { showPresetMenu(); };
        addAndMakeVisible (presetButton);

        const bool realtime = type.isRealtime();
        previewButton.setButtonText (TRANS ("Preview"));
        previewButton.setClickingTogglesState (true);
        previewButton.setTooltip (TRANS ("Listen to the effect while you change it (plays the selection, or from the cursor)"));
        previewButton.onClick = [this]
        {
            if (previewButton.getToggleState())
                workspace.startPreview (type, values);
            else
                workspace.stopPreview();
        };
        previewButton.setVisible (realtime);
        addChildComponent (previewButton);

        addToRackButton.setButtonText (TRANS ("Add to Rack"));
        addToRackButton.setTooltip (TRANS ("Put this effect with these settings into the effect rack"));
        addToRackButton.onClick = [this]
        {
            workspace.getRack().add (getSettings());
            addToRackButton.setButtonText (TRANS ("Added"));
        };
        addToRackButton.setVisible (realtime);
        addChildComponent (addToRackButton);

        resetButton.setButtonText (TRANS ("Reset"));
        resetButton.setTooltip (TRANS ("Put every control back to its default"));
        resetButton.onClick = [this] { applyPreset (type.getDefaults(), {}); };
        addAndMakeVisible (resetButton);

        cancelButton.setButtonText (TRANS ("Close"));
        cancelButton.onClick = [this] { closeDialog(); };
        addAndMakeVisible (cancelButton);

        applyButton.setButtonText (type.kind == dsp::EffectType::Kind::generator ? TRANS ("Insert") : TRANS ("Apply"));
        applyButton.onClick = [this] { apply(); };
        applyButton.setColour (juce::TextButton::buttonColourId, palette::accent);
        applyButton.setColour (juce::TextButton::textColourOffId, palette::onAccent);
        addAndMakeVisible (applyButton);

        workspace.addChangeListener (this);
        updateButtons();

        // VST3 and CLAP plugins are set in their own window: open it at once.
        if (PluginWindow::canShow (values->state))
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<EffectDialog> (this)]
            {
                if (safe != nullptr)
                    PluginWindow::show (safe->values->state, safe->type.name);
            });

        const auto panelHeight = panel.getIdealHeight();
        const auto width = fonts::scaled (type.response != nullptr ? 620 : 560);
        panel.setSize (width - 2 * padding - 12, panelHeight);
        const auto maxViewport = fonts::scaled (560);
        setSize (width, headerHeight() + juce::jmin (panelHeight, maxViewport) + buttonHeight() + 3 * padding + 8);
    }

    dsp::EffectSettings EffectDialog::getSettings() const
    {
        dsp::EffectSettings settings { type.id, values->getAll(), true };
        if (values->state != nullptr)
            settings.state = values->state->save(); // VST3 and CLAP plugins: everything set in their window

        return settings;
    }

    EffectDialog::~EffectDialog()
    {
        PluginWindow::closeFor (values->state);
        lastValues()[type.id] = values->getAll();
        workspace.removeChangeListener (this);
        workspace.stopPreview();
    }

    void EffectDialog::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        updateButtons();
    }

    void EffectDialog::updateButtons()
    {
        const bool canEdit = workspace.canEdit();
        applyButton.setEnabled (canEdit);
        previewButton.setEnabled (workspace.isReady() && ! workspace.isBusy() && ! workspace.isRecording());

        // Playback stopped (end reached, or stopped elsewhere): the preview is over.
        if (previewButton.getToggleState() && ! workspace.isPlaying())
        {
            previewButton.setToggleState (false, juce::dontSendNotification);
            workspace.stopPreview();
        }

        repaint();
    }

    juce::String EffectDialog::describeTarget() const
    {
        const auto* doc = workspace.getDocument();
        if (doc == nullptr)
            return {};

        if (type.kind == dsp::EffectType::Kind::generator)
            return workspace.hasSelection() ? TRANS ("Replaces the selection") : TRANS ("Inserted at the cursor");

        const auto range = workspace.getEffectRange();
        return (workspace.hasSelection() ? TRANS ("Applies to the selection") : TRANS ("Applies to the whole file"))
             + " (" + Workspace::formatTime (range.getLength(), doc->getSampleRate()) + ")";
    }

    void EffectDialog::showPresetMenu()
    {
        juce::PopupMenu menu;
        const auto user = presets.getEffectPresets (type.id);

        if (! type.factoryPresets.empty())
        {
            menu.addSectionHeader (TRANS ("Built-in presets"));
            for (size_t i = 0; i < type.factoryPresets.size(); ++i)
                menu.addItem ((int) i + 1, TRANS (type.factoryPresets[i].name));
        }

        if (! user.empty())
        {
            menu.addSectionHeader (TRANS ("My presets"));
            for (size_t i = 0; i < user.size(); ++i)
                menu.addItem (1000 + (int) i, user[i].name);
        }

        menu.addSeparator();
        menu.addItem (9001, TRANS ("Save Current Settings as Preset..."));

        if (! user.empty())
        {
            juce::PopupMenu remove;
            for (size_t i = 0; i < user.size(); ++i)
                remove.addItem (2000 + (int) i, user[i].name);
            menu.addSubMenu (TRANS ("Delete Preset"), remove);
        }

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetButton),
                            [safe = juce::Component::SafePointer<EffectDialog> (this), user] (int result)
        {
            if (safe == nullptr || result == 0)
                return;

            auto& self = *safe;
            if (result >= 1 && result <= (int) self.type.factoryPresets.size())
            {
                const auto& preset = self.type.factoryPresets[(size_t) result - 1];
                self.applyPreset (preset.values, TRANS (preset.name));
            }
            else if (result >= 1000 && result < 1000 + (int) user.size())
            {
                const auto& preset = user[(size_t) result - 1000];
                self.applyPreset (preset.settings.values, preset.name);
            }
            else if (result >= 2000 && result < 2000 + (int) user.size())
            {
                self.presets.deleteEffectPreset (self.type.id, user[(size_t) result - 2000].name);
            }
            else if (result == 9001)
            {
                self.savePreset();
            }
        });
    }

    void EffectDialog::applyPreset (const std::vector<float>& presetValues, const juce::String& name)
    {
        values->setAll (presetValues);
        panel.refreshFromValues();
        presetName = name;
        presetButton.setButtonText (name.isNotEmpty() ? name : TRANS ("Presets") + "...");
    }

    void EffectDialog::savePreset()
    {
        dialogs::askForText (TRANS ("Save Preset"), TRANS ("Name of the preset:"), presetName,
                             [safe = juce::Component::SafePointer<EffectDialog> (this)] (const juce::String& name)
        {
            if (safe == nullptr || name.trim().isEmpty())
                return;

            if (! safe->presets.saveEffectPreset (name, { safe->type.id, safe->values->getAll(), true }))
                dialogs::showWarning (TRANS ("Save Preset"), TRANS ("The preset could not be saved."));
            else
                safe->applyPreset (safe->values->getAll(), name.trim());
        });
    }

    void EffectDialog::apply()
    {
        workspace.stopPreview();
        workspace.applyEffect (getSettings());
        closeDialog();
    }

    void EffectDialog::closeDialog()
    {
        if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
            window->exitModalState (0);
    }

    void EffectDialog::resized()
    {
        auto area = getLocalBounds().reduced (padding);
        auto header = area.removeFromTop (headerHeight());
        presetButton.setBounds (header.removeFromRight (fonts::scaled (210)).withSizeKeepingCentre (fonts::scaled (210), buttonHeight()));

        auto buttons = area.removeFromBottom (buttonHeight());
        area.removeFromBottom (padding);
        viewport.setBounds (area);
        panel.setSize (area.getWidth() - (panel.getIdealHeight() > area.getHeight() ? viewport.getScrollBarThickness() + 4 : 0),
                       panel.getIdealHeight());

        const auto w = fonts::scaled (104);
        applyButton.setBounds (buttons.removeFromRight (w));
        buttons.removeFromRight (8);
        cancelButton.setBounds (buttons.removeFromRight (w));
        buttons.removeFromRight (8);
        resetButton.setBounds (buttons.removeFromRight (w));

        previewButton.setBounds (buttons.removeFromLeft (w));
        buttons.removeFromLeft (8);
        addToRackButton.setBounds (buttons.removeFromLeft (fonts::scaled (130)));
    }

    void EffectDialog::paint (juce::Graphics& g)
    {
        g.fillAll (palette::panel);
        auto header = getLocalBounds().reduced (padding).removeFromTop (headerHeight());
        header.removeFromRight (fonts::scaled (220));

        g.setColour (palette::text);
        g.setFont (fonts::sans (13.0f));
        g.drawFittedText (TRANS (type.description), header.removeFromTop (header.getHeight() / 2), juce::Justification::centredLeft, 1);
        g.setColour (palette::playhead);
        g.setFont (fonts::sans (12.0f));
        g.drawFittedText (describeTarget(), header, juce::Justification::centredLeft, 1);
    }
}
