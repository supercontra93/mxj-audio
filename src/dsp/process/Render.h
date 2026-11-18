// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/audio/AudioSequence.h"
#include "core/audio/BlockStore.h"
#include "dsp/effects/EffectCatalogue.h"

#include <atomic>
#include <functional>

namespace mxj::dsp
{
    /** How long work reports back: `shouldContinue` returns false to cancel,
        `progress` (0..1) is shown by the interface. Both may be empty. */
    struct WorkControl
    {
        std::function<bool()> shouldContinue;
        std::atomic<double>* progress = nullptr;
        double rangeStart = 0.0, rangeEnd = 1.0; ///< the part of `progress` this work fills

        bool cancelled() const { return shouldContinue != nullptr && ! shouldContinue(); }

        void report (double p) const
        {
            if (progress != nullptr)
                progress->store (rangeStart + (rangeEnd - rangeStart) * juce::jlimit (0.0, 1.0, p));
        }

        /** The same control for one step of a longer job, filling [from, to] of this one's range. */
        WorkControl part (double from, double to) const
        {
            const auto span = rangeEnd - rangeStart;
            return { shouldContinue, progress, rangeStart + span * from, rangeStart + span * to };
        }
    };

    /** Writes new audio into the block store, with its peaks, and returns it as
        a sequence. Not thread safe: one writer per thread. */
    class SequenceWriter
    {
    public:
        SequenceWriter (BlockStore& store, int numChannels, double sampleRate, juce::int64 expectedFrames);

        bool append (const float* const* channels, int numFrames);

        /** The written audio, or an empty sequence on a write error. */
        AudioSequence finish();

    private:
        std::unique_ptr<BlockStore::Writer> writer;
        std::shared_ptr<PeakCache> peaks;
        int channels;
        double rate;
        bool failed = false;
    };

    //==============================================================================
    /** Applies effects to audio (master plan: "applying an effect writes new
        blocks"). Runs `chain` over `input` and writes the result into `store`.

        Look-ahead effects delay their output; that delay is removed, feeding the
        chain with `following` (the audio after the input, if any) or silence, so
        the result lines up exactly with the input and has the same length.

        Blocks: call on a background thread. Returns an empty sequence (0
        channels) if cancelled, on a write error or when a processor reports an
        error (chain.getError()).
    */
    AudioSequence render (const AudioSequence& input, ProcessorChain& chain, BlockStore& store,
                          const AudioSequence& following = {}, const WorkControl& control = {});

    /** Generates `numFrames` of new audio with a generator effect (tone, noise).
        On a processor error returns an empty sequence and sets `error`. */
    AudioSequence generate (const EffectSettings& generator, int numChannels, double sampleRate, juce::int64 numFrames,
                            BlockStore& store, const WorkControl& control = {}, juce::String* error = nullptr);

    /** The input played backwards. */
    AudioSequence reverse (const AudioSequence& input, BlockStore& store, const WorkControl& control = {});

    /** Multiplies each channel by its own gain (missing gains = 1). */
    AudioSequence applyChannelGains (const AudioSequence& input, const std::vector<float>& gains, BlockStore& store,
                                     const WorkControl& control = {});

    /** Plays the input `speedPercent` % as fast (pitch changes with it). The
        result keeps the input's sample rate and is shorter or longer. */
    AudioSequence changeSpeed (const AudioSequence& input, double speedPercent, BlockStore& store,
                               const WorkControl& control = {});
}
