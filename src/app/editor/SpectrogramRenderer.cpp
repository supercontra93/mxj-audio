// SPDX-License-Identifier: AGPL-3.0-only
#include "app/editor/SpectrogramRenderer.h"
#include "app/theme/Palette.h"

#include <array>

namespace mxj
{
    namespace
    {
        constexpr float floorDb = -110.0f, topDb = -5.0f;
        constexpr int maxChannels = 16;

        template <size_t N>
        juce::Colour fromStops (const juce::Colour (&stops)[N], float level)
        {
            const auto pos = juce::jlimit (0.0f, 1.0f, level) * (float) (N - 1);
            const auto i = juce::jmin ((int) N - 2, (int) pos);
            return stops[i].interpolatedWith (stops[i + 1], pos - (float) i);
        }
    }

    bool SpectrogramRenderer::Request::isSameAs (const Request& o) const
    {
        return audio.isSameAs (o.audio) && juce::approximatelyEqual (startFrame, o.startFrame)
            && juce::approximatelyEqual (framesPerPixel, o.framesPerPixel) && width == o.width && height == o.height
            && fftSize == o.fftSize && scale == o.scale && colours == o.colours;
    }

    juce::Colour SpectrogramRenderer::colourFor (const juce::String& colours, float level)
    {
        if (colours == "gray")    return fromStops (palette::spectrogram::gray, level);
        if (colours == "rainbow") return fromStops (palette::spectrogram::rainbow, level);
        return fromStops (palette::spectrogram::amber, level);
    }

    SpectrogramRenderer::SpectrogramRenderer() : juce::Thread ("Spectrogram")
    {
        startThread (juce::Thread::Priority::low);
    }

    SpectrogramRenderer::~SpectrogramRenderer()
    {
        signalThreadShouldExit();
        ++generation;
        notify();
        stopThread (5000);
        cancelPendingUpdate();
    }

    void SpectrogramRenderer::request (const Request& r)
    {
        if (requestedAny && r.isSameAs (lastRequested))
            return;

        lastRequested = r;
        requestedAny = true;

        {
            const juce::ScopedLock sl (lock);
            wanted = r;
            hasWanted = true;
        }

        ++generation; // stops the picture being made
        notify();
    }

    void SpectrogramRenderer::clear()
    {
        {
            const juce::ScopedLock sl (lock);
            hasWanted = false;
            hasReady = false;
        }

        ++generation;
        requestedAny = false;
        picture = {};
        complete = {};
    }

    void SpectrogramRenderer::run()
    {
        while (! threadShouldExit())
        {
            Request r;
            {
                const juce::ScopedLock sl (lock);
                if (hasWanted)
                {
                    r = wanted;
                    hasWanted = false;
                }
            }

            if (r.width <= 0 || r.height <= 0 || r.audio.getNumChannels() <= 0)
            {
                wait (-1);
                continue;
            }

            render (r);
        }
    }

