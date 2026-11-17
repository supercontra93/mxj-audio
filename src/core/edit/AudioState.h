// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/audio/AudioSequence.h"

#include <vector>

namespace mxj
{
    /** A named point in the audio (master plan: "işaretçiler"). Saved in WAV
        files as cue points with labels. */
    struct Marker
    {
        juce::int64 position = 0;
        juce::String name;

        bool operator== (const Marker& other) const { return position == other.position && name == other.name; }
    };

    /** Markers, always sorted by position. */
    using MarkerList = std::vector<Marker>;

    /** Everything an edit can change in one open file: its audio and its markers.
        A value: copying it copies block lists, never audio, so every undo step
        can keep the whole state before and after the edit (docs/adr/0006). */
    struct AudioState
    {
        AudioSequence audio;
        MarkerList markers;
    };

    /** A range of frames [start, end). Empty when start == end. */
    using FrameRange = juce::Range<juce::int64>;
}
