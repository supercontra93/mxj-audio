// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "dsp/spectral/Stft.h"

namespace mxj::dsp
{
    /** A spectral selection: a time range and a frequency band (master plan,
        module F). An empty band (high <= low) means all frequencies. */
    struct SpectralRegion
    {
        FrameRange time;
        double lowHz = 0.0, highHz = 0.0;

        bool hasBand() const noexcept { return highHz > lowHz; }
    };

    /** Editing inside a spectral selection. Each function returns new audio for
        spectralEditRange() (the selection plus one window on each side, so the
        change fades in and out without clicks); everything outside the region
        is left as it was. Call on a background thread; an empty sequence
        (0 channels) means cancelled or a write error. */
    namespace spectral
    {
        /** The range the functions below rewrite. */
        FrameRange editRange (const AudioSequence& audio, FrameRange time);

        /** Raises or lowers the region by `gainDb` (-100 or lower = silence). */
        AudioSequence changeGain (const AudioSequence& audio, const SpectralRegion& region, float gainDb,
                                  BlockStore& store, const WorkControl& control = {});

        /** Heals the region from its surroundings (master plan: "çevresinden
            doldurarak iyileştirme"): every frequency is brought to the level it has
            just before and just after the region. Louder sound (a cough, a click,
            a phone ring) is lowered to that level; a hole (a dropout) is filled. */
        AudioSequence heal (const AudioSequence& audio, const SpectralRegion& region,
                            BlockStore& store, const WorkControl& control = {});

        /** Takes the region from `processed` and the rest from `original` (an
            effect applied to a frequency band only). `processed` must be the same
            timeline as `original` with the edit range processed. */
        AudioSequence blend (const AudioSequence& original, const AudioSequence& processed, const SpectralRegion& region,
                             BlockStore& store, const WorkControl& control = {});

        /** How much a window centred at `centre` and a bin at `hz` belong to the
            region: 1 inside, 0 well outside, soft edges (one window in time,
            two bins in frequency). */
        float weight (const SpectralRegion& region, juce::int64 centre, double hz, int windowSize, double binHz);
    }
}
