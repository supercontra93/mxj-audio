// SPDX-License-Identifier: AGPL-3.0-only
#include "app/theme/GrafitLookAndFeel.h"
#include "app/theme/Fonts.h"
#include "app/theme/Palette.h"

namespace mxj
{
    using namespace palette;

    GrafitLookAndFeel::GrafitLookAndFeel()
        : LookAndFeel_V4 (ColourScheme (background, panel, panel, line, text, panelRaised, onAccent, accent, text))
    {
        using juce::Colour;

        setColour (juce::ResizableWindow::backgroundColourId, background);
        setColour (juce::DocumentWindow::textColourId, text);

        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
        setColour (juce::PopupMenu::highlightedTextColourId, onAccent);
        setColour (juce::PopupMenu::headerTextColourId, textDim);

        setColour (juce::TextButton::buttonColourId, panelRaised);
        setColour (juce::TextButton::buttonOnColourId, accent);
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, onAccent);

        setColour (juce::Label::textColourId, text);
        setColour (juce::TooltipWindow::backgroundColourId, panelRaised);
        setColour (juce::TooltipWindow::textColourId, text);
        setColour (juce::TooltipWindow::outlineColourId, line);

        setColour (juce::ScrollBar::thumbColourId, line);
        setColour (juce::TextEditor::backgroundColourId, panelRaised);
        setColour (juce::TextEditor::outlineColourId, line);
        setColour (juce::TextEditor::focusedOutlineColourId, accent);
        setColour (juce::TextEditor::highlightColourId, selection);
        setColour (juce::CaretComponent::caretColourId, accent);
    }

    juce::Typeface::Ptr GrafitLookAndFeel::getTypefaceForFont (const juce::Font& font)
    {
        if (font.getTypefaceName() == juce::Font::getDefaultMonospacedFontName())
            return fonts::monoRegular();

        if (font.isBold())
            return fonts::sansSemiBold();

        if (font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
            return fonts::sansRegular();

        return LookAndFeel_V4::getTypefaceForFont (font);
    }

    void GrafitLookAndFeel::drawMenuBarBackground (juce::Graphics& g, int width, int height, bool, juce::MenuBarComponent&)
    {
        g.fillAll (panel);
        g.setColour (line);
        g.fillRect (0, height - 1, width, 1);
    }

    void GrafitLookAndFeel::drawMenuBarItem (juce::Graphics& g, int width, int height, int, const juce::String& itemText,
                                             bool isMouseOverItem, bool isMenuOpen, bool, juce::MenuBarComponent&)
    {
        if (isMenuOpen || isMouseOverItem)
        {
            g.setColour (panelRaised);
            g.fillRect (0, 0, width, height - 1);
        }

        g.setColour (isMenuOpen ? accent : text);
        g.setFont (fonts::sans (fonts::baseSize));
        g.drawFittedText (itemText, 0, 0, width, height - 1, juce::Justification::centred, 1);
    }

    juce::Font GrafitLookAndFeel::getMenuBarFont (juce::MenuBarComponent&, int, const juce::String&)
    {
        return fonts::sans (fonts::baseSize);
    }

    int GrafitLookAndFeel::getMenuBarItemWidth (juce::MenuBarComponent&, int, const juce::String& itemText)
    {
        return juce::GlyphArrangement::getStringWidthInt (fonts::sans (fonts::baseSize), itemText) + 20;
    }

    juce::Font GrafitLookAndFeel::getPopupMenuFont()
    {
        return fonts::sans (fonts::baseSize);
    }

    void GrafitLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
    {
        g.fillAll (panel);
        g.setColour (line);
        g.drawRect (0, 0, width, height, 1);
    }

    void GrafitLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                                  bool isMouseOverButton, bool isButtonDown)
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        auto fill = backgroundColour;

        if (! button.isEnabled())
            fill = fill.withMultipliedAlpha (0.5f);
        else if (isButtonDown)
            fill = fill.brighter (0.15f);
        else if (isMouseOverButton)
            fill = fill.brighter (0.08f);

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, cornerRadius);
        g.setColour (button.hasKeyboardFocus (true) ? accent : line);
        g.drawRoundedRectangle (bounds, cornerRadius, 1.0f);
    }

    juce::Font GrafitLookAndFeel::getTextButtonFont (juce::TextButton&, int)
    {
        return fonts::sans (fonts::baseSize);
    }

    juce::Rectangle<int> GrafitLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                              juce::Rectangle<int> parentArea)
    {
        const int w = juce::GlyphArrangement::getStringWidthInt (fonts::sans (12.0f), tipText) + 16;
        const int h = 24;

        return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                     screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6,
                                     w, h)
                   .constrainedWithin (parentArea);
    }

    void GrafitLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& tipText, int width, int height)
    {
        g.fillAll (panelRaised);
        g.setColour (line);
        g.drawRect (0, 0, width, height, 1);
        g.setColour (text);
        g.setFont (fonts::sans (12.0f));
        g.drawFittedText (tipText, 8, 0, width - 16, height, juce::Justification::centredLeft, 1);
    }
}
