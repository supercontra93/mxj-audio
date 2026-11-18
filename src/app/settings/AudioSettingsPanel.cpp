// SPDX-License-Identifier: AGPL-3.0-only
#include "app/settings/AudioSettingsPanel.h"
#include "app/theme/Fonts.h"
#include "app/theme/Palette.h"
#include "services/settings/SettingKeys.h"

namespace mxj
{
    namespace
    {
        int rowHeight()  { return fonts::scaled (28); }
        int labelWidth() { return fonts::scaled (150); }
        constexpr int padding = 20;

        const char* const labels[] { "Driver type", "Output device", "Sample rate", "Buffer size",
                                     "Recording input", "Input channels" };
    }

    AudioSettingsPanel::AudioSettingsPanel (AudioEngine& e, Settings& s) : engine (e), settings (s)
    {
        for (auto* box : { &typeBox, &deviceBox, &rateBox, &bufferBox, &inputBox, &inputChannelBox })
            addAndMakeVisible (box);

        inputBox.onChange = [this]
        {
            // Item 1 is "default".
            settings.set (keys::recordInputDevice, inputBox.getSelectedId() <= 1 ? juce::String() : inputBox.getText());
            refreshInputs();
        };

        inputChannelBox.onChange = [this]
        {
            const auto index = inputChannelBox.getSelectedItemIndex();
            if (juce::isPositiveAndBelow (index, inputChoices.size()))
            {
                settings.set (keys::recordFirstChannel, inputChoices[index].first);
                settings.set (keys::recordChannels, inputChoices[index].second);
            }
        };

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
        setSize (fonts::scaled (520), fonts::scaled (430));
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
        setup.inputDeviceName = {}; // inputs open only while recording (AudioEngine::startRecording)
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
        refreshInputs();
        repaint();
    }

    void AudioSettingsPanel::refreshInputs()
    {
        const auto chosen = settings.getString (keys::recordInputDevice);
        const auto names = engine.getInputDeviceNames();

        inputBox.clear (juce::dontSendNotification);
        inputBox.addItem (TRANS ("System default"), 1);
        for (int i = 0; i < names.size(); ++i)
            inputBox.addItem (names[i], i + 2);

        const auto index = names.indexOf (chosen);
        inputBox.setSelectedId (index >= 0 ? index + 2 : 1, juce::dontSendNotification);

        // Mono on each input, and stereo pairs.
        const auto channelNames = engine.getInputChannelNames (chosen);
        inputChannelBox.clear (juce::dontSendNotification);
        inputChoices.clear();

        for (int i = 0; i < channelNames.size(); ++i)
        {
            inputChoices.add ({ i, 1 });
            inputChannelBox.addItem (TRANS ("Mono") + ": " + channelNames[i], inputChoices.size());
        }

        for (int i = 0; i + 1 < channelNames.size(); i += 2)
        {
            inputChoices.add ({ i, 2 });
            inputChannelBox.addItem (TRANS ("Stereo") + ": " + channelNames[i] + " + " + channelNames[i + 1], inputChoices.size());
        }

        const std::pair<int, int> current { (int) settings.get (keys::recordFirstChannel), (int) settings.get (keys::recordChannels) };
        inputChannelBox.setSelectedItemIndex (juce::jmax (0, inputChoices.indexOf (current)), juce::dontSendNotification);
        inputBox.setEnabled (names.size() > 0);
        inputChannelBox.setEnabled (inputChoices.size() > 0);
    }

    void AudioSettingsPanel::resized()
    {
        auto area = getLocalBounds().reduced (padding);
        area.removeFromTop (8);

        for (auto* box : { &typeBox, &deviceBox, &rateBox, &bufferBox, &inputBox, &inputChannelBox })
        {
            auto row = area.removeFromTop (rowHeight());
            row.removeFromLeft (labelWidth());
            box->setBounds (row);
            area.removeFromTop (box == &bufferBox ? 26 : 10);
        }

        area.removeFromTop (4);
        auto buttons = area.removeFromTop (rowHeight());
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

        int row = 0;
        for (const auto* label : labels)
        {
            g.setColour (palette::textDim);
            g.drawText (TRANS (label), area.removeFromTop (rowHeight()).removeFromLeft (labelWidth()), juce::Justification::centredLeft);
            area.removeFromTop (row++ == 3 ? 26 : 10);
        }

        area.removeFromTop (4 + rowHeight() + 16);
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

    void AudioSettingsPanel::show (AudioEngine& engine, Settings& settings, juce::Component* parent, std::function<void()> onClosed)
    {
        // Saves the choice when the window closes.
        struct Holder : public juce::Component
        {
            Holder (AudioEngine& e, Settings& s, std::function<void()> f) : panel (e, s), closed (std::move (f))
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
        options.content.setOwned (new Holder (engine, settings, std::move (onClosed)));
        options.dialogTitle = TRANS ("Audio Device Settings");
        options.dialogBackgroundColour = palette::panel;
        options.componentToCentreAround = parent;
        options.useNativeTitleBar = true;
        options.resizable = false;
        options.escapeKeyTriggersCloseButton = true;
        options.launchAsync();
    }
}
