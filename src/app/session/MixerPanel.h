// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "app/session/SessionWorkspace.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** The mixer (master plan, module D: "mikser paneli"): one channel strip per
        track and bus, and the master on the right. Each strip has its output,
        a send to a bus with its level, the effects button, pan, mute, solo, a
        fader and a level meter. Moving a control is heard at once and becomes
        one undo step when the mouse is released. */
    class MixerPanel : public juce::Component,
                       private juce::ChangeListener,
                       private juce::Timer
    {
    public:
        /** `showEffects` opens the effects of a track (0 = master). */
        MixerPanel (SessionWorkspace& workspace, std::function<void (int trackId)> showEffects);
        ~MixerPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        static int preferredHeight();

    private:
        class Strip;

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;
        void update();

        SessionWorkspace& workspace;
        std::function<void (int)> showEffects;
        juce::Viewport viewport;
        juce::Component strips;
        juce::OwnedArray<Strip> trackStrips;
        std::unique_ptr<Strip> masterStrip;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerPanel)
    };
}
