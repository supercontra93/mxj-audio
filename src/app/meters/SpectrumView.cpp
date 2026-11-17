// SPDX-License-Identifier: AGPL-3.0-only
#include "app/meters/SpectrumView.h"
#include "app/theme/Fonts.h"
#include "app/theme/Palette.h"

#include <cmath>

namespace mxj
{
    namespace
    {
        constexpr float topDb = 0.0f, bottomDb = -96.0f;
    }

    int SpectrumView::preferredHeight()
    {
        return fonts::scaled (150);
    }

    SpectrumView::SpectrumView (PlaybackEngine& p) : playback (p)
    {
        const auto n = (size_t) spectrum.getSize();
        history.assign (n, 0.0f);
        scratch.assign (n, 0.0f);
        magnitudes.assign ((size_t) spectrum.getNumBins(), bottomDb);
        smoothed.assign ((size_t) spectrum.getNumBins(), bottomDb);
        peaks.assign ((size_t) spectrum.getNumBins(), bottomDb);
        setOpaque (true);
        startTimerHz (30);
    }

    void SpectrumView::timerCallback()
    {
        // Slide the newest output into the history window.
        const auto got = playback.readRecentOutput (scratch.data(), (int) scratch.size());
        if (got > 0)
        {
            const auto keep = (int) history.size() - got;
            if (keep > 0)
                std::move (history.begin() + got, history.end(), history.begin());
            std::copy_n (scratch.begin(), got, history.begin() + juce::jmax (0, keep));
        }

        active = playback.isPlaying();
        spectrum.compute (history.data(), magnitudes.data());

        for (size_t i = 0; i < magnitudes.size(); ++i)
        {
            const auto target = active ? magnitudes[i] : bottomDb;
            smoothed[i] = target > smoothed[i] ? smoothed[i] * 0.3f + target * 0.7f : smoothed[i] * 0.8f + target * 0.2f;
            peaks[i] = juce::jmax (smoothed[i], peaks[i] - 0.6f);
        }

        repaint();
    }

    void SpectrumView::paint (juce::Graphics& g)
    {
        g.fillAll (palette::background);
        auto area = getLocalBounds().toFloat().reduced (1.0f).withTrimmedLeft ((float) fonts::scaled (34)).withTrimmedBottom ((float) fonts::scaled (14));

        const auto rate = juce::jmax (8000.0, playback.getDeviceSampleRate() > 0.0 ? playback.getDeviceSampleRate() : 48000.0);
        const auto xFor = [&] (double hz) { return area.getX() + area.getWidth() * (float) (std::log (hz / 20.0) / std::log (1000.0)); };
        const auto yFor = [&] (float db) { return area.getY() + area.getHeight() * (topDb - db) / (topDb - bottomDb); };

        g.setFont (fonts::mono (10.0f));
        for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        {
            const auto x = xFor (hz);
            g.setColour (palette::line);
            g.fillRect (x, area.getY(), 1.0f, area.getHeight());
            g.setColour (palette::textDim);
            g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz),
                        juce::Rectangle<float> (x - 20.0f, area.getBottom() + 1.0f, 40.0f, (float) fonts::scaled (13)), juce::Justification::centred);
        }

        for (float db : { -12.0f, -24.0f, -48.0f, -72.0f })
        {
            const auto y = yFor (db);
            g.setColour (palette::line);
            g.fillRect (area.getX(), y, area.getWidth(), 1.0f);
            g.setColour (palette::textDim);
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (0.0f, y - 6.0f, area.getX() - 4.0f, 12.0f), juce::Justification::centredRight);
        }

        auto makePath = [&] (const std::vector<float>& values, bool closed)
        {
            juce::Path p;
            const auto binHz = rate / spectrum.getSize();
            bool started = false;
            float lastX = -10.0f;

            for (size_t i = 1; i < values.size(); ++i)
            {
                const auto hz = (double) i * binHz;
                if (hz < 20.0 || hz > 20000.0)
                    continue;

                const auto x = xFor (hz);
                if (x - lastX < 1.5f && i + 1 < values.size())
                    continue; // many bins share a pixel at high frequencies

                const auto y = juce::jlimit (area.getY(), area.getBottom(), yFor (values[i]));
                if (! started)
                {
                    p.startNewSubPath (x, closed ? area.getBottom() : y);
                    if (closed)
                        p.lineTo (x, y);
                    started = true;
                }
                else
                {
                    p.lineTo (x, y);
                }

                lastX = x;
            }

            if (closed && started)
            {
                p.lineTo (lastX, area.getBottom());
                p.closeSubPath();
            }

            return p;
        };

        g.setColour (palette::accent.withAlpha (0.25f));
        g.fillPath (makePath (smoothed, true));
        g.setColour (palette::accent);
        g.strokePath (makePath (smoothed, false), juce::PathStrokeType (1.5f));
        g.setColour (palette::playhead.withAlpha (0.6f));
        g.strokePath (makePath (peaks, false), juce::PathStrokeType (1.0f));

        if (! active)
        {
            g.setColour (palette::textDim);
            g.setFont (fonts::sans (12.0f));
            g.drawText (TRANS ("Spectrum: play the file to see its frequencies"), area.toNearestInt(), juce::Justification::centred);
        }
    }
}
