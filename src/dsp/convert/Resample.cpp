// SPDX-License-Identifier: AGPL-3.0-only
#include "dsp/convert/Resample.h"

#include <cmath>

namespace mxj::dsp
{
    AudioSequence resample (const AudioSequence& input, double newRate, BlockStore& store,
                            const std::function<bool()>& shouldContinue, std::atomic<double>* progress)
    {
        const auto channels = input.getNumChannels();
        if (channels <= 0 || newRate <= 0 || input.getSampleRate() <= 0)
            return {};

        if (juce::approximatelyEqual (input.getSampleRate(), newRate))
            return input;

        const auto ratio = input.getSampleRate() / newRate; // input frames per output frame
        const auto outputFrames = (juce::int64) std::llround ((double) input.getNumFrames() / ratio);

        // The interpolator delays its output by this many input frames; drop them.
        const auto skip = (int) std::llround (juce::WindowedSincInterpolator::getBaseLatency() / ratio);

        constexpr int chunk = 1 << 14;
        std::vector<juce::WindowedSincInterpolator> interpolators ((size_t) channels);
        juce::AudioBuffer<float> in (channels, (int) std::ceil (chunk * ratio) + 16);
        juce::AudioBuffer<float> out (channels, chunk);

        auto writer = store.createWriter (channels, newRate);
        auto peaks = std::make_shared<PeakCache> (channels, outputFrames);

        juce::int64 readPos = 0, produced = 0;
        int toSkip = skip;

        while (produced < outputFrames)
        {
            if (shouldContinue != nullptr && ! shouldContinue())
            {
                writer->finish();
                return {};
            }

            const auto wanted = (int) juce::jmin ((juce::int64) chunk, outputFrames - produced + toSkip);
            const auto inputFrames = (int) std::ceil (wanted * ratio) + 8;
            in.setSize (channels, inputFrames, false, false, true);
            input.read (in.getArrayOfWritePointers(), channels, readPos, inputFrames);

            int used = 0;
            for (int c = 0; c < channels; ++c)
                used = interpolators[(size_t) c].process (ratio, in.getReadPointer (c), out.getWritePointer (c), wanted, inputFrames, 0);
            readPos += used;

            const auto dropped = juce::jmin (toSkip, wanted);
            toSkip -= dropped;
            const auto count = wanted - dropped;
            if (count <= 0)
                continue;

            const float* pointers[64];
            for (int c = 0; c < juce::jmin (channels, 64); ++c)
                pointers[c] = out.getReadPointer (c, dropped);

            if (! writer->append (pointers, count))
            {
                writer->finish();
                return {};
            }

            peaks->append (pointers, count);
            produced += count;

            if (progress != nullptr)
                progress->store ((double) produced / (double) juce::jmax ((juce::int64) 1, outputFrames));
        }

        if (outputFrames == 0)
            peaks->finishEarly();

        auto source = writer->finish();
        if (source == nullptr)
            return {};

        source->setPeaks (peaks);
        return AudioSequence::fromSource (source);
    }
}
