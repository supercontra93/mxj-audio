// SPDX-License-Identifier: AGPL-3.0-only
#include "dsp/spectral/Stft.h"

#include <cmath>

namespace mxj::dsp
{
    namespace stft
    {
        namespace
        {
            constexpr int maxChannels = 64;

            /** Reads [start, start + n) where parts outside the audio are silent. */
            void readPadded (const AudioSequence& audio, juce::AudioBuffer<float>& dest, juce::int64 start, int n)
            {
                dest.clear();
                const auto length = audio.getNumFrames();
                const auto from = juce::jmax ((juce::int64) 0, start);
                const auto to = juce::jmin (length, start + n);
                if (to <= from)
                    return;

                float* pointers[maxChannels];
                const auto channels = juce::jmin (dest.getNumChannels(), maxChannels);
                for (int c = 0; c < channels; ++c)
                    pointers[c] = dest.getWritePointer (c, (int) (from - start));

                audio.read (pointers, channels, from, (int) (to - from));
            }
        }

        int orderForRate (double sampleRate)
        {
            return sampleRate > 100000.0 ? 13 : sampleRate > 64000.0 ? 12 : sampleRate < 24000.0 ? 10 : 11;
        }

        AudioSequence process (const AudioSequence& audio, FrameRange range, int order, const Modify& modify,
                               BlockStore& store, const WorkControl& control, const AudioSequence* other)
        {
            const auto channels = juce::jmin (audio.getNumChannels(), maxChannels);
            range = range.getIntersectionWith ({ 0, audio.getNumFrames() });
            if (channels <= 0 || range.isEmpty())
                return {};

            if (other != nullptr && ! other->hasSameFormat (audio))
                other = nullptr;

            RealFft fft (order);
            const int n = fft.getSize();
            const int hop = n / 4;
            const int numBins = fft.getNumBins();
            const auto window = hannWindow (n);
            const auto binHz = audio.getSampleRate() / n;
            const float norm = 1.0f / 1.5f; // sum of the squared Hann windows at hop n / 4

            const int chunk = hop * 64;
            juce::AudioBuffer<float> input (channels, chunk + 2 * n), otherInput (channels, other != nullptr ? chunk + 2 * n : 1),
                                     output (channels, chunk + 2 * n);
            std::vector<float> frame ((size_t) n);
            std::vector<std::complex<float>> bins ((size_t) numBins), otherBins ((size_t) numBins);

            SequenceWriter out (store, channels, audio.getSampleRate(), range.getLength());

            for (auto chunkStart = range.getStart(); chunkStart < range.getEnd(); chunkStart += chunk)
            {
                if (control.cancelled())
                {
                    out.finish();
                    return {};
                }

                const auto chunkLength = (int) juce::jmin ((juce::int64) chunk, range.getEnd() - chunkStart);
                const auto spanStart = chunkStart - n; // buffers cover [chunkStart - n, chunkStart + chunk + n)
                readPadded (audio, input, spanStart, input.getNumSamples());
                if (other != nullptr)
                    readPadded (*other, otherInput, spanStart, otherInput.getNumSamples());

                output.clear();

                // Windows start on a grid that begins at range.getStart() - (n - hop).
                const auto gridOrigin = range.getStart() - (n - hop);
                auto first = chunkStart - n + 1;
                const auto offset = ((first - gridOrigin) % hop + hop) % hop;
                if (offset != 0)
                    first += hop - offset;

                for (auto frameStart = first; frameStart < chunkStart + chunkLength; frameStart += hop)
                {
                    const auto pos = (int) (frameStart - spanStart);

                    for (int c = 0; c < channels; ++c)
                    {
                        const auto* src = input.getReadPointer (c, pos);
                        for (int i = 0; i < n; ++i)
                            frame[(size_t) i] = src[i] * window[(size_t) i];
                        fft.forward (frame.data(), bins.data());

                        if (other != nullptr)
                        {
                            const auto* o = otherInput.getReadPointer (c, pos);
                            for (int i = 0; i < n; ++i)
                                frame[(size_t) i] = o[i] * window[(size_t) i];
                            fft.forward (frame.data(), otherBins.data());
                        }

                        Frame f;
                        f.channel = c;
                        f.centre = frameStart + n / 2;
                        f.bins = bins.data();
                        f.other = other != nullptr ? otherBins.data() : nullptr;
                        f.numBins = numBins;
                        f.binHz = binHz;
                        modify (f);

                        bins[0] = { bins[0].real(), 0.0f };
                        bins[(size_t) numBins - 1] = { bins[(size_t) numBins - 1].real(), 0.0f };
                        fft.inverse (bins.data(), frame.data());

                        auto* dst = output.getWritePointer (c, pos);
                        for (int i = 0; i < n; ++i)
                            dst[i] += frame[(size_t) i] * window[(size_t) i] * norm;
                    }
                }

                const float* pointers[maxChannels];
                for (int c = 0; c < channels; ++c)
                    pointers[c] = output.getReadPointer (c, n);

                if (! out.append (pointers, chunkLength))
                {
                    out.finish();
                    return {};
                }

                control.report ((double) (chunkStart + chunkLength - range.getStart()) / (double) range.getLength());
            }

            return out.finish();
        }

