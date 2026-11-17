// SPDX-License-Identifier: AGPL-3.0-only
#include "app/ui/StartPage.h"
#include "app/theme/Fonts.h"
#include "app/theme/Icons.h"
#include "app/theme/Palette.h"
#include "core/Version.h"
#include "services/i18n/Localisation.h"
#include "services/updates/UpdatePolicy.h"

namespace mxj
{
    namespace
    {
        constexpr int gap = 1;          // panels are separated by 1 px lines
        constexpr int sideWidth = 240;
        constexpr int padding = 20;

        void drawSectionHeading (juce::Graphics& g, const juce::String& textToDraw, juce::Rectangle<int> area)
        {
            g.setColour (palette::textDim);
            g.setFont (fonts::sansBold (12.0f));
            g.drawText (textToDraw, area, juce::Justification::centredLeft);
        }

        /** "ctrl + shift + N" -> "Ctrl+Shift+N", the way Windows programs show shortcuts. */
        juce::String formatShortcut (const juce::String& description)
        {
            if (description.isEmpty())
                return {};

            juce::StringArray parts;
            parts.addTokens (juce::KeyPress::createFromDescription (description).getTextDescription(), "+", {});
            parts.trim();

            for (auto& p : parts)
                p = p.substring (0, 1).toUpperCase() + p.substring (1);

            return parts.joinIntoString ("+");
        }

        void drawKeyValue (juce::Graphics& g, juce::Rectangle<int>& area, const juce::String& key,
                           const juce::String& value, juce::Colour valueColour = palette::text)
        {
            auto row = area.removeFromTop (22);
            g.setFont (fonts::sans (12.5f));
            g.setColour (palette::textDim);
            g.drawText (key, row, juce::Justification::centredLeft);
            g.setFont (fonts::mono (12.0f));
            g.setColour (valueColour);
            g.drawText (value, row, juce::Justification::centredRight);
        }
    }

    //==============================================================================
    /** A large quick start button: icon, title and keyboard shortcut. */
    class StartPage::Tile : public juce::Button
    {
    public:
        Tile (const CommandInfo& info, const juce::String& iconName, juce::Colour iconColour, RunCommand run)
            : Button (TRANS (info.name)),
              shortcut (formatShortcut (info.defaultShortcut)),
              icon (icons::create (iconName, iconColour))
        {
            const bool enabled = info.isEnabled == nullptr || info.isEnabled();
            setEnabled (enabled);
            setTooltip (enabled ? TRANS (info.description)
                                : TRANS (info.description) + " - " + TRANS ("arrives in a later version"));
            onClick = [id = info.id, run] { run (id); };
        }

        void paintButton (juce::Graphics& g, bool isOver, bool isDown) override
        {
            auto b = getLocalBounds().toFloat().reduced (0.5f);
            const float alpha = isEnabled() ? 1.0f : 0.45f;

            g.setColour ((isDown ? palette::line : isOver ? palette::panelRaised.brighter (0.05f) : palette::panelRaised)
                             .withMultipliedAlpha (alpha));
            g.fillRoundedRectangle (b, palette::cornerRadius);
            g.setColour ((isOver && isEnabled() ? palette::accent : palette::line).withMultipliedAlpha (alpha));
            g.drawRoundedRectangle (b, palette::cornerRadius, 1.0f);

            auto area = getLocalBounds().reduced (14, 12);

            if (icon != nullptr)
                icon->drawWithin (g, area.removeFromTop (24).removeFromLeft (24).toFloat(),
                                  juce::RectanglePlacement::centred, alpha);

            area.removeFromTop (8);
            g.setColour (palette::text.withMultipliedAlpha (alpha));
            g.setFont (fonts::sansBold (13.5f));
            g.drawText (getButtonText(), area.removeFromTop (20), juce::Justification::centredLeft);

            g.setColour (palette::textDim.withMultipliedAlpha (alpha));
            g.setFont (fonts::mono (11.5f));
            g.drawText (shortcut, area.removeFromTop (18), juce::Justification::centredLeft);
        }

    private:
        juce::String shortcut;
        std::unique_ptr<juce::Drawable> icon;
    };

