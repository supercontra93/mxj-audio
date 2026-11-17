// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** Content of the About window: logo, version, licence, third-party credits. */
    class AboutPanel : public juce::Component
    {
    public:
        AboutPanel();

        void paint (juce::Graphics&) override;
        void resized() override;

        /** Opens the About window (non-modal). */
        static void show (juce::Component* parent);

    private:
        std::unique_ptr<juce::Drawable> logo;
        juce::TextEditor credits;
        juce::HyperlinkButton sourceLink;
    };
}
