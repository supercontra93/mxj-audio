// SPDX-License-Identifier: AGPL-3.0-only
#include "app/meters/MeterPanel.h"
#include "app/theme/Fonts.h"
#include "app/theme/Palette.h"

namespace mxj
{
    namespace
    {
        constexpr float floorDb = -60.0f;
        constexpr int fps = 30;

        /** Bar position 0..1 for a level; the top 20 dB get more room, as on a mixing desk. */
        float meterPosition (float db)
        {
            if (db <= floorDb)
                return 0.0f;
            if (db >= 0.0f)
                return 1.0f;

            return db > -20.0f ? 0.5f + 0.5f * (db + 20.0f) / 20.0f
                               : 0.5f * (db - floorDb) / (-20.0f - floorDb);
        }

        juce::String lufsText (float v)
        {
            return v <= dsp::LoudnessMeter::silence + 0.5f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")) : juce::String (v, 1);
        }
    }

    int MeterPanel::preferredWidth()
    {
        return fonts::scaled (168);
    }

    MeterPanel::MeterPanel (dsp::LevelMeter& m) : meter (m)
    {
        setTooltip (TRANS ("Click to reset the peak holds and the loudness measurement"));
        startTimerHz (fps);
    }

    void MeterPanel::timerCallback()
    {
        const auto fall = 24.0f / fps; // 24 dB per second, as common peak meters

        for (int c = 0; c < 2; ++c)
        {
            auto& ch = channels[(size_t) c];
            const auto peak = dsp::gainToDb (meter.takePeak (c));
            const auto rms = dsp::gainToDb (meter.getRms (c));

            ch.peakDb = juce::jmax (peak, ch.peakDb - fall);
            ch.rmsDb = rms;

            if (peak >= ch.holdDb)
            {
                ch.holdDb = peak;
                ch.holdFrames = fps * 2;
            }
            else if (--ch.holdFrames <= 0)
            {
                ch.holdDb = juce::jmax (floorDb - 1.0f, ch.holdDb - fall);
            }

            if (peak >= -0.001f)
                ch.clipped = true;

            truePeakDb = juce::jmax (truePeakDb - fall * 0.25f, dsp::gainToDb (meter.takeTruePeak (c)));
        }

        repaint();
    }

    void MeterPanel::mouseDown (const juce::MouseEvent&)
    {
        meter.requestReset();
        for (auto& ch : channels)
            ch = {};
        truePeakDb = -100.0f;
        repaint();
    }

