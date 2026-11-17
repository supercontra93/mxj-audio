// SPDX-License-Identifier: AGPL-3.0-only
#include "app/settings/AudioSettingsPanel.h"
#include "app/theme/Fonts.h"
#include "app/theme/Palette.h"

namespace mxj
{
    namespace
    {
        constexpr int rowHeight = 28;
        constexpr int labelWidth = 150;
        constexpr int padding = 20;

        const char* const labels[] { "Driver type", "Output device", "Sample rate", "Buffer size" };
    }

    AudioSettingsPanel::AudioSettingsPanel (AudioEngine& e) : engine (e)
    {
        for (auto* box : { &typeBox, &deviceBox, &rateBox, &bufferBox })
            addAndMakeVisible (box);

        typeBox.onChange = [this]
        {
            const auto name = typeBox.getText();
            if (name.isNotEmpty() && name != engine.getDeviceManager().getCurrentAudioDeviceType())
            {
                engine.getDeviceManager().setCurrentAudioDeviceType (name, true);
                error = engine.getDeviceManager().getCurrentAudioDevice() == nullptr ? TRANS ("No device of this type could be opened.") : juce::String();
                refresh();
            }
        };

        deviceBox.onChange = [this]
        {
            apply ([name = deviceBox.getText()] (auto& setup)
            {
                setup.outputDeviceName = name;
                setup.useDefaultOutputChannels = true;
                setup.sampleRate = 0;  // let the new device choose
                setup.bufferSize = 0;
            });
        };

        rateBox.onChange = [this]
        {
            const auto index = rateBox.getSelectedItemIndex();
            if (juce::isPositiveAndBelow (index, rates.size()))
                apply ([r = rates[index]] (auto& setup) { setup.sampleRate = r; });
        };

        bufferBox.onChange = [this]
        {
            const auto index = bufferBox.getSelectedItemIndex();
            if (juce::isPositiveAndBelow (index, bufferSizes.size()))
                apply ([b = bufferSizes[index]] (auto& setup) { setup.bufferSize = b; });
        };

        controlPanelButton.setButtonText (TRANS ("Driver Control Panel..."));
        controlPanelButton.onClick = [this]
        {
            if (auto* device = engine.getDeviceManager().getCurrentAudioDevice())
                if (device->showControlPanel())
                    engine.getDeviceManager().restartLastAudioDevice();
        };
        addChildComponent (controlPanelButton);

        testToneButton.setButtonText (TRANS ("Play Test Tone"));
        testToneButton.setClickingTogglesState (true);
        testToneButton.onClick = [this]
        {
            engine.getPlayback().setTestToneEnabled (testToneButton.getToggleState());
            testToneButton.setButtonText (testToneButton.getToggleState() ? TRANS ("Stop Test Tone") : TRANS ("Play Test Tone"));
        };
        addAndMakeVisible (testToneButton);

        engine.getDeviceManager().addChangeListener (this);
        refresh();
        setSize (520, 330);
    }

    AudioSettingsPanel::~AudioSettingsPanel()
    {
        engine.getPlayback().setTestToneEnabled (false);
        engine.getDeviceManager().removeChangeListener (this);
    }

