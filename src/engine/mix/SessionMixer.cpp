// SPDX-License-Identifier: AGPL-3.0-only
#include "engine/mix/SessionMixer.h"
#include "core/session/SessionEdits.h"

#include <cmath>

namespace mxj
{
    //==============================================================================
    /** One mixer channel strip of the graph: an audio track, a bus or the master. */
    struct SessionMixer::Strip
    {
        struct PlayClip
        {
            AudioSequence audio;
            juce::int64 sourceStart = 0, length = 0, position = 0, fadeIn = 0, fadeOut = 0;
            float gain = 1.0f;
        };

        struct SendTarget
        {
            int busId = 0;
            Strip* bus = nullptr;
            std::atomic<float> levelDb { 0.0f };
            float lastGain = 0.0f; // disk thread
        };

        struct MeterEntry
        {
            std::atomic<juce::int64> frame { -1 };
            std::atomic<float> left { 0.0f }, right { 0.0f };
        };

        int id = 0;
        Track::Kind kind = Track::Kind::audio;
        Strip* output = nullptr; // bus; nullptr = master
        std::vector<PlayClip> clips;
        std::unique_ptr<dsp::ProcessorChain> chain;
        juce::StringArray effectTypes;
        AutomationLane volumeAutomation, panAutomation;
        std::vector<std::unique_ptr<SendTarget>> sends;

        std::atomic<float> volumeDb { 0.0f }, pan { 0.0f };
        std::atomic<bool> audible { true };

        static constexpr int meterSize = 128;
        std::array<MeterEntry, meterSize> meter;
        std::atomic<int> meterWrite { 0 };

        // Disk thread
        juce::AudioBuffer<float> buffer { 2, maxChunk };
        float lastLeft = 0.0f, lastRight = 0.0f;
        bool fresh = true; ///< after a seek: start at the target gain, no ramp
    };

    struct SessionMixer::Graph
    {
        double sampleRate = 48000.0;
        std::vector<std::unique_ptr<Strip>> tracks, buses;
        std::unique_ptr<Strip> master;
        std::map<int, Strip*> byId;
    };

    namespace
    {
        float balanceLeft (float pan) noexcept  { return pan <= 0.0f ? 1.0f : 1.0f - pan; }
        float balanceRight (float pan) noexcept { return pan >= 0.0f ? 1.0f : 1.0f + pan; }

        void addRamped (float* dest, const float* src, int n, float from, float to) noexcept
        {
            if (juce::approximatelyEqual (from, to))
            {
                if (! juce::approximatelyEqual (to, 0.0f))
                    juce::FloatVectorOperations::addWithMultiply (dest, src, to, n);
                return;
            }

            const auto step = (to - from) / (float) juce::jmax (1, n);
            auto g = from;
            for (int i = 0; i < n; ++i, g += step)
                dest[i] += src[i] * g;
        }

        void applyRamped (float* data, int n, float from, float to) noexcept
        {
            if (juce::approximatelyEqual (from, to))
            {
                if (! juce::approximatelyEqual (to, 1.0f))
                    juce::FloatVectorOperations::multiply (data, to, n);
                return;
            }

            const auto step = (to - from) / (float) juce::jmax (1, n);
            auto g = from;
            for (int i = 0; i < n; ++i, g += step)
                data[i] *= g;
        }
    }

    //==============================================================================
    SessionMixer::SessionMixer() = default;
    SessionMixer::~SessionMixer() = default;

    bool SessionMixer::updateEffects (int trackId, const juce::var& effects)
    {
        const auto settings = dsp::chainFromVar (effects);
        juce::StringArray types;
        std::vector<const dsp::EffectType*> pointers;
        std::vector<const dsp::EffectSettings*> used;

        for (const auto& s : settings)
        {
            const auto* type = dsp::catalogue::find (s.typeId);
            if (type == nullptr || ! type->isRealtime() || type->create == nullptr)
                continue;

            types.add (s.typeId);
            pointers.push_back (type);
            used.push_back (&s);
        }

        auto& state = effectStates[trackId];
        const bool changed = state.types != types || state.values.size() != used.size();

        if (changed)
        {
            state.types = types;
            state.typePointers = pointers;
            state.values.clear();
            state.enabled.clear();

            for (const auto* type : pointers)
            {
                state.values.push_back (type->createValues());
                state.enabled.push_back (std::make_shared<std::atomic<bool>> (true));
            }
        }

        for (size_t i = 0; i < used.size(); ++i)
        {
            state.values[i]->setAll (used[i]->values);
            state.enabled[i]->store (used[i]->enabled);
        }

        return changed;
    }

