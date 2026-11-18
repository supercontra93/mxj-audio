// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/session/Session.h"

namespace mxj::sessionEdits
{
    /** Edits of a multitrack session. Each works on a Session value; the
        caller keeps the value from before for undo (CLAUDE.md, rule 6).
        Positions are in frames of the session. Pure functions, no audio is read. */

    /** Adds an audio track or a bus at `index` (-1 = at the end). Returns its id. */
    int addTrack (Session&, Track::Kind kind, const juce::String& name, int index = -1);

    /** Removes a track or bus. Tracks that played into a removed bus play into the master. */
    bool removeTrack (Session&, int trackId);

    /** Moves a track to another place in the list. */
    bool moveTrack (Session&, int trackId, int newIndex);

    /** Adds a clip to a track. A clip without an id gets one. Returns the id, or 0. */
    int addClip (Session&, int trackId, Clip clip);

    /** A clip that plays all of `audio` at `position`. */
    Clip makeClip (Session&, const juce::String& name, const AudioSequence& audio, juce::int64 position);

    /** Moves a clip in time (not before 0) and, if `newTrackId` differs, to another audio track. */
    bool moveClip (Session&, int clipId, int newTrackId, juce::int64 newPosition);

    /** Splits a clip at `frame` (strictly inside it). The right part gets a new
        id, which is returned (0 if nothing was split). */
    int splitClip (Session&, int clipId, juce::int64 frame);

    /** Splits every clip of the given tracks (all audio tracks if empty) that
        crosses `frame`. Returns the number of clips split. */
    int splitAt (Session&, juce::int64 frame, const std::vector<int>& trackIds = {});

    /** Moves the start of a clip to `newStart` (its end stays): reveals or hides
        audio, limited to the clip's audio. */
    bool trimClipStart (Session&, int clipId, juce::int64 newStart);

    /** Moves the end of a clip (its start stays), limited to the clip's audio. */
    bool trimClipEnd (Session&, int clipId, juce::int64 newEnd);

    /** Sets the fade lengths of a clip (limited to the clip). */
    bool setClipFades (Session&, int clipId, juce::int64 fadeIn, juce::int64 fadeOut);

    bool setClipGain (Session&, int clipId, float gainDb);

    /** Removes clips. With `ripple`, the later clips of each track move left
        to close the gap (master plan: "kaydırmalı düzenleme"). */
    int deleteClips (Session&, const std::vector<int>& clipIds, bool ripple);

    /** Removes the time range from the given tracks (all if empty): parts of
        clips inside it go away, and with `ripple` everything after it moves left. */
    void deleteRange (Session&, FrameRange range, bool ripple, const std::vector<int>& trackIds = {});

    /** Moves `frame` onto the nearest edge (clip start or end, `extraTargets`,
        zero) within `tolerance` frames. Clips in `ignoreClips` are not targets. */
    juce::int64 snap (const Session&, juce::int64 frame, juce::int64 tolerance,
                      const std::vector<int>& ignoreClips = {}, const std::vector<juce::int64>& extraTargets = {});

    /** Equal-power fade curve used by clip fades and crossfades: 0 -> 0, 1 -> 1. */
    inline float fadeCurve (double t) noexcept
    {
        return (float) std::sin (juce::MathConstants<double>::halfPi * juce::jlimit (0.0, 1.0, t));
    }
}
