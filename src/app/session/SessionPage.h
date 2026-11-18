// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "app/meters/MeterPanel.h"
#include "app/session/MixerPanel.h"
#include "app/session/SessionWorkspace.h"
#include "app/session/TimelineView.h"
#include "app/session/TrackEffectsPanel.h"
#include "app/ui/IconButton.h"
#include "core/commands/CommandRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** The page of the multitrack session (phase 5): transport and tool bar,
        the time line with its scroll bars, the mixer under it, and on the
        right the effects of the selected track and the level meters of the
        mix. Buttons run commands, so they behave like menus and shortcuts.
        The visibility of the panels is kept in the settings (Window menu). */
    class SessionPage : public juce::Component,
                        private juce::ChangeListener,
                        private juce::ScrollBar::Listener,
                        private juce::Timer
    {
    public:
        using RunCommand = std::function<void (const CommandId&)>;

        SessionPage (SessionWorkspace& workspace, const CommandRegistry& commands, RunCommand runCommand, AppContext& context);
        ~SessionPage() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
        void timerCallback() override;
        void updateFromWorkspace();
        void updatePanels();
        void updateScrollBars();
        juce::String describeSession() const;
        juce::String describeSelection() const;

        SessionWorkspace& workspace;
        const CommandRegistry& commands;
        AppContext& context;
        juce::OwnedArray<IconButton> buttons;
        IconButton* playButton = nullptr;
        IconButton* recordButton = nullptr;
        IconButton* snapButton = nullptr;
        IconButton* mixerButton = nullptr;
        juce::Array<int> groupEnds;

        TimelineView timeline;
        MixerPanel mixer;
        TrackEffectsPanel trackEffects;
        MeterPanel meterPanel;
        juce::ScrollBar horizontal { false }, vertical { true };
        juce::Rectangle<int> timeArea, infoArea;
        juce::String timeText;
        int followedTrack = -1;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SessionPage)
    };
}
