// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/edit/AudioState.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <unordered_map>

namespace mxj::cueMarkers
{
    /** Reads markers from the cue points and labels of a WAV file, as JUCE's
        WAV reader reports them in metadataValues ("NumCuePoints", "Cue0Offset",
        "CueLabel0Text", ...). Sorted by position. */
    MarkerList read (const juce::StringPairArray& metadata, juce::int64 numFrames);

    /** The metadata values that make JUCE's WAV writer store the markers as cue
        points with labels, which Audition, Audacity, Reaper and others show. */
    std::unordered_map<juce::String, juce::String> toMetadata (const MarkerList& markers);
}
