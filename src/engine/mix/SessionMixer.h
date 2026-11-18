// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/session/Session.h"
#include "dsp/effects/EffectCatalogue.h"
#include "dsp/process/Render.h"
#include "engine/playback/PlaybackSource.h"

#include <map>
#include <mutex>

namespace mxj
{
    /** Mixes a multitrack session (master plan, module D): clips with their
        gain, fades and crossfades, each track's effects, volume and pan (or
        their automation), mute and solo, sends and buses, and the master.

        The same code plays the session and renders the mixdown, so both always
        sound the same.

        Playing: the mixer is the PlaybackSource of the session's PlaybackEngine
        and runs on its disk thread, a little ahead of the audio thread. The
        audio thread itself only copies finished audio (CLAUDE.md, rule 4).
        The message thread describes the session with setSession(); when only
        mixer values changed (faders, pan, mute, solo, sends, effect settings)
        they are handed over through atomics and heard within the look-ahead,
        and effects keep their state. Other changes build a new mix graph,
        which the disk thread takes over between two chunks (keeping the effect
        state of tracks whose effect list did not change).

        All tracks are mixed in stereo; mono clips play on both sides. Pan is a
        balance control (centre = both sides at full level).
    */
    class SessionMixer final : public PlaybackSource
    {
    public:
        SessionMixer();
        ~SessionMixer() override;

        //==============================================================================
        /** What to play. Cheap when only mixer values changed. Message thread. */
        void setSession (const Session& session);

        /** Live changes while a control moves, before the change is recorded in
            the session. Message thread. */
        void setTrackVolume (int trackId, float db);
        void setTrackPan (int trackId, float pan);
        void setSendLevel (int trackId, int busId, float db);

        /** Peak levels (gain, left and right) of a track, bus or the master (id 0)
            that are heard at `frame`, for the meters. Message thread. */
        std::pair<float, float> getLevel (int trackId, juce::int64 frame) const;

        //==============================================================================
        // PlaybackSource (disk thread, except where noted)
        double getSampleRate() const override       { return rate.load(); }
        juce::int64 getLength() const override      { return length.load(); }
        void seek (juce::int64 frame) override;
        void read (float* const* dest, int numFrames) override;

        /** Largest chunk mixed at once. */
        static constexpr int maxChunk = 1024;

        //==============================================================================
        /** Renders `range` of the session (stereo, the session's sample rate)
            into the block store. Background thread. Returns an empty sequence
            (0 channels) if cancelled or on a write error. */
        static AudioSequence mixdown (const Session& session, FrameRange range, BlockStore& store,
                                      const dsp::WorkControl& control = {});

    private:
        struct Graph;
        struct Strip;

        struct EffectState
        {
            juce::StringArray types;
            std::vector<const dsp::EffectType*> typePointers;
            std::vector<dsp::ParameterValuesPtr> values;
            std::vector<std::shared_ptr<std::atomic<bool>>> enabled;
        };

        /** Updates the shared effect values of a track. Returns true if its list
            of effect types changed (a new chain is needed). Message thread. */
        bool updateEffects (int trackId, const juce::var& effects);

        std::shared_ptr<Graph> buildGraph (const Session&);
        void updateValues (const Session&);
        static bool sameStructure (const Session& a, const Session& b);
        Strip* findStrip (int trackId) const;

        /** Disk thread. */
        void adoptPendingGraph();
        void renderChunk (float* const* dest, int numFrames);
        void renderClips (Strip&, juce::int64 start, int numFrames);
        void applyStrip (Strip&, juce::int64 start, int numFrames, float* const* target);

        // Message thread
        Session shown;
        bool hasShown = false;
        std::shared_ptr<Graph> latest;
        std::map<int, EffectState> effectStates;

        // Handed over
        std::mutex pendingLock;
        std::shared_ptr<Graph> pending;
        std::atomic<double> rate { 48000.0 };
        std::atomic<juce::int64> length { 0 };

        // Disk thread
        std::shared_ptr<Graph> current;
        juce::int64 position = 0;
        juce::AudioBuffer<float> clipBuffer { 2, maxChunk };

        JUCE_DECLARE_NON_COPYABLE (SessionMixer)
    };
}
