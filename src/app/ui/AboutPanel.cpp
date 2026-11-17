// SPDX-License-Identifier: AGPL-3.0-only
#include "app/ui/AboutPanel.h"
#include "app/theme/Fonts.h"
#include "app/theme/Icons.h"
#include "app/theme/Palette.h"
#include "core/Version.h"

namespace mxj
{
    namespace
    {
        // Keep in step with THIRD_PARTY.md.
        juce::String getCreditsText()
        {
            return TRANS ("MXJ AUDIO is free software: you can redistribute it and/or modify it under the terms of "
                          "the GNU Affero General Public License version 3. It comes with no warranty.")
                 + "\n\n"
                 + TRANS ("Built with:") + "\n"
                 + "  JUCE 9 - AGPLv3\n"
                 + "  ASIO SDK (Steinberg, bundled with JUCE) - GPLv3\n"
                 + "  IBM Plex Sans / Mono - SIL Open Font License 1.1\n"
                 + "  Phosphor Icons - MIT\n\n"
                 + TRANS ("ASIO is a trademark and software of Steinberg Media Technologies GmbH.");
        }
    }

    AboutPanel::AboutPanel()
        : logo (icons::createLogo()),
          sourceLink (TRANS ("Source code on GitHub"), juce::URL ("https://github.com/supercontra93/mxj-audio"))
    {
        credits.setMultiLine (true);
        credits.setReadOnly (true);
        credits.setScrollbarsShown (true);
        credits.setCaretVisible (false);
        credits.setFont (fonts::sans (12.5f));
        credits.setColour (juce::TextEditor::backgroundColourId, palette::background);
        credits.setColour (juce::TextEditor::textColourId, palette::text);
        credits.setText (getCreditsText());
        addAndMakeVisible (credits);

        sourceLink.setColour (juce::HyperlinkButton::textColourId, palette::accent);
        sourceLink.setFont (fonts::sans (13.0f), false, juce::Justification::centredLeft);
        addAndMakeVisible (sourceLink);

        setSize (520, 380);
    }

    void AboutPanel::paint (juce::Graphics& g)
    {
        g.fillAll (palette::panel);

        auto header = getLocalBounds().reduced (24).removeFromTop (64);
        if (logo != nullptr)
            logo->drawWithin (g, header.removeFromLeft (64).toFloat(), juce::RectanglePlacement::centred, 1.0f);

        header.removeFromLeft (18);
        g.setFont (fonts::sansBold (22.0f));
        g.setColour (palette::text);
        g.drawText ("MXJ", header.removeFromTop (34), juce::Justification::bottomLeft);

        const auto mxjWidth = juce::GlyphArrangement::getStringWidthInt (fonts::sansBold (22.0f), "MXJ ");
        g.setColour (palette::accent);
        g.drawText ("AUDIO", header.withTrimmedLeft (mxjWidth).withHeight (34).translated (0, -34),
                    juce::Justification::bottomLeft);

        g.setFont (fonts::mono (12.5f));
        g.setColour (palette::textDim);
        g.drawText (TRANS ("Version") + " " + version::string, header, juce::Justification::topLeft);
    }

    void AboutPanel::resized()
    {
        auto area = getLocalBounds().reduced (24);
        area.removeFromTop (64 + 16);
        sourceLink.setBounds (area.removeFromBottom (24));
        area.removeFromBottom (8);
        credits.setBounds (area);
    }

    void AboutPanel::show (juce::Component* parent)
    {
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (new AboutPanel());
        options.dialogTitle = TRANS ("About MXJ AUDIO");
        options.dialogBackgroundColour = palette::panel;
        options.componentToCentreAround = parent;
        options.useNativeTitleBar = true;
        options.resizable = false;
        options.escapeKeyTriggersCloseButton = true;
        options.launchAsync();
    }
}
