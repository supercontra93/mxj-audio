// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** The "Grafit" theme: dark graphite, amber accent, flat fills, 1 px lines,
        IBM Plex fonts. Every component gets its colours from here, never hard-coded. */
    class GrafitLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        GrafitLookAndFeel();

        juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

        // Menu bar
        void drawMenuBarBackground (juce::Graphics&, int width, int height, bool isMouseOverBar, juce::MenuBarComponent&) override;
        void drawMenuBarItem (juce::Graphics&, int width, int height, int itemIndex, const juce::String& itemText,
                              bool isMouseOverItem, bool isMenuOpen, bool isMouseOverBar, juce::MenuBarComponent&) override;
        juce::Font getMenuBarFont (juce::MenuBarComponent&, int itemIndex, const juce::String& itemText) override;
        int getMenuBarItemWidth (juce::MenuBarComponent&, int itemIndex, const juce::String& itemText) override;

        // Popup menus
        juce::Font getPopupMenuFont() override;
        void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;

        // Buttons
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                                   bool isMouseOverButton, bool isButtonDown) override;
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

        // Tooltips
        juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
        void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    };
}
