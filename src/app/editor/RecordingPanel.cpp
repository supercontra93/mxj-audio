// SPDX-License-Identifier: AGPL-3.0-only
#include "app/editor/RecordingPanel.h"
#include "app/Workspace.h"
#include "app/theme/Fonts.h"
#include "app/theme/Palette.h"

namespace mxj
{
    namespace
    {
        constexpr float minDb = -60.0f;

        float toMeter (float gain)
        {
            const auto db = juce::Decibels::gainToDecibels (gain, minDb);
            return juce::jlimit (0.0f, 1.0f, (db - minDb) / -minDb);
        }
    }

    RecordingPanel::RecordingPanel (Recorder& r) : recorder (r)
    {
        setInterceptsMouseClicks (false, false);
    }

    void RecordingPanel::visibilityChanged()
    {
        if (isVisible())
        {
            for (int c = 0; c < 2; ++c)
            {
                levels[c] = holds[c] = 0.0f;
                clipped[c] = false;
            }
            startTimerHz (30);
        }
        else
        {
            stopTimer();
        }
    }

    void RecordingPanel::timerCallback()
    {
        for (int c = 0; c < 2; ++c)
        {
            const auto peak = recorder.takePeakLevel (c);
            levels[c] = juce::jmax (peak, levels[c] * 0.85f);

            if (peak >= holds[c] || --holdTicks[c] <= 0)
            {
                holds[c] = peak;
                holdTicks[c] = 45;
            }

            if (peak >= 0.999f)
                clipped[c] = true;
        }

        repaint();
    }

    void RecordingPanel::paintMeter (juce::Graphics& g, juce::Rectangle<float> area, int channel)
    {
        g.setColour (palette::background);
        g.fillRoundedRectangle (area, palette::cornerRadius);

        const auto level = toMeter (levels[channel]);
        auto bar = area.reduced (1.0f).withWidth ((area.getWidth() - 2.0f) * level);

        // Green-free palette: amber up to -6 dB, red above.
        const auto warnX = area.getX() + area.getWidth() * toMeter (juce::Decibels::decibelsToGain (-6.0f));
        g.setColour (palette::accent);
        g.fillRect (bar.withRight (juce::jmin (bar.getRight(), warnX)));
        if (bar.getRight() > warnX)
        {
            g.setColour (palette::record);
            g.fillRect (bar.withLeft (warnX));
        }

        const auto holdX = area.getX() + 1.0f + (area.getWidth() - 2.0f) * toMeter (holds[channel]);
        g.setColour (palette::text);
        g.fillRect (juce::Rectangle<float> (holdX - 1.0f, area.getY() + 1.0f, 2.0f, area.getHeight() - 2.0f));

        if (clipped[channel])
        {
            g.setColour (palette::record);
            g.setFont (fonts::sansBold (11.0f));
            g.drawText (TRANS ("CLIP"), area.withTrimmedRight (6.0f), juce::Justification::centredRight);
        }
    }

    void RecordingPanel::paint (juce::Graphics& g)
    {
        g.fillAll (palette::background);
        auto area = getLocalBounds().toFloat().reduced (24.0f, 16.0f);

        // Header: blinking dot, "Recording", time.
        auto header = area.removeFromTop (40.0f);
        const bool on = (juce::Time::getMillisecondCounter() / 500) % 2 == 0;
        g.setColour (palette::record.withAlpha (on ? 1.0f : 0.35f));
        g.fillEllipse (header.removeFromLeft (18.0f).withSizeKeepingCentre (14.0f, 14.0f));
        header.removeFromLeft (10.0f);

        g.setColour (palette::text);
        g.setFont (fonts::sansBold (16.0f));
        g.drawText (TRANS ("Recording"), header.removeFromLeft (140.0f), juce::Justification::centredLeft);

        const auto rate = recorder.getSampleRate();
        const auto frames = recorder.getFramesRecorded();
        g.setColour (palette::record);
        g.setFont (fonts::mono (26.0f));
        g.drawText (Workspace::formatTime (frames, rate), header.removeFromLeft (220.0f), juce::Justification::centredLeft);

        g.setColour (palette::textDim);
        g.setFont (fonts::sans (12.5f));
        g.drawText (juce::String (juce::roundToInt (rate)) + " Hz   |   "
                        + (recorder.getNumChannels() == 1 ? TRANS ("Mono") : TRANS ("Stereo"))
                        + "   |   " + TRANS ("Press R or Space to stop"),
                    header, juce::Justification::centredRight);

        // Level meters.
        area.removeFromTop (12.0f);
        for (int c = 0; c < recorder.getNumChannels(); ++c)
        {
            paintMeter (g, area.removeFromTop (16.0f), c);
            area.removeFromTop (4.0f);
        }

        // dB scale under the meters.
        auto scale = area.removeFromTop (16.0f);
        g.setFont (fonts::mono (10.0f));
        g.setColour (palette::textDim);
        for (auto db : { -60, -48, -36, -24, -18, -12, -6, -3, 0 })
        {
            const auto x = scale.getX() + scale.getWidth() * toMeter (juce::Decibels::decibelsToGain ((float) db));
            g.drawText (juce::String (db), juce::Rectangle<float> (x - 20.0f, scale.getY(), 40.0f, scale.getHeight()),
                        juce::Justification::centred);
        }

        // The take so far: at least 30 s wide, so it grows from the left.
        area.removeFromTop (12.0f);
        g.setColour (palette::panel);
        g.fillRect (area);

        if (rate <= 0 || area.getWidth() < 2.0f)
            return;

        const auto span = juce::jmax ((double) frames, rate * 30.0);
        const auto framesPerPixel = span / area.getWidth();
        const auto channels = recorder.getNumChannels();
        const auto laneHeight = area.getHeight() / (float) juce::jmax (1, channels);

        g.setColour (palette::record.withAlpha (0.85f));
        for (int c = 0; c < channels; ++c)
        {
            const auto lane = juce::Rectangle<float> (area.getX(), area.getY() + laneHeight * (float) c, area.getWidth(), laneHeight);
            const auto mid = lane.getCentreY();
            const auto half = lane.getHeight() * 0.5f - 2.0f;

            for (int x = 0; x < (int) area.getWidth(); ++x)
            {
                const auto start = (juce::int64) (x * framesPerPixel);
                const auto end = juce::jmax (start + 1, (juce::int64) ((x + 1) * framesPerPixel));
                if (start >= frames)
                    break;

                float lo, hi;
                if (! recorder.getMinMax (c, start, end, lo, hi))
                    continue;

                const auto top = mid - juce::jlimit (-1.0f, 1.0f, hi) * half;
                const auto bottom = mid - juce::jlimit (-1.0f, 1.0f, lo) * half;
                g.fillRect (juce::Rectangle<float> (area.getX() + (float) x, top, 1.0f, juce::jmax (1.0f, bottom - top)));
            }
        }
    }
}
