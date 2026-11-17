// SPDX-License-Identifier: AGPL-3.0-only
#include "app/effects/StatsDialog.h"
#include "app/Workspace.h"
#include "app/theme/Fonts.h"
#include "app/theme/Palette.h"

namespace mxj
{
    namespace
    {
        constexpr int padding = 18;
        int rowHeight() { return fonts::scaled (22); }

        juce::String db (float v, const char* unit = "dB")
        {
            return v <= -150.0f ? TRANS ("silence") : juce::String (v, 2) + " " + unit;
        }

        juce::String lufs (float v)
        {
            return v <= dsp::LoudnessMeter::silence + 0.5f ? TRANS ("too short or too quiet") : juce::String (v, 1) + " LUFS";
        }
    }

    void StatsDialog::show (const dsp::AudioStats& stats, const juce::String& what, juce::Component* parent)
    {
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (new StatsDialog (stats, what));
        options.dialogTitle = TRANS ("Loudness and Levels");
        options.dialogBackgroundColour = palette::panel;
        options.componentToCentreAround = parent;
        options.useNativeTitleBar = true;
        options.resizable = false;
        options.escapeKeyTriggersCloseButton = true;
        options.launchAsync();
    }

    std::vector<StatsDialog::Row> StatsDialog::rowsFor (const dsp::AudioStats& s)
    {
        std::vector<Row> r;
        r.push_back ({ TRANS ("Loudness (EBU R128)"), {}, true });
        r.push_back ({ TRANS ("Integrated loudness"), lufs (s.integratedLufs) });
        r.push_back ({ TRANS ("Loudness range"), juce::String (s.loudnessRange, 1) + " LU" });
        r.push_back ({ TRANS ("Highest momentary loudness"), lufs (s.maxMomentaryLufs) });
        r.push_back ({ TRANS ("Highest short-term loudness"), lufs (s.maxShortTermLufs) });
        r.push_back ({ TRANS ("True peak"), db (s.truePeakDb, "dBTP") });

        r.push_back ({ TRANS ("Levels"), {}, true });
        r.push_back ({ TRANS ("Peak"), db (s.peakDb) });
        r.push_back ({ TRANS ("RMS (average level)"), db (s.rmsDb) });

        for (int c = 0; c < s.numChannels && c < 8; ++c)
        {
            const auto name = s.numChannels == 1 ? TRANS ("Mono")
                            : s.numChannels == 2 ? (c == 0 ? TRANS ("Left") : TRANS ("Right"))
                                                 : TRANS ("Channel") + " " + juce::String (c + 1);

            r.push_back ({ name, {}, true });
            r.push_back ({ TRANS ("Peak"), db (s.channelPeakDb[(size_t) c]) });
            r.push_back ({ TRANS ("RMS (average level)"), db (s.channelRmsDb[(size_t) c]) });
            r.push_back ({ TRANS ("DC offset"), juce::String (s.channelDcOffset[(size_t) c] * 100.0f, 3) + " %" });
            r.push_back ({ TRANS ("Clipped samples"), juce::String (s.channelClipped[(size_t) c]) });
        }

        return r;
    }

    juce::String StatsDialog::toText (const dsp::AudioStats& s)
    {
        juce::String text;
        for (const auto& row : rowsFor (s))
            text << (row.header ? "\n" + row.label + "\n" : "  " + row.label + ": " + row.value + "\n");

        return text.trim();
    }

    StatsDialog::StatsDialog (const dsp::AudioStats& s, juce::String what)
        : stats (s), description (std::move (what)), rows (rowsFor (s))
    {
        copyButton.setButtonText (TRANS ("Copy"));
        copyButton.setTooltip (TRANS ("Copy the table as text"));
        copyButton.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (description + "\n\n" + toText (stats)); };
        addAndMakeVisible (copyButton);

        closeButton.setButtonText (TRANS ("Close"));
        closeButton.onClick = [this]
        {
            if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
                window->exitModalState (0);
        };
        addAndMakeVisible (closeButton);

        setSize (fonts::scaled (440), padding * 3 + rowHeight() * ((int) rows.size() + 2) + fonts::scaled (28));
    }

    void StatsDialog::resized()
    {
        auto buttons = getLocalBounds().reduced (padding).removeFromBottom (fonts::scaled (28));
        closeButton.setBounds (buttons.removeFromRight (fonts::scaled (100)));
        buttons.removeFromRight (8);
        copyButton.setBounds (buttons.removeFromRight (fonts::scaled (100)));
    }

    void StatsDialog::paint (juce::Graphics& g)
    {
        g.fillAll (palette::panel);
        auto area = getLocalBounds().reduced (padding);

        g.setColour (palette::playhead);
        g.setFont (fonts::sans (12.5f));
        g.drawFittedText (description, area.removeFromTop (rowHeight()), juce::Justification::centredLeft, 1);
        area.removeFromTop (rowHeight() / 2);

        for (const auto& row : rows)
        {
            auto r = area.removeFromTop (rowHeight());
            if (row.header)
            {
                g.setColour (palette::text);
                g.setFont (fonts::sansBold (12.5f));
                g.drawText (row.label, r, juce::Justification::bottomLeft);
                continue;
            }

            g.setColour (palette::textDim);
            g.setFont (fonts::sans (12.5f));
            g.drawText (row.label, r.withTrimmedLeft (12), juce::Justification::centredLeft);
            g.setColour (palette::text);
            g.setFont (fonts::mono (12.5f));
            g.drawText (row.value, r, juce::Justification::centredRight);
        }
    }
}
