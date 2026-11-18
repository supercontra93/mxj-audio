// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "app/effects/EffectRack.h"
#include "app/effects/ParameterPanel.h"
#include "app/session/SessionWorkspace.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** The effects of one track, bus or the master in the session (real-time,
        non-destructive: the clips never change). Same controls as the effect
        rack of the waveform editor; every change is heard at once and becomes
        an undo step of the session. */
    class TrackEffectsPanel : public juce::Component,
                              private juce::ChangeListener,
                              private juce::Timer
    {
    public:
        explicit TrackEffectsPanel (SessionWorkspace& workspace);
        ~TrackEffectsPanel() override;

        /** Shows the effects of a track (0 = master). */
        void showTrack (int trackId);
        int getShownTrack() const noexcept { return shownTrack; }

        void paint (juce::Graphics&) override;
        void resized() override;

        static int preferredWidth();

    private:
        class SlotRow;

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;
        void loadFromSession();
        void rebuildRows();
        void showAddMenu();
        void commit (bool asUndoStep);

        SessionWorkspace& workspace;
        EffectRack rack;
        int shownTrack = -1;
        juce::String shownJson;
        bool loading = false;

        juce::TextButton addButton;
        juce::OwnedArray<SlotRow> rows;
        std::unique_ptr<ParameterPanel> parameters;
        juce::Viewport parameterView;
        int shownSlot = -1;
        juce::Rectangle<int> titleArea;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackEffectsPanel)
    };
}
