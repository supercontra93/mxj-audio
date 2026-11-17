// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "engine/devices/AudioEngine.h"
#include "services/settings/Settings.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** Audio device settings: driver type (Windows Audio, exclusive and low
        latency modes, ASIO), output device, sample rate and buffer size, with a
        test tone, and the recording input (device and channels). Changes apply
        at once. The input is only opened while recording (AudioEngine). */
    class AudioSettingsPanel : public juce::Component,
                               private juce::ChangeListener
    {
    public:
        AudioSettingsPanel (AudioEngine& engine, Settings& settings);
        ~AudioSettingsPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        /** Opens the panel in its own window. `onClosed` runs when it closes
            (used to save the choice). */
        static void show (AudioEngine& engine, Settings& settings, juce::Component* parent, std::function<void()> onClosed);

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void refresh();
        void apply (const std::function<void (juce::AudioDeviceManager::AudioDeviceSetup&)>& change);
        void refreshInputs();

        AudioEngine& engine;
        Settings& settings;
        juce::ComboBox typeBox, deviceBox, rateBox, bufferBox, inputBox, inputChannelBox;
        juce::Array<std::pair<int, int>> inputChoices; ///< first channel, channel count
        juce::TextButton controlPanelButton, testToneButton;
        juce::String status, error;
        juce::Array<double> rates;
        juce::Array<int> bufferSizes;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioSettingsPanel)
    };
}