    //==============================================================================
    StartPage::StartPage (const CommandRegistry& commands, RunCommand runCommand, std::function<juce::String()> describeAudioDevice)
        : logo (icons::createLogo()), describeDevice (std::move (describeAudioDevice))
    {
        struct TileSpec { const char* command; const char* icon; juce::Colour colour; };

        const TileSpec specs[] = {
            { "file.new",        "plus",        palette::accent },
            { "file.newSession", "rows",        palette::accent },
            { "file.open",       "folder-open", palette::accent },
        };

        for (const auto& s : specs)
            if (const auto* info = commands.find (s.command))
                addAndMakeVisible (tiles.add (new Tile (*info, s.icon, s.colour, runCommand)));

        if (const auto* info = commands.find ("settings.audioDevices"))
        {
            deviceButton = std::make_unique<juce::TextButton> (TRANS (info->name));
            deviceButton->setWantsKeyboardFocus (false);
            deviceButton->onClick = [id = info->id, runCommand] { runCommand (id); };
            addAndMakeVisible (*deviceButton);
        }
    }

    StartPage::~StartPage() = default;

    void StartPage::resized()
    {
        auto area = getLocalBounds();
        sidePanel = area.removeFromRight (sideWidth);
        area.removeFromRight (gap);
        centrePanel = area;

        auto content = centrePanel.reduced (padding * 2, padding);
        content.removeFromTop (110); // logo, welcome text

        auto tileRow = content.removeFromTop (100);
        const int tileWidth = juce::jmin (220, (tileRow.getWidth() - (tiles.size() - 1) * 12) / juce::jmax (1, tiles.size()));

        for (auto* t : tiles)
        {
            t->setBounds (tileRow.removeFromLeft (tileWidth));
            tileRow.removeFromLeft (12);
        }

        content.removeFromTop (28);
        recentArea = content;

        if (deviceButton != nullptr)
            deviceButton->setBounds (sidePanel.reduced (16, 14).withTrimmedTop (24 + 4 * 22 + 14 + 24 + 70).withHeight (28));
    }

    void StartPage::paint (juce::Graphics& g)
    {
        g.fillAll (palette::line); // shows through the 1 px gaps
        g.setColour (palette::panel);
        g.fillRect (centrePanel);
        g.fillRect (sidePanel);

        // Welcome
        auto content = centrePanel.reduced (padding * 2, padding);
        auto header = content.removeFromTop (110);
        auto logoArea = header.removeFromLeft (56).withSizeKeepingCentre (56, 56);

        if (logo != nullptr)
            logo->drawWithin (g, logoArea.toFloat(), juce::RectanglePlacement::centred, 1.0f);

        header.removeFromLeft (16);
        auto titleArea = header.withSizeKeepingCentre (header.getWidth(), 56);

        g.setColour (palette::text);
        g.setFont (fonts::sansBold (22.0f));
        g.drawText (TRANS ("Welcome"), titleArea.removeFromTop (30), juce::Justification::centredLeft);
        g.setColour (palette::textDim);
        g.setFont (fonts::sans (13.5f));
        g.drawText (TRANS ("Start something new or continue where you left off."),
                    titleArea, juce::Justification::centredLeft);

        // Recent projects
        auto recent = recentArea;
        drawSectionHeading (g, TRANS ("Recent projects"), recent.removeFromTop (24));
        g.setColour (palette::line);
        g.fillRect (recent.removeFromTop (1));
        g.setColour (palette::textDim);
        g.setFont (fonts::sans (13.0f));
        g.drawText (TRANS ("No recent projects yet."), recent.removeFromTop (40), juce::Justification::centredLeft);

        // Side panel
        auto side = sidePanel.reduced (16, 14);
        drawSectionHeading (g, TRANS ("Version"), side.removeFromTop (24));
        drawKeyValue (g, side, version::name, version::string);
        drawKeyValue (g, side, TRANS ("Build"), TRANS ("Development"), palette::accent);
        drawKeyValue (g, side, TRANS ("Language"), Localisation::getCurrentLanguage().toUpperCase());
        drawKeyValue (g, side, TRANS ("Updates"), updates::enabled ? TRANS ("On") : TRANS ("Off"));

        side.removeFromTop (14);
        drawSectionHeading (g, TRANS ("Audio device"), side.removeFromTop (24));
        g.setColour (palette::textDim);
        g.setFont (fonts::sans (12.5f));
        g.drawFittedText (describeDevice != nullptr ? describeDevice().replace (" - ", "\n") : juce::String(),
                          side.removeFromTop (64), juce::Justification::topLeft, 4);
    }
}
