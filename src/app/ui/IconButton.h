// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "app/theme/Icons.h"
#include "app/theme/Palette.h"
#include "core/commands/CommandInfo.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** "ctrl + O" -> "Ctrl+O", for tooltips. */
    inline juce::String shortcutText (const CommandInfo& info)
    {
        if (info.defaultShortcut.isEmpty())
            return {};

        return " (" + juce::KeyPress::createFromDescription (info.defaultShortcut).getTextDescriptionWithIcons() + ")";
    }

    /** A square transport button with a Phosphor icon. */
    class IconButton : public juce::Button
    {
    public:
        IconButton (const CommandInfo& info, const juce::String& iconName, std::function<void (const CommandId&)> run,
                    juce::Colour iconColour = palette::text, juce::Colour highlight = palette::accent)
            : Button (TRANS (info.name)), commandId (info.id), colour (iconColour), highlightColour (highlight)
        {
            setIcon (iconName);
            setTooltip (TRANS (info.name) + shortcutText (info));
            setWantsKeyboardFocus (false); // Space must reach the play command, not "click" the button
            onClick = [id = info.id, run] { run (id); };
        }

        void setIcon (const juce::String& iconName)
        {
            if (iconName == currentIcon)
                return;

            currentIcon = iconName;
            icon = icons::create (iconName, colour);
            iconActive = icons::create (iconName, palette::onAccent);
            repaint();
        }

        void setHighlighted (bool shouldHighlight)
        {
            if (highlighted != shouldHighlight)
            {
                highlighted = shouldHighlight;
                repaint();
            }
        }

        void paintButton (juce::Graphics& g, bool isOver, bool isDown) override
        {
            auto b = getLocalBounds().toFloat().reduced (0.5f);
            const float alpha = isEnabled() ? 1.0f : 0.4f;

            const auto fill = highlighted ? highlightColour
                            : isDown      ? palette::line
                            : isOver      ? palette::panelRaised.brighter (0.06f)
                                          : palette::panelRaised;
            g.setColour (fill.withMultipliedAlpha (alpha));
            g.fillRoundedRectangle (b, palette::cornerRadius);

            if (! highlighted)
            {
                g.setColour ((isOver && isEnabled() ? palette::accent : palette::line).withMultipliedAlpha (alpha));
                g.drawRoundedRectangle (b, palette::cornerRadius, 1.0f);
            }

            auto& d = highlighted ? iconActive : icon;
            if (d != nullptr)
                d->drawWithin (g, getLocalBounds().toFloat().reduced (8.0f), juce::RectanglePlacement::centred, alpha);
        }

        const CommandId commandId;

    private:
        juce::String currentIcon;
        juce::Colour colour, highlightColour;
        std::unique_ptr<juce::Drawable> icon, iconActive;
        bool highlighted = false;
    };

}
