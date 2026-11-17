// SPDX-License-Identifier: AGPL-3.0-only
#include "app/ui/StatusBar.h"
#include "app/theme/Fonts.h"
#include "app/theme/Palette.h"
#include "core/Version.h"

namespace mxj
{
    StatusBar::StatusBar() { refreshTexts(); }

    void StatusBar::setMessage (const juce::String& m)
    {
        message = m;
        repaint();
    }

    void StatusBar::refreshTexts()
    {
        message = TRANS ("Ready");
        rightText = juce::String (version::name) + " " + version::string + "  |  " + TRANS ("development build");
        repaint();
    }

    void StatusBar::paint (juce::Graphics& g)
    {
        g.fillAll (palette::panel);
        g.setColour (palette::line);
        g.fillRect (0, 0, getWidth(), 1);

        auto area = getLocalBounds().reduced (10, 0).withTrimmedTop (1);
        g.setFont (fonts::sans (12.0f));
        g.setColour (palette::text);
        g.drawText (message, area, juce::Justification::centredLeft);

        g.setFont (fonts::mono (11.5f));
        g.setColour (palette::textDim);
        g.drawText (rightText, area, juce::Justification::centredRight);
    }
}