    void MeterPanel::paint (juce::Graphics& g)
    {
        g.fillAll (palette::panel);
        g.setColour (palette::line);
        g.fillRect (0, 0, 1, getHeight());

        auto area = getLocalBounds().reduced (fonts::scaled (10), fonts::scaled (8)).withTrimmedLeft (1);
        const auto lineH = fonts::scaled (19);

        g.setColour (palette::text);
        g.setFont (fonts::sansBold (13.0f));
        g.drawText (TRANS ("Levels"), area.removeFromTop (lineH + 4), juce::Justification::centredLeft);

        // Read-outs at the bottom.
        const auto& loud = meter.getLoudness();
        struct Line { juce::String label, value, unit; juce::Colour colour; };
        const Line lines[] {
            { TRANS ("Momentary"),  lufsText (loud.getMomentary()), "LUFS", palette::text },
            { TRANS ("Short-term"), lufsText (loud.getShortTerm()), "LUFS", palette::text },
            { TRANS ("Integrated"), lufsText (loud.getIntegrated()), "LUFS", palette::accent },
            { TRANS ("Range"),      juce::String (loud.getLoudnessRange(), 1), "LU", palette::text },
            { TRANS ("True peak"),  meter.getMaxTruePeakDb() <= -150.0f ? lufsText (-100.0f) : juce::String (meter.getMaxTruePeakDb(), 1), "dBTP",
              meter.getMaxTruePeakDb() > -1.0f ? palette::record : palette::text },
        };

        auto readouts = area.removeFromBottom ((int) std::size (lines) * lineH + lineH * 2 + 8);

        // Correlation bar: -1 ... +1.
        {
            auto row = readouts.removeFromTop (lineH);
            g.setFont (fonts::sans (11.5f));
            g.setColour (palette::textDim);
            g.drawText (TRANS ("Phase"), row.removeFromLeft (fonts::scaled (52)), juce::Justification::centredLeft);
            auto bar = row.reduced (0, row.getHeight() / 2 - 3).toFloat();
            g.setColour (palette::background);
            g.fillRoundedRectangle (bar, 2.0f);
            const auto corr = meter.getCorrelation();
            const auto x = bar.getX() + bar.getWidth() * (corr + 1.0f) * 0.5f;
            g.setColour (palette::line.brighter (0.4f));
            g.fillRect (bar.getCentreX(), bar.getY(), 1.0f, bar.getHeight());
            g.setColour (corr < 0.0f ? palette::record : palette::playhead);
            g.fillRoundedRectangle (juce::Rectangle<float> (x - 3.0f, bar.getY() - 2.0f, 6.0f, bar.getHeight() + 4.0f), 2.0f);
            readouts.removeFromTop (lineH / 2);
        }

        for (const auto& l : lines)
        {
            auto row = readouts.removeFromTop (lineH);
            g.setFont (fonts::sans (11.5f));
            g.setColour (palette::textDim);
            g.drawText (l.label, row, juce::Justification::centredLeft);
            auto unit = row.removeFromRight (fonts::scaled (34));
            g.drawText (l.unit, unit, juce::Justification::centredRight);
            g.setFont (fonts::mono (12.5f));
            g.setColour (l.colour);
            g.drawText (l.value, row, juce::Justification::centredRight);
        }

        // Bars with a dB scale between them and the right edge.
        area.removeFromBottom (lineH / 2);
        auto clipRow = area.removeFromTop (fonts::scaled (12));
        auto scale = area.removeFromRight (fonts::scaled (34));
        const auto barGap = fonts::scaled (6);
        const auto barWidth = (area.getWidth() - barGap) / 2;

        for (int c = 0; c < 2; ++c)
        {
            auto bar = juce::Rectangle<int> (area.getX() + c * (barWidth + barGap), area.getY(), barWidth, area.getHeight()).toFloat();
            const auto& ch = channels[(size_t) c];

            g.setColour (palette::background);
            g.fillRect (bar);

            const auto peakTop = bar.getBottom() - bar.getHeight() * meterPosition (ch.peakDb);
            const auto rmsTop = bar.getBottom() - bar.getHeight() * meterPosition (ch.rmsDb);
            const auto yellowLine = bar.getBottom() - bar.getHeight() * meterPosition (-12.0f);
            const auto redLine = bar.getBottom() - bar.getHeight() * meterPosition (-3.0f);

            // Peak: thin colour gradient green-amber-red by level; RMS: solid inner bar.
            juce::ColourGradient grad (palette::record, 0.0f, redLine, palette::meterGreen, 0.0f, bar.getBottom(), false);
            grad.addColour ((yellowLine - redLine) / juce::jmax (1.0f, bar.getBottom() - redLine), palette::accent);
            g.setGradientFill (grad);
            g.setOpacity (0.55f);
            g.fillRect (bar.withTop (peakTop));
            g.setOpacity (1.0f);
            g.setGradientFill (grad);
            g.fillRect (bar.withTop (rmsTop).reduced (bar.getWidth() * 0.2f, 0.0f));

            if (ch.holdDb > floorDb)
            {
                const auto y = bar.getBottom() - bar.getHeight() * meterPosition (ch.holdDb);
                g.setColour (ch.holdDb > -3.0f ? palette::record : palette::text);
                g.fillRect (bar.getX(), y - 1.0f, bar.getWidth(), 2.0f);
            }

            auto light = clipRow.withX (bar.toNearestInt().getX()).withWidth (barWidth).reduced (0, 2).toFloat();
            g.setColour (ch.clipped ? palette::record : palette::panelRaised);
            g.fillRoundedRectangle (light, 2.0f);
        }

        g.setFont (fonts::mono (10.0f));
        for (float db : { 0.0f, -3.0f, -6.0f, -12.0f, -20.0f, -30.0f, -40.0f, -60.0f })
        {
            const auto y = (float) area.getBottom() - (float) area.getHeight() * meterPosition (db);
            g.setColour (palette::line);
            g.fillRect ((float) area.getX(), y, (float) area.getWidth(), 1.0f);
            g.setColour (palette::textDim);
            g.drawText (juce::String ((int) db), juce::Rectangle<float> ((float) scale.getX(), y - 6.0f, (float) scale.getWidth(), 12.0f),
                        juce::Justification::centredRight);
        }
    }
}