    bool SessionMixer::sameStructure (const Session& a, const Session& b)
    {
        if (! juce::approximatelyEqual (a.sampleRate, b.sampleRate) || a.tracks.size() != b.tracks.size())
            return false;

        const auto sameTrack = [] (const Track& x, const Track& y)
        {
            if (x.id != y.id || x.kind != y.kind || x.outputId != y.outputId || x.sends.size() != y.sends.size()
                || x.clips.size() != y.clips.size()
                || ! (x.volumeAutomation == y.volumeAutomation) || ! (x.panAutomation == y.panAutomation))
                return false;

            for (size_t i = 0; i < x.sends.size(); ++i)
                if (x.sends[i].busId != y.sends[i].busId)
                    return false;

            for (size_t i = 0; i < x.clips.size(); ++i)
            {
                const auto& c = x.clips[i];
                const auto& d = y.clips[i];
                if (c.id != d.id || c.sourceStart != d.sourceStart || c.length != d.length || c.position != d.position
                    || ! juce::exactlyEqual (c.gainDb, d.gainDb) || c.fadeIn != d.fadeIn || c.fadeOut != d.fadeOut
                    || c.muted != d.muted || ! c.audio.isSameAs (d.audio))
                    return false;
            }

            return true;
        };

        for (size_t i = 0; i < a.tracks.size(); ++i)
            if (! sameTrack (a.tracks[i], b.tracks[i]))
                return false;

        return sameTrack (a.master, b.master);
    }

    std::shared_ptr<SessionMixer::Graph> SessionMixer::buildGraph (const Session& session)
    {
        auto g = std::make_shared<Graph>();
        g->sampleRate = session.sampleRate;

        const auto makeStrip = [this, &session] (const Track& t)
        {
            auto s = std::make_unique<Strip>();
            s->id = t.id;
            s->kind = t.kind;
            s->volumeAutomation = t.volumeAutomation;
            s->panAutomation = t.panAutomation;
            s->volumeDb = t.volumeDb;
            s->pan = t.pan;
            s->audible = session.isAudible (t);

            updateEffects (t.id, t.effects);
            const auto& fx = effectStates[t.id];
            s->effectTypes = fx.types;
            s->chain = std::make_unique<dsp::ProcessorChain>();
            for (size_t i = 0; i < fx.typePointers.size(); ++i)
                s->chain->add (fx.typePointers[i]->create (fx.values[i]), fx.enabled[i]);
            s->chain->prepare (session.sampleRate, maxChunk, 2);

            // Clips, with automatic crossfades where two overlap.
            for (const auto& c : t.clips)
            {
                if (c.muted || c.length <= 0)
                    continue;

                Strip::PlayClip p;
                p.audio = c.audio;
                p.sourceStart = c.sourceStart;
                p.length = c.length;
                p.position = c.position;
                p.fadeIn = c.fadeIn;
                p.fadeOut = c.fadeOut;
                p.gain = dsp::dbToGain (c.gainDb);
                s->clips.push_back (std::move (p));
            }

            for (size_t i = 0; i + 1 < s->clips.size(); ++i)
            {
                auto& a = s->clips[i];
                auto& b = s->clips[i + 1];
                const auto aEnd = a.position + a.length;
                const auto bEnd = b.position + b.length;

                if (b.position < aEnd && bEnd > aEnd)
                {
                    const auto overlap = aEnd - b.position;
                    a.fadeOut = juce::jmin (a.length, juce::jmax (a.fadeOut, overlap));
                    b.fadeIn = juce::jmin (b.length, juce::jmax (b.fadeIn, overlap));
                }
            }

            return s;
        };

        for (const auto& t : session.tracks)
        {
            auto s = makeStrip (t);
            g->byId[t.id] = s.get();
            (t.isBus() ? g->buses : g->tracks).push_back (std::move (s));
        }

        g->master = makeStrip (session.master);
        g->byId[0] = g->master.get();

        // Routing: outputs and sends to buses.
        for (const auto& t : session.tracks)
        {
            if (! t.isAudio())
                continue;

            auto* s = g->byId[t.id];
            if (const auto it = g->byId.find (t.outputId); t.outputId != 0 && it != g->byId.end() && it->second->kind == Track::Kind::bus)
                s->output = it->second;

            for (const auto& send : t.sends)
            {
                const auto it = g->byId.find (send.busId);
                if (it == g->byId.end() || it->second->kind != Track::Kind::bus)
                    continue;

                auto target = std::make_unique<Strip::SendTarget>();
                target->busId = send.busId;
                target->bus = it->second;
                target->levelDb = send.levelDb;
                s->sends.push_back (std::move (target));
            }
        }

        // Effect states of removed tracks are not needed any more.
        for (auto it = effectStates.begin(); it != effectStates.end();)
            it = g->byId.count (it->first) == 0 ? effectStates.erase (it) : std::next (it);

        return g;
    }

