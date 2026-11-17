// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/commands/CommandRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** The page shown when no file is open: welcome text, quick start tiles,
        recent projects and a side panel with program information. */
    class StartPage : public juce::Component
    {
    public:
        using RunCommand = std::function<void (const CommandId&)>;

        StartPage (const CommandRegistry& commands, RunCommand runCommand, std::function<juce::String()> describeAudioDevice);
        ~StartPage() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        class Tile;

        juce::Rectangle<int> centrePanel, sidePanel, recentArea;
        juce::OwnedArray<Tile> tiles;
        std::unique_ptr<juce::Drawable> logo;
        std::function<juce::String()> describeDevice;
        std::unique_ptr<juce::TextButton> deviceButton;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StartPage)
    };
}
