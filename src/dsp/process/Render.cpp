// SPDX-License-Identifier: AGPL-3.0-only
#include "dsp/process/Render.h"
#include "dsp/convert/Resample.h"

#include <cmath>

namespace mxj::dsp
{
    namespace
    {
        constexpr int chunk = 1 << 13;
    }

    //==============================================================================
    SequenceWriter::SequenceWriter (BlockStore& store, int numChannels, double sampleRate, juce::int64 expectedFrames)
        : writer (store.createWriter (numChannels, sampleRate)),
          peaks (std::make_shared<PeakCache> (numChannels, juce::jmax ((juce::int64) 0, expectedFrames))),
          channels (numChannels), rate (sampleRate)
    {
        failed = writer == nullptr;
    }

    bool SequenceWriter::append (const float* const* data, int numFrames)
    {
        if (failed || numFrames <= 0)
            return ! failed;

        if (! writer->append (data, numFrames))
        {
            failed = true;
            return false;
        }

        peaks->append (data, numFrames);
        return true;
    }

    AudioSequence SequenceWriter::finish()
    {
        if (writer == nullptr)
            return {};

        if (peaks->getFramesReady() < peaks->getTotalFrames() || writer->getNumFramesWritten() == 0)
            peaks->finishEarly();

        auto source = writer->finish();
        if (failed || source == nullptr)
            return {};

        if (source->getNumFrames() == 0)
            return AudioSequence (channels, rate);

        source->setPeaks (peaks);
        return AudioSequence::fromSource (source);
    }

    //==============================================================================
    AudioSequence render (const AudioSequence& input, ProcessorChain& chain, BlockStore& store,
                          const AudioSequence& following, const WorkControl& control)
    {
        const auto channels = input.getNumChannels();
        const auto length = input.getNumFrames();
        if (channels <= 0)
            return {};

        chain.prepare (input.getSampleRate(), chunk, channels);
        const auto latency = (juce::int64) chain.getLatencyFrames();

        SequenceWriter out (store, channels, input.getSampleRate(), length);
        juce::AudioBuffer<float> buffer (channels, chunk);
        juce::int64 readPos = 0, toSkip = latency, written = 0;

        while (written < length)
        {
            if (control.cancelled())
            {
                out.finish();
                return {};
            }

            const auto n = (int) juce::jmin ((juce::int64) chunk, length + latency - readPos);
            buffer.clear();

            // The input, then the audio after it (or silence) to push out the look-ahead.
            const auto fromInput = (int) juce::jlimit ((juce::int64) 0, (juce::int64) n, length - readPos);
            if (fromInput > 0)
                input.read (buffer.getArrayOfWritePointers(), channels, readPos, fromInput);

            if (fromInput < n && ! following.isEmpty() && following.hasSameFormat (input))
            {
                float* rest[64];
                for (int c = 0; c < juce::jmin (channels, 64); ++c)
                    rest[c] = buffer.getWritePointer (c, fromInput);

                following.read (rest, channels, readPos + fromInput - length, n - fromInput);
            }

            chain.process (buffer.getArrayOfWritePointers(), channels, n);
            readPos += n;

            const auto skip = (int) juce::jmin ((juce::int64) n, toSkip);
            toSkip -= skip;
            const auto count = (int) juce::jmin ((juce::int64) (n - skip), length - written);

            if (count > 0)
            {
                const float* pointers[64];
                for (int c = 0; c < juce::jmin (channels, 64); ++c)
                    pointers[c] = buffer.getReadPointer (c, skip);

                if (! out.append (pointers, count))
                {
                    out.finish();
                    return {};
                }

                written += count;
            }

            control.report ((double) written / (double) juce::jmax ((juce::int64) 1, length));

            if (n <= 0)
                break;
        }

        return out.finish();
    }

    AudioSequence generate (const EffectSettings& settings, int channels, double rate, juce::int64 numFrames,
                            BlockStore& store, const WorkControl& control)
    {
        const auto* type = catalogue::find (settings.typeId);
        if (type == nullptr || type->create == nullptr || channels <= 0 || rate <= 0.0)
            return {};

        auto values = type->createValues();
        values->setAll (settings.values);
        auto processor = type->create (values);
        processor->prepare (rate, chunk, channels);

        SequenceWriter out (store, channels, rate, numFrames);
        juce::AudioBuffer<float> buffer (channels, chunk);
        juce::int64 done = 0;

        while (done < numFrames)
        {
            if (control.cancelled())
            {
                out.finish();
                return {};
            }

            const auto n = (int) juce::jmin ((juce::int64) chunk, numFrames - done);
            buffer.clear();
            processor->process (buffer.getArrayOfWritePointers(), channels, n);

            if (! out.append (buffer.getArrayOfReadPointers(), n))
            {
                out.finish();
                return {};
            }

            done += n;
            control.report ((double) done / (double) juce::jmax ((juce::int64) 1, numFrames));
        }

        return out.finish();
    }

    AudioSequence reverse (const AudioSequence& input, BlockStore& store, const WorkControl& control)
    {
        const auto channels = input.getNumChannels();
        const auto length = input.getNumFrames();
        if (channels <= 0)
            return {};

        SequenceWriter out (store, channels, input.getSampleRate(), length);
        juce::AudioBuffer<float> buffer (channels, chunk);
        juce::int64 done = 0;

        while (done < length)
        {
            if (control.cancelled())
            {
                out.finish();
                return {};
            }

            const auto n = (int) juce::jmin ((juce::int64) chunk, length - done);
            input.read (buffer.getArrayOfWritePointers(), channels, length - done - n, n);

            for (int c = 0; c < channels; ++c)
                std::reverse (buffer.getWritePointer (c), buffer.getWritePointer (c) + n);

            if (! out.append (buffer.getArrayOfReadPointers(), n))
            {
                out.finish();
                return {};
            }

            done += n;
            control.report ((double) done / (double) juce::jmax ((juce::int64) 1, length));
        }

        return out.finish();
    }

    AudioSequence applyChannelGains (const AudioSequence& input, const std::vector<float>& gains, BlockStore& store,
                                     const WorkControl& control)
    {
        const auto channels = input.getNumChannels();
        const auto length = input.getNumFrames();
        if (channels <= 0)
            return {};

        SequenceWriter out (store, channels, input.getSampleRate(), length);
        juce::AudioBuffer<float> buffer (channels, chunk);
        juce::int64 done = 0;

        while (done < length)
        {
            if (control.cancelled())
            {
                out.finish();
                return {};
            }

            const auto n = (int) juce::jmin ((juce::int64) chunk, length - done);
            input.read (buffer.getArrayOfWritePointers(), channels, done, n);

            for (int c = 0; c < channels; ++c)
                if (c < (int) gains.size())
                    buffer.applyGain (c, 0, n, gains[(size_t) c]);

            if (! out.append (buffer.getArrayOfReadPointers(), n))
            {
                out.finish();
                return {};
            }

            done += n;
            control.report ((double) done / (double) juce::jmax ((juce::int64) 1, length));
        }

        return out.finish();
    }

    AudioSequence changeSpeed (const AudioSequence& input, double speedPercent, BlockStore& store, const WorkControl& control)
    {
        if (input.getNumChannels() <= 0 || speedPercent <= 0.0)
            return {};

        if (std::abs (speedPercent - 100.0) < 1.0e-6)
            return input;

        // Resampling to rate * 100 / speed and playing it at the original rate
        // changes speed and pitch together, like a tape played faster.
        const auto rate = input.getSampleRate();
        return resample (input, rate * 100.0 / speedPercent, store, control.shouldContinue, control.progress, rate);
    }
}
