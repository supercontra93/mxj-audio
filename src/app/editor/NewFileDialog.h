// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "services/settings/Settings.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** File > New Audio File: choose the sample rate and mono or stereo for an
        empty file. Remembers the last choice in the settings. */
    class NewFileDialog : public juce::Component
    {
    public:
        using Callback = std::function<void (double sampleRate, int numChannels)>;

        /** Sample rates offered, in Hz. */
        static const std::vector<int>& getSampleRates();

        /** Opens the dialog. `onCreate` runs when the user presses Create. */
        static void show (Settings& settings, juce::Component* parent, Callback onCreate);

        NewFileDialog (Settings&, Callback onCreate);

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void create();
        void closeDialog();

        Settings& settings;
        Callback callback;
        juce::ComboBox rateBox, channelBox;
        juce::TextButton createButton, cancelButton;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NewFileDialog)
    };
}