    bool SpectrogramRenderer::render (const Request& r)
    {
        const int myGeneration = generation.load();
        const auto channels = juce::jmin (r.audio.getNumChannels(), maxChannels);
        const int order = juce::jlimit (8, 14, (int) std::lround (std::log2 ((double) juce::jmax (256, r.fftSize))));
        dsp::SpectrumAnalyser analyser (order);
        const int n = analyser.getSize();
        const int numBins = analyser.getNumBins();
        const auto rate = r.audio.getSampleRate();
        const dsp::FrequencyScale scale { r.scale, rate * 0.5 };

        Picture result;
        result.request = r;
        for (int c = 0; c < channels; ++c)
            result.channels.emplace_back (juce::Image::RGB, r.width, r.height, true, juce::SoftwareImageType());

        // For each row: the bins it covers.
        std::vector<int> rowLow ((size_t) r.height), rowHigh ((size_t) r.height);
        const auto binHz = rate / n;
        for (int y = 0; y < r.height; ++y)
        {
            const auto top = scale.fromProportion (1.0 - (double) y / r.height);
            const auto bottom = scale.fromProportion (1.0 - (double) (y + 1) / r.height);
            rowLow[(size_t) y] = juce::jlimit (0, numBins - 1, (int) std::floor (bottom / binHz + 0.5));
            rowHigh[(size_t) y] = juce::jlimit (rowLow[(size_t) y], numBins - 1, (int) std::floor (top / binHz + 0.5));
        }

        // Colour table.
        std::array<juce::PixelRGB, 256> table;
        for (int i = 0; i < 256; ++i)
        {
            const auto colour = colourFor (r.colours, (float) i / 255.0f);
            table[(size_t) i].setARGB (255, colour.getRed(), colour.getGreen(), colour.getBlue());
        }

        juce::AudioBuffer<float> samples (channels, n);
        std::vector<float> power ((size_t) numBins), best ((size_t) (channels * numBins));
        std::vector<std::unique_ptr<juce::Image::BitmapData>> bitmaps;
        for (auto& image : result.channels)
            bitmaps.push_back (std::make_unique<juce::Image::BitmapData> (image, juce::Image::BitmapData::writeOnly));

        const auto length = r.audio.getNumFrames();
        // Zoomed out, one column covers many windows: look at up to four of them.
        const int looks = juce::jlimit (1, 4, (int) (r.framesPerPixel / (n / 2)));
        auto lastPost = juce::Time::getMillisecondCounter();

        for (int x = 0; x < r.width; ++x)
        {
            if (generation.load() != myGeneration || threadShouldExit())
            {
                bitmaps.clear();
                return false;
            }

            const auto columnStart = r.startFrame + x * r.framesPerPixel;
            std::fill (best.begin(), best.end(), -200.0f);
            bool any = false;

            for (int l = 0; l < looks; ++l)
            {
                const auto centre = (juce::int64) (columnStart + (l + 0.5) * r.framesPerPixel / looks);
                if (centre < 0 || centre >= length)
                    continue;

                any = true;
                samples.clear();
                const auto from = juce::jmax ((juce::int64) 0, centre - n / 2);
                const auto to = juce::jmin (length, centre + n / 2);
                float* pointers[maxChannels];
                for (int c = 0; c < channels; ++c)
                    pointers[c] = samples.getWritePointer (c, (int) (from - (centre - n / 2)));
                r.audio.read (pointers, channels, from, (int) (to - from));

                for (int c = 0; c < channels; ++c)
                {
                    analyser.compute (samples.getReadPointer (c), power.data());
                    auto* b = best.data() + c * numBins;
                    for (int k = 0; k < numBins; ++k)
                        b[k] = juce::jmax (b[k], power[(size_t) k]);
                }
            }

            for (int c = 0; c < channels; ++c)
            {
                auto& bitmap = *bitmaps[(size_t) c];
                const auto* b = best.data() + c * numBins;

                for (int y = 0; y < r.height; ++y)
                {
                    auto* pixel = reinterpret_cast<juce::PixelRGB*> (bitmap.getPixelPointer (x, y));
                    if (! any)
                    {
                        *pixel = table[0];
                        continue;
                    }

                    float v = -200.0f;
                    for (int k = rowLow[(size_t) y]; k <= rowHigh[(size_t) y]; ++k)
                        v = juce::jmax (v, b[k]);

                    const auto level = (v - floorDb) / (topDb - floorDb);
                    *pixel = table[(size_t) juce::jlimit (0, 255, (int) (level * 255.0f))];
                }
            }

            // Show the picture growing while it is drawn.
            const auto now = juce::Time::getMillisecondCounter();
            if (now - lastPost > 150 && x + 1 < r.width)
            {
                lastPost = now;
                Picture partial;
                partial.request = r;
                partial.columnsDone = x + 1;
                for (auto& image : result.channels)
                    partial.channels.push_back (image.createCopy());

                const juce::ScopedLock sl (lock);
                ready = std::move (partial);
                hasReady = true;
                triggerAsyncUpdate();
            }
        }

        bitmaps.clear();

        if (generation.load() != myGeneration)
            return false;

        result.columnsDone = r.width;
        const juce::ScopedLock sl (lock);
        ready = std::move (result);
        hasReady = true;
        triggerAsyncUpdate();
        return true;
    }

    void SpectrogramRenderer::handleAsyncUpdate()
    {
        {
            const juce::ScopedLock sl (lock);
            if (! hasReady)
                return;

            picture = std::move (ready);
            ready = {};
            hasReady = false;
        }

        if (picture.isComplete())
            complete = picture;

        if (onPictureChanged != nullptr)
            onPictureChanged();
    }
}
