// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/edit/AudioState.h"
#include "core/audio/DerivedSources.h"

namespace mxj
{
    /** The editing operations of the waveform editor, as pure functions: each
        takes a state and returns the new state (plus the selection to show
        afterwards). They never touch audio data, so each costs time in
        proportion to the number of blocks only. The app records the state
        before and after in the undo history.

        Markers follow the audio: markers inside removed audio are removed,
        markers after an edit move with the audio behind them.
    */
    namespace edits
    {
        struct Result
        {
            AudioState state;
            FrameRange selection; ///< what to select afterwards (empty = cursor at start)
            bool changed = false;
        };

        /** Removes the range (Cut and Delete). */
        Result erase (const AudioState&, FrameRange range);

        /** Keeps only the range (Trim / Crop). */
        Result trim (const AudioState&, FrameRange range);

        /** Inserts `clip` at `position`, or replaces `range` by it when the range
            is not empty (Paste). The clip is converted to the state's channel
            count; it must have the same sample rate. */
        Result paste (const AudioState&, FrameRange range, const AudioSequence& clip);

        /** Adds `clip` to the audio starting at range.getStart() (Mix Paste). The
            audio grows if the clip runs past its end. */
        Result mixPaste (const AudioState&, FrameRange range, const AudioSequence& clip);

        /** Replaces the range by silence of the same length. */
        Result silence (const AudioState&, FrameRange range);

        /** Inserts `numFrames` of silence at the start of the range (replacing the range, if any). */
        Result insertSilence (const AudioState&, FrameRange range, juce::int64 numFrames);

        /** Fades the range in from silence, or out to silence. */
        Result fadeIn (const AudioState&, FrameRange range);
        Result fadeOut (const AudioState&, FrameRange range);

        /** Multiplies the range by a constant gain (used by tests and later effects). */
        Result gain (const AudioState&, FrameRange range, float gainFactor);

        /** Replaces the range by processed audio (an applied effect). The new audio
            may have another length (speed change): markers inside the range then
            move in proportion, markers after it move with the audio. It must have
            the state's channel count and sample rate. The result selects the new audio. */
        Result replaceRange (const AudioState&, FrameRange range, const AudioSequence& processed);

        //==============================================================================
        /** Adds a marker (kept sorted). Returns the index of the new marker. */
        size_t addMarker (MarkerList&, Marker);

        /** The default name of a new marker: "Marker 1", "Marker 2", ... (translated). */
        juce::String nextMarkerName (const MarkerList&, const juce::String& baseName);

        /** Markers after removing `length` frames at `start`. */
        MarkerList markersAfterErase (const MarkerList&, juce::int64 start, juce::int64 length);

        /** Markers after inserting `length` frames at `position`. */
        MarkerList markersAfterInsert (const MarkerList&, juce::int64 position, juce::int64 length);
    }
}