    void SessionMixer::updateValues (const Session& session)
    {
        const auto update = [this, &session] (const Track& t)
        {
            auto* s = findStrip (t.id);
            if (s == nullptr)
                return false;

            s->volumeDb = t.volumeDb;
            s->pan = t.pan;
            s->audible = session.isAudible (t);

            for (auto& target : s->sends)
                for (const auto& send : t.sends)
                    if (send.busId == target->busId)
                        target->levelDb = send.levelDb;

            return ! updateEffects (t.id, t.effects);
        };

        bool ok = true;
        for (const auto& t : session.tracks)
            ok = update (t) && ok;

        ok = update (session.master) && ok;

        // The list of effects changed somewhere: a new graph is needed after all.
        if (! ok)
        {
            latest = buildGraph (session);
            const std::lock_guard<std::mutex> lock (pendingLock);
            pending = latest;
        }
    }

    SessionMixer::Strip* SessionMixer::findStrip (int trackId) const
    {
        if (latest == nullptr)
            return nullptr;

        const auto it = latest->byId.find (trackId);
        return it != latest->byId.end() ? it->second : nullptr;
    }

    void SessionMixer::setSession (const Session& session)
    {
        rate = session.sampleRate;
        length = session.getLength();

        if (hasShown && latest != nullptr && sameStructure (shown, session))
        {
            shown = session;
            updateValues (session);
            return;
        }

        shown = session;
        hasShown = true;
        latest = buildGraph (session);

        const std::lock_guard<std::mutex> lock (pendingLock);
        pending = latest;
    }

    void SessionMixer::setTrackVolume (int trackId, float db)
    {
        if (auto* s = findStrip (trackId))
            s->volumeDb = db;
    }

    void SessionMixer::setTrackPan (int trackId, float pan)
    {
        if (auto* s = findStrip (trackId))
            s->pan = juce::jlimit (-1.0f, 1.0f, pan);
    }

    void SessionMixer::setSendLevel (int trackId, int busId, float db)
    {
        if (auto* s = findStrip (trackId))
            for (auto& target : s->sends)
                if (target->busId == busId)
                    target->levelDb = db;
    }

    std::pair<float, float> SessionMixer::getLevel (int trackId, juce::int64 frame) const
    {
        const auto* s = findStrip (trackId);
        if (s == nullptr)
            return { 0.0f, 0.0f };

        // The mix runs ahead of what is heard: show the chunks around the playhead.
        const auto window = (juce::int64) (0.06 * rate.load());
        float left = 0.0f, right = 0.0f;

        for (const auto& e : s->meter)
        {
            const auto f = e.frame.load (std::memory_order_acquire);
            if (f >= 0 && f <= frame && f > frame - window)
            {
                left = juce::jmax (left, e.left.load (std::memory_order_relaxed));
                right = juce::jmax (right, e.right.load (std::memory_order_relaxed));
            }
        }

        return { left, right };
    }