    void AudioSettingsPanel::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        refresh();
    }

    void AudioSettingsPanel::apply (const std::function<void (juce::AudioDeviceManager::AudioDeviceSetup&)>& change)
    {
        auto& dm = engine.getDeviceManager();
        auto setup = dm.getAudioDeviceSetup();
        change (setup);
        setup.inputDeviceName = {}; // recording arrives in phase 2
        setup.useDefaultInputChannels = false;
        setup.inputChannels.clear();

        error = dm.setAudioDeviceSetup (setup, true);
        refresh();
    }

    void AudioSettingsPanel::refresh()
    {
        auto& dm = engine.getDeviceManager();

        // Driver types.
        typeBox.clear (juce::dontSendNotification);
        int id = 1;
        for (auto* type : dm.getAvailableDeviceTypes())
            typeBox.addItem (type->getTypeName(), id++);
        typeBox.setText (dm.getCurrentAudioDeviceType(), juce::dontSendNotification);

        // Output devices of the current type.
        deviceBox.clear (juce::dontSendNotification);
        if (auto* type = dm.getCurrentDeviceTypeObject())
        {
            type->scanForDevices();
            id = 1;
            for (const auto& name : type->getDeviceNames (false))
                deviceBox.addItem (name, id++);
        }

        auto* device = dm.getCurrentAudioDevice();
        deviceBox.setText (device != nullptr ? device->getName() : juce::String(), juce::dontSendNotification);

        // Rates and buffer sizes of the open device.
        rateBox.clear (juce::dontSendNotification);
        bufferBox.clear (juce::dontSendNotification);
        rates.clearQuick();
        bufferSizes.clearQuick();

        if (device != nullptr)
        {
            rates = device->getAvailableSampleRates();
            for (int i = 0; i < rates.size(); ++i)
                rateBox.addItem (juce::String (juce::roundToInt (rates[i])) + " Hz", i + 1);
            rateBox.setSelectedItemIndex (rates.indexOf (device->getCurrentSampleRate()), juce::dontSendNotification);

            const auto rate = device->getCurrentSampleRate();
            bufferSizes = device->getAvailableBufferSizes();
            for (int i = 0; i < bufferSizes.size(); ++i)
                bufferBox.addItem (juce::String (bufferSizes[i]) + " " + TRANS ("samples") + " ("
                                       + juce::String (rate > 0 ? 1000.0 * bufferSizes[i] / rate : 0.0, 1) + " ms)", i + 1);
            bufferBox.setSelectedItemIndex (bufferSizes.indexOf (device->getCurrentBufferSizeSamples()), juce::dontSendNotification);

            status = TRANS ("Output latency") + ": "
                   + juce::String (rate > 0 ? 1000.0 * (device->getOutputLatencyInSamples() + device->getCurrentBufferSizeSamples()) / rate : 0.0, 1)
                   + " ms";
        }
        else
        {
            status = TRANS ("No audio output device is open.");
        }

        for (auto* box : { &deviceBox, &rateBox, &bufferBox })
            box->setEnabled (box->getNumItems() > 0);

        controlPanelButton.setVisible (device != nullptr && device->hasControlPanel());
        testToneButton.setEnabled (device != nullptr);
        repaint();
    }

    void AudioSettingsPanel::resized()
    {
        auto area = getLocalBounds().reduced (padding);
        area.removeFromTop (8);

        for (auto* box : { &typeBox, &deviceBox, &rateBox, &bufferBox })
        {
            auto row = area.removeFromTop (rowHeight);
            row.removeFromLeft (labelWidth);
            box->setBounds (row);
            area.removeFromTop (10);
        }

        area.removeFromTop (4);
        auto buttons = area.removeFromTop (rowHeight);
        testToneButton.setBounds (buttons.removeFromLeft (160));
        buttons.removeFromLeft (10);
        controlPanelButton.setBounds (buttons.removeFromLeft (200));
    }

    void AudioSettingsPanel::paint (juce::Graphics& g)
    {
        g.fillAll (palette::panel);

        auto area = getLocalBounds().reduced (padding);
        area.removeFromTop (8);
        g.setFont (fonts::sans (13.0f));

        for (const auto* label : labels)
        {
            g.setColour (palette::textDim);
            g.drawText (TRANS (label), area.removeFromTop (rowHeight).removeFromLeft (labelWidth), juce::Justification::centredLeft);
            area.removeFromTop (10);
        }

        area.removeFromTop (4 + rowHeight + 16);
        g.setColour (palette::text);
        g.drawText (status, area.removeFromTop (20), juce::Justification::centredLeft);

        if (error.isNotEmpty())
        {
            g.setColour (palette::record);
            g.drawFittedText (error, area.removeFromTop (40), juce::Justification::topLeft, 2);
        }
        else
        {
            g.setColour (palette::textDim);
            g.setFont (fonts::sans (12.0f));
            g.drawFittedText (TRANS ("Smaller buffers give lower latency but need a faster computer. "
                                     "If playback crackles, choose a larger buffer."),
                              area.removeFromTop (40), juce::Justification::topLeft, 3);
        }
    }

    void AudioSettingsPanel::show (AudioEngine& engine, juce::Component* parent, std::function<void()> onClosed)
    {
        // Saves the choice when the window closes.
        struct Holder : public juce::Component
        {
            Holder (AudioEngine& e, std::function<void()> f) : panel (e), closed (std::move (f))
            {
                addAndMakeVisible (panel);
                setSize (panel.getWidth(), panel.getHeight());
            }

            ~Holder() override
            {
                if (closed != nullptr)
                    closed();
            }

            void resized() override { panel.setBounds (getLocalBounds()); }

            AudioSettingsPanel panel;
            std::function<void()> closed;
        };

        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (new Holder (engine, std::move (onClosed)));
        options.dialogTitle = TRANS ("Audio Device Settings");
        options.dialogBackgroundColour = palette::panel;
        options.componentToCentreAround = parent;
        options.useNativeTitleBar = true;
        options.resizable = false;
        options.escapeKeyTriggersCloseButton = true;
        options.launchAsync();
    }
}
