// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "app/Workspace.h"
#include "app/editor/WaveformView.h"
#include "core/commands/CommandRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** The page shown while a file is open: transport bar, waveform and scroll bar.
        Buttons run commands, so they behave exactly like menus and shortcuts. */
    class EditorPage : public juce::Component,
                       private juce::ChangeListener,
                       private juce::ScrollBar::Listener,
                       private juce::Timer
    {
    public:
        using RunCommand = std::function<void (const CommandId&)>;

        EditorPage (Workspace& workspace, const CommandRegistry& commands, RunCommand runCommand);
        ~EditorPage() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        static constexpr int transportHeight = 48;

    private:
        class IconButton;

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
        void timerCallback() override;
        void updateFromWorkspace();

        juce::String describeFile() const;

        Workspace& workspace;
        const CommandRegistry& commands;
        juce::OwnedArray<IconButton> buttons;
        IconButton* playButton = nullptr;
        WaveformView waveform;
        juce::ScrollBar scrollBar { false };
        juce::Rectangle<int> timeArea, infoArea;
        juce::String timeText;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorPage)
    };
}