    //==============================================================================
    void SessionMixer::adoptPendingGraph()
    {
        std::shared_ptr<Graph> next;
        {
            const std::lock_guard<std::mutex> lock (pendingLock);
            next = std::move (pending);
            pending = nullptr;
        }

        if (next == nullptr)
            return;

        // Tracks whose effects did not change keep their running chain (reverb tails, envelopes).
        if (current != nullptr)
        {
            for (auto& [id, strip] : next->byId)
            {
                const auto old = current->byId.find (id);
                if (old == current->byId.end())
                    continue;

                auto* o = old->second;
                if (o->effectTypes == strip->effectTypes)
                    std::swap (o->chain, strip->chain);

                strip->lastLeft = o->lastLeft;
                strip->lastRight = o->lastRight;
                strip->fresh = o->fresh;

                for (auto& target : strip->sends)
                    for (auto& oldTarget : o->sends)
                        if (oldTarget->busId == target->busId)
                            target->lastGain = oldTarget->lastGain;
            }
        }

        current = std::move (next);
    }

    void SessionMixer::seek (juce::int64 frame)
    {
        adoptPendingGraph();
        position = juce::jmax ((juce::int64) 0, frame);

        if (current == nullptr)
            return;

        for (auto& [id, strip] : current->byId)
        {
            strip->chain->reset();
            strip->fresh = true;
            for (auto& target : strip->sends)
                target->lastGain = 0.0f;
        }
    }

    void SessionMixer::read (float* const* dest, int numFrames)
    {
        adoptPendingGraph();

        for (int done = 0; done < numFrames;)
        {
            const auto n = juce::jmin (maxChunk, numFrames - done);
            float* out[2] = { dest[0] + done, dest[1] + done };
            renderChunk (out, n);
            position += n;
            done += n;
        }
    }

