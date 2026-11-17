// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** Thin bar at the bottom of the window: status message on the left,
        program and format information on the right. */
    class StatusBar : public juce::Component
    {
    public:
        StatusBar();

        void setMessage (const juce::String& message);
        void refreshTexts();

        void paint (juce::Graphics&) override;

    private:
        juce::String message;
        juce::String rightText;
    };
}
