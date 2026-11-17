// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/audio/AudioSequence.h"
#include "core/audio/BlockStore.h"

#include <atomic>
#include <functional>

namespace mxj::dsp
{
    /** Converts audio to another sample rate (windowed sinc, as in playback) and
        writes the result into the block store, with its peaks.

        Used when a recording or pasted audio does not have the sample rate of
        the file it goes into. Blocks; run it on a background thread. Returns an
        empty sequence (with `numChannels` 0) if it was cancelled or failed.

        `storedSampleRate` (if > 0) is the sample rate the result is labelled
        with instead of `newSampleRate`: labelling it with the original rate
        changes the speed (see changeSpeed()).
    */
    AudioSequence resample (const AudioSequence& input, double newSampleRate, BlockStore& store,
                            const std::function<bool()>& shouldContinue = nullptr,
                            std::atomic<double>* progress = nullptr,
                            double storedSampleRate = 0.0);
}