    void SessionMixer::renderClips (Strip& s, juce::int64 start, int numFrames)
    {
        const auto end = start + numFrames;
        float* tmp[2] = { clipBuffer.getWritePointer (0), clipBuffer.getWritePointer (1) };

        for (const auto& c : s.clips)
        {
            const auto clipEnd = c.position + c.length;
            if (c.position >= end)
                break; // sorted by position
            if (clipEnd <= start)
                continue;

            const auto from = juce::jmax (start, c.position);
            const auto to = juce::jmin (end, clipEnd);
            const auto count = (int) (to - from);
            const auto offset = (int) (from - start);

            c.audio.read (tmp, 2, c.sourceStart + (from - c.position), count);
            if (c.audio.getNumChannels() == 1)
                juce::FloatVectorOperations::copy (tmp[1], tmp[0], count);

            const bool fading = (c.fadeIn > 0 && from < c.position + c.fadeIn)
                             || (c.fadeOut > 0 && to > clipEnd - c.fadeOut);

            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = s.buffer.getWritePointer (ch) + offset;

                if (! fading)
                {
                    juce::FloatVectorOperations::addWithMultiply (d, tmp[ch], c.gain, count);
                    continue;
                }

                for (int i = 0; i < count; ++i)
                {
                    const auto inClip = from - c.position + i;
                    auto g = c.gain;
                    if (inClip < c.fadeIn)
                        g *= sessionEdits::fadeCurve ((double) inClip / (double) c.fadeIn);
                    if (inClip >= c.length - c.fadeOut)
                        g *= sessionEdits::fadeCurve ((double) (c.length - inClip) / (double) c.fadeOut);

                    d[i] += tmp[ch][i] * g;
                }
            }
        }
    }

    void SessionMixer::applyStrip (Strip& s, juce::int64 start, int numFrames, float* const* target)
    {
        auto* data = s.buffer.getArrayOfWritePointers();

        if (! s.chain->isEmpty())
            s.chain->process (data, 2, numFrames);

        // Fader and pan (or their automation) at the end of the chunk; ramp there from the last values.
        const auto at = start + numFrames;
        const auto db = s.volumeAutomation.valueAt (at, s.volumeDb.load (std::memory_order_relaxed));
        const auto pan = juce::jlimit (-1.0f, 1.0f, s.panAutomation.valueAt (at, s.pan.load (std::memory_order_relaxed)));
        const auto gain = s.audible.load (std::memory_order_relaxed) ? dsp::dbToGain (db) : 0.0f;
        const auto left = gain * balanceLeft (pan);
        const auto right = gain * balanceRight (pan);

        if (s.fresh)
        {
            s.lastLeft = left;
            s.lastRight = right;
        }

        applyRamped (data[0], numFrames, s.lastLeft, left);
        applyRamped (data[1], numFrames, s.lastRight, right);
        s.lastLeft = left;
        s.lastRight = right;

        // Meter (post fader), stamped with the session position of the chunk.
        const auto peakL = s.buffer.getMagnitude (0, 0, numFrames);
        const auto peakR = s.buffer.getMagnitude (1, 0, numFrames);
        const auto slot = s.meterWrite.load (std::memory_order_relaxed);
        auto& entry = s.meter[(size_t) slot];
        entry.frame.store (-1, std::memory_order_release);
        entry.left.store (peakL, std::memory_order_relaxed);
        entry.right.store (peakR, std::memory_order_relaxed);
        entry.frame.store (start, std::memory_order_release);
        s.meterWrite.store ((slot + 1) % Strip::meterSize, std::memory_order_relaxed);

        // Post-fader sends
        for (auto& send : s.sends)
        {
            const auto sendGain = s.audible.load (std::memory_order_relaxed) ? dsp::dbToGain (send->levelDb.load (std::memory_order_relaxed)) : 0.0f;
            if (s.fresh)
                send->lastGain = sendGain;

            for (int ch = 0; ch < 2; ++ch)
                addRamped (send->bus->buffer.getWritePointer (ch), data[ch], numFrames, send->lastGain, sendGain);

            send->lastGain = sendGain;
        }

        s.fresh = false;

        if (target != nullptr)
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::add (target[ch], data[ch], numFrames);
    }

    void SessionMixer::renderChunk (float* const* dest, int numFrames)
    {
        juce::FloatVectorOperations::clear (dest[0], numFrames);
        juce::FloatVectorOperations::clear (dest[1], numFrames);

        if (current == nullptr)
            return;

        auto& g = *current;

        for (auto& b : g.buses)
            b->buffer.clear (0, numFrames);
        g.master->buffer.clear (0, numFrames);

        for (auto& t : g.tracks)
        {
            t->buffer.clear (0, numFrames);
            renderClips (*t, position, numFrames);
            auto* out = t->output != nullptr ? t->output : g.master.get();
            applyStrip (*t, position, numFrames, out->buffer.getArrayOfWritePointers());
        }

        for (auto& b : g.buses)
            applyStrip (*b, position, numFrames, g.master->buffer.getArrayOfWritePointers());

        applyStrip (*g.master, position, numFrames, dest);
    }

    //==============================================================================
    AudioSequence SessionMixer::mixdown (const Session& session, FrameRange range, BlockStore& store,
                                         const dsp::WorkControl& control)
    {
        SessionMixer mixer;
        mixer.setSession (session);
        mixer.seek (range.getStart());

        dsp::SequenceWriter writer (store, 2, session.sampleRate, range.getLength());
        juce::AudioBuffer<float> buffer (2, 8192);

        for (juce::int64 done = 0; done < range.getLength();)
        {
            if (control.cancelled())
                return AudioSequence();

            const auto n = (int) juce::jmin ((juce::int64) buffer.getNumSamples(), range.getLength() - done);
            mixer.read (buffer.getArrayOfWritePointers(), n);

            if (! writer.append (buffer.getArrayOfReadPointers(), n))
                return AudioSequence();

            done += n;
            control.report ((double) done / (double) juce::jmax ((juce::int64) 1, range.getLength()));
        }

        return writer.finish();
    }
}
