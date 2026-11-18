// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/audio/AudioSequence.h"
#include "core/edit/AudioState.h"

#include <vector>

namespace mxj
{
    /** One point of an automation curve: a value at a position of the session. */
    struct AutomationPoint
    {
        juce::int64 frame = 0;
        float value = 0.0f;

        bool operator== (const AutomationPoint& o) const { return frame == o.frame && juce::exactlyEqual (value, o.value); }
    };

    /** An automation curve (master plan, module D): points joined by straight
        lines, sorted by frame. Before the first point the curve holds the first
        value, after the last point the last value. No points = not automated:
        the mixer control (fader, pan knob) is used instead. */
    struct AutomationLane
    {
        std::vector<AutomationPoint> points;

        bool isActive() const noexcept { return ! points.empty(); }

        /** The curve's value at `frame`, or `fallback` if there are no points. */
        float valueAt (juce::int64 frame, float fallback) const noexcept;

        /** Adds a point (replacing one at the same frame). Returns its index. */
        int add (AutomationPoint point);

        bool operator== (const AutomationLane& o) const { return points == o.points; }
    };

    //==============================================================================
    /** A piece of audio placed on a track (master plan, module D: "klipler").

        The clip plays `length` frames of `audio`, starting at `sourceStart`,
        at `position` on the session's time line. The audio itself never
        changes: moving, trimming and splitting only change these numbers
        (non-destructive editing, CLAUDE.md rule 6).
    */
    struct Clip
    {
        int id = 0;
        juce::String name;
        AudioSequence audio;            ///< everything the clip may show; at the session's sample rate
        juce::int64 sourceStart = 0;    ///< first frame of `audio` that plays
        juce::int64 length = 0;         ///< frames that play
        juce::int64 position = 0;       ///< where the clip starts in the session
        float gainDb = 0.0f;            ///< clip volume
        juce::int64 fadeIn = 0;         ///< frames
        juce::int64 fadeOut = 0;        ///< frames
        bool muted = false;

        juce::int64 getEnd() const noexcept { return position + length; }
        FrameRange getRange() const noexcept { return { position, position + length }; }

        /** Frames of audio that may still be revealed on each side by trimming. */
        juce::int64 getHeadroomStart() const noexcept { return sourceStart; }
        juce::int64 getHeadroomEnd() const noexcept   { return audio.getNumFrames() - (sourceStart + length); }
    };

    /** A post-fader send from a track to a bus. */
    struct Send
    {
        int busId = 0;
        float levelDb = 0.0f;
    };

    /** An audio track, a bus (master plan: "ara kanal") or the master output.

        Audio tracks hold clips and play into their output (the master or a
        bus) and their sends. Buses mix what is sent to them and play into the
        master. The master plays to the speakers and into a mixdown. */
    struct Track
    {
        enum class Kind { audio, bus, master };

        int id = 0;
        Kind kind = Kind::audio;
        juce::String name;
        int colour = 0;                 ///< index into the theme's track colours
        float volumeDb = 0.0f;
        float pan = 0.0f;               ///< -1 left ... +1 right
        bool mute = false;
        bool solo = false;
        bool armed = false;             ///< ready to record (audio tracks)
        int outputId = 0;               ///< bus id, 0 = master (audio tracks)
        std::vector<Send> sends;
        juce::var effects;              ///< the track's effect chain as JSON (dsp::chainToVar); void = none
        AutomationLane volumeAutomation; ///< dB
        AutomationLane panAutomation;    ///< -1 ... +1
        bool showAutomation = false;
        std::vector<Clip> clips;        ///< sorted by position

        bool isAudio() const noexcept   { return kind == Kind::audio; }
        bool isBus() const noexcept     { return kind == Kind::bus; }

        /** End of the last clip. */
        juce::int64 getEnd() const noexcept;

        const Clip* findClip (int clipId) const;
        Clip* findClip (int clipId);

        /** The clip that plays at `frame` (the later one where two overlap), or nullptr. */
        const Clip* clipAt (juce::int64 frame) const;

        /** Keeps the clips sorted by position. */
        void sortClips();
    };

    //==============================================================================
    /** A multitrack session (master plan, module D): tracks with clips, buses
        and the master, all at one sample rate. A value: copying it copies block
        lists, never audio, so every undo step keeps the whole session.
        Saved as a .mxj project (see core/session/SessionFile.h). */
    struct Session
    {
        Session();

        double sampleRate = 48000.0;
        std::vector<Track> tracks;      ///< audio tracks and buses, top to bottom
        Track master;
        MarkerList markers;
        int nextId = 1;                 ///< for new tracks and clips; ids are never reused

        int createId() noexcept         { return nextId++; }

        /** End of the last clip, in frames. */
        juce::int64 getLength() const noexcept;

        int getNumAudioTracks() const noexcept;

        /** A track or bus by id; the master has id 0... use `master` directly. */
        const Track* findTrack (int trackId) const;
        Track* findTrack (int trackId);
        int indexOfTrack (int trackId) const;

        /** The track holding a clip, or nullptr. */
        const Track* findTrackOfClip (int clipId) const;
        Track* findTrackOfClip (int clipId);
        const Clip* findClip (int clipId) const;

        /** True if any track or bus is soloed. */
        bool hasSolo() const noexcept;

        /** True if the track (or bus) is heard, given mute and solo of all tracks:
            with something soloed, only soloed tracks, buses fed by a soloed
            track and tracks feeding a soloed bus are heard. */
        bool isAudible (const Track&) const;

        /** True if `track` plays into the bus (as its output or through a send). */
        static bool feeds (const Track& track, int busId);
    };
}