        std::vector<std::vector<float>> averagePower (const AudioSequence& audio, FrameRange range, int order,
                                                      const WorkControl& control)
        {
            const auto channels = juce::jmin (audio.getNumChannels(), maxChannels);
            RealFft fft (order);
            const int n = fft.getSize();
            const int numBins = fft.getNumBins();
            std::vector<std::vector<float>> result ((size_t) juce::jmax (0, channels), std::vector<float> ((size_t) numBins, 0.0f));

            range = range.getIntersectionWith ({ 0, audio.getNumFrames() });
            if (channels <= 0 || range.isEmpty())
                return result;

            const auto window = hannWindow (n);
            float energy = 0.0f;
            for (auto w : window)
                energy += w * w;

            // Windows every half window inside the range; a short range gets one window around its middle.
            std::vector<juce::int64> starts;
            if (range.getLength() < n)
                starts.push_back (range.getStart() + range.getLength() / 2 - n / 2);
            else
                for (auto s = range.getStart(); s + n <= range.getEnd(); s += n / 2)
                    starts.push_back (s);

            juce::AudioBuffer<float> buffer (channels, n);
            std::vector<float> frame ((size_t) n);
            std::vector<std::complex<float>> bins ((size_t) numBins);

            for (size_t i = 0; i < starts.size(); ++i)
            {
                if (control.cancelled())
                    return {};

                const auto s = starts[i];
                buffer.clear();
                const auto from = juce::jmax (s, range.getStart());
                const auto to = juce::jmin (s + n, range.getEnd());
                float* pointers[maxChannels];
                for (int c = 0; c < channels; ++c)
                    pointers[c] = buffer.getWritePointer (c, (int) (from - s));
                audio.read (pointers, channels, from, (int) (to - from));

                for (int c = 0; c < channels; ++c)
                {
                    const auto* src = buffer.getReadPointer (c);
                    for (int k = 0; k < n; ++k)
                        frame[(size_t) k] = src[k] * window[(size_t) k];
                    fft.forward (frame.data(), bins.data());

                    auto& r = result[(size_t) c];
                    for (int k = 0; k < numBins; ++k)
                        r[(size_t) k] += std::norm (bins[(size_t) k]);
                }

                control.report ((double) (i + 1) / (double) starts.size());
            }

            const auto scale = 1.0f / ((float) starts.size() * energy);
            for (auto& r : result)
                for (auto& v : r)
                    v *= scale;

            return result;
        }
    }

    //==============================================================================
    double FrequencyScale::hzToMel (double hz)  { return 2595.0 * std::log10 (1.0 + hz / 700.0); }
    double FrequencyScale::melToHz (double mel) { return 700.0 * (std::pow (10.0, mel / 2595.0) - 1.0); }

    double FrequencyScale::toProportion (double hz) const
    {
        hz = juce::jlimit (0.0, nyquist, hz);
        switch (kind)
        {
            case Kind::linear:
                return hz / nyquist;
            case Kind::logarithmic:
                if (hz <= lowestLogHz)
                    return 0.0;
                return std::log (hz / lowestLogHz) / std::log (nyquist / lowestLogHz);
            case Kind::mel:
                return hzToMel (hz) / hzToMel (nyquist);
        }

        return 0.0;
    }

    double FrequencyScale::fromProportion (double p) const
    {
        p = juce::jlimit (0.0, 1.0, p);
        switch (kind)
        {
            case Kind::linear:      return p * nyquist;
            case Kind::logarithmic: return p <= 0.0 ? 0.0 : lowestLogHz * std::pow (nyquist / lowestLogHz, p);
            case Kind::mel:         return melToHz (p * hzToMel (nyquist));
        }

        return 0.0;
    }

    FrequencyScale::Kind FrequencyScale::fromName (const juce::String& name)
    {
        if (name == "linear") return Kind::linear;
        if (name == "mel")    return Kind::mel;
        return Kind::logarithmic;
    }

    //==============================================================================
    SpectrumAnalyser::SpectrumAnalyser (int order)
        : fft (order), window (hannWindow (fft.getSize())), buffer ((size_t) fft.getSize()), bins ((size_t) fft.getNumBins())
    {
        // A full-scale sine gives |X| = sum(window) / 2 at its bin.
        float sum = 0.0f;
        for (auto w : window)
            sum += w;
        scale = 2.0f / sum;
    }

    void SpectrumAnalyser::compute (const float* samples, float* powerDb)
    {
        const auto n = fft.getSize();
        for (int i = 0; i < n; ++i)
            buffer[(size_t) i] = samples[i] * window[(size_t) i];

        fft.forward (buffer.data(), bins.data());

        for (int k = 0; k < fft.getNumBins(); ++k)
        {
            const auto power = std::norm (bins[(size_t) k]) * scale * scale;
            powerDb[k] = power > 1.0e-20f ? 10.0f * std::log10 (power) : -200.0f;
        }
    }
}
