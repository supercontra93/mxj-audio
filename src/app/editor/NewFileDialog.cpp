// SPDX-License-Identifier: AGPL-3.0-only
#include "app/editor/NewFileDialog.h"
#include "app/theme/Fonts.h"
#include "app/theme/Palette.h"
#include "services/settings/SettingKeys.h"

namespace mxj
{
    namespace
    {
        constexpr int padding = 20;
        int rowHeight()  { return fonts::scaled (28); }
        int labelWidth() { return fonts::scaled (130); }
    }

    const std::vector<int>& NewFileDialog::getSampleRates()
    {
        static const std::vector<int> rates { 8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000 };
        return rates;
    }

    void NewFileDialog::show (Settings& settings, juce::Component* parent, Callback onCreate)
    {
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (new NewFileDialog (settings, std::move (onCreate)));
        options.dialogTitle = TRANS ("New Audio File");
        options.dialogBackgroundColour = palette::panel;
        options.componentToCentreAround = parent;
        options.useNativeTitleBar = true;
        options.resizable = false;
        options.escapeKeyTriggersCloseButton = true;
        options.launchAsync();
    }

    NewFileDialog::NewFileDialog (Settings& s, Callback onCreate) : settings (s), callback (std::move (onCreate))
    {
        const auto& rates = getSampleRates();
        const auto storedRate = (int) settings.get (keys::newFileSampleRate);
        int selected = (int) (std::find (rates.begin(), rates.end(), 48000) - rates.begin());

        for (int i = 0; i < (int) rates.size(); ++i)
        {
            rateBox.addItem (juce::String (rates[(size_t) i]) + " Hz", i + 1);
            if (rates[(size_t) i] == storedRate)
                selected = i;
        }

        rateBox.setSelectedItemIndex (selected, juce::dontSendNotification);
        addAndMakeVisible (rateBox);

        channelBox.addItem (TRANS ("Mono"), 1);
        channelBox.addItem (TRANS ("Stereo"), 2);
        channelBox.setSelectedId ((int) settings.get (keys::newFileChannels) == 1 ? 1 : 2, juce::dontSendNotification);
        addAndMakeVisible (channelBox);

        createButton.setButtonText (TRANS ("Create"));
        createButton.onClick = [this] { create(); };
        addAndMakeVisible (createButton);

        cancelButton.setButtonText (TRANS ("Cancel"));
        cancelButton.onClick = [this] { closeDialog(); };
        addAndMakeVisible (cancelButton);

        setSize (fonts::scaled (420), fonts::scaled (200));
    }

    void NewFileDialog::create()
    {
        const auto& rates = getSampleRates();
        const auto rate = rates[(size_t) juce::jlimit (0, (int) rates.size() - 1, rateBox.getSelectedItemIndex())];
        const auto channels = channelBox.getSelectedId() == 1 ? 1 : 2;

        settings.set (keys::newFileSampleRate, rate);
        settings.set (keys::newFileChannels, channels);

        auto cb = callback;
        closeDialog(); // deletes this dialog
        if (cb != nullptr)
            cb ((double) rate, channels);
    }

    void NewFileDialog::closeDialog()
    {
        if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
            window->exitModalState (0);
    }

    void NewFileDialog::resized()
    {
        auto area = getLocalBounds().reduced (padding);
        area.removeFromTop (6);

        for (auto* box : { &rateBox, &channelBox })
        {
            auto row = area.removeFromTop (rowHeight());
            row.removeFromLeft (labelWidth());
            box->setBounds (row);
            area.removeFromTop (10);
        }

        auto buttons = area.removeFromBottom (rowHeight());
        cancelButton.setBounds (buttons.removeFromRight (fonts::scaled (100)));
        buttons.removeFromRight (10);
        createButton.setBounds (buttons.removeFromRight (fonts::scaled (110)));
    }

    void NewFileDialog::paint (juce::Graphics& g)
    {
        g.fillAll (palette::panel);
        auto area = getLocalBounds().reduced (padding);
        area.removeFromTop (6);
        g.setFont (fonts::sans (13.0f));
        g.setColour (palette::textDim);
        g.drawText (TRANS ("Sample rate"), area.removeFromTop (rowHeight()).removeFromLeft (labelWidth()), juce::Justification::centredLeft);
        area.removeFromTop (10);
        g.drawText (TRANS ("Channels"), area.removeFromTop (rowHeight()).removeFromLeft (labelWidth()), juce::Justification::centredLeft);
    }
}
