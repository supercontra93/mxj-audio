// SPDX-License-Identifier: AGPL-3.0-only
#include "core/session/SessionEdits.h"

#include <algorithm>

namespace mxj::sessionEdits
{
    namespace
    {
        bool contains (const std::vector<int>& ids, int id)
        {
            return std::find (ids.begin(), ids.end(), id) != ids.end();
        }

        void limitFades (Clip& c)
        {
            c.fadeIn = juce::jlimit ((juce::int64) 0, c.length, c.fadeIn);
            c.fadeOut = juce::jlimit ((juce::int64) 0, c.length - c.fadeIn, c.fadeOut);
        }

        bool appliesTo (const Track& t, const std::vector<int>& trackIds)
        {
            return t.isAudio() && (trackIds.empty() || contains (trackIds, t.id));
        }
    }

    int addTrack (Session& s, Track::Kind kind, const juce::String& name, int index)
    {
        Track t;
        t.id = s.createId();
        t.kind = kind == Track::Kind::bus ? Track::Kind::bus : Track::Kind::audio;
        t.name = name;
        t.colour = (int) s.tracks.size();

        if (index < 0 || index > (int) s.tracks.size())
            s.tracks.push_back (std::move (t));
        else
            s.tracks.insert (s.tracks.begin() + index, std::move (t));

        return s.nextId - 1;
    }

    bool removeTrack (Session& s, int trackId)
    {
        const auto index = s.indexOfTrack (trackId);
        if (index < 0)
            return false;

        s.tracks.erase (s.tracks.begin() + index);

        for (auto& t : s.tracks)
        {
            if (t.outputId == trackId)
                t.outputId = 0;

            t.sends.erase (std::remove_if (t.sends.begin(), t.sends.end(), [trackId] (const Send& x) { return x.busId == trackId; }),
                           t.sends.end());
        }

        return true;
    }

    bool moveTrack (Session& s, int trackId, int newIndex)
    {
        const auto index = s.indexOfTrack (trackId);
        if (index < 0)
            return false;

        newIndex = juce::jlimit (0, (int) s.tracks.size() - 1, newIndex);
        if (newIndex == index)
            return false;

        auto t = std::move (s.tracks[(size_t) index]);
        s.tracks.erase (s.tracks.begin() + index);
        s.tracks.insert (s.tracks.begin() + newIndex, std::move (t));
        return true;
    }

    Clip makeClip (Session& s, const juce::String& name, const AudioSequence& audio, juce::int64 position)
    {
        Clip c;
        c.id = s.createId();
        c.name = name;
        c.audio = audio;
        c.length = audio.getNumFrames();
        c.position = juce::jmax ((juce::int64) 0, position);
        return c;
    }

    int addClip (Session& s, int trackId, Clip clip)
    {
        auto* t = s.findTrack (trackId);
        if (t == nullptr || ! t->isAudio() || clip.length <= 0)
            return 0;

        if (clip.id == 0)
            clip.id = s.createId();

        clip.position = juce::jmax ((juce::int64) 0, clip.position);
        limitFades (clip);
        const auto id = clip.id;
        t->clips.push_back (std::move (clip));
        t->sortClips();
        return id;
    }

    bool moveClip (Session& s, int clipId, int newTrackId, juce::int64 newPosition)
    {
        auto* from = s.findTrackOfClip (clipId);
        auto* to = s.findTrack (newTrackId);
        if (from == nullptr || to == nullptr || ! to->isAudio())
            return false;

        auto clip = *from->findClip (clipId);
        clip.position = juce::jmax ((juce::int64) 0, newPosition);

        from->clips.erase (std::remove_if (from->clips.begin(), from->clips.end(), [clipId] (const Clip& c) { return c.id == clipId; }),
                           from->clips.end());
        to->clips.push_back (std::move (clip));
        to->sortClips();
        return true;
    }

    int splitClip (Session& s, int clipId, juce::int64 frame)
    {
        auto* t = s.findTrackOfClip (clipId);
        if (t == nullptr)
            return 0;

        auto& left = *t->findClip (clipId);
        if (frame <= left.position || frame >= left.getEnd())
            return 0;

        auto right = left;
        right.id = s.createId();
        const auto cut = frame - left.position;

        left.length = cut;
        left.fadeOut = 0;
        right.sourceStart += cut;
        right.length -= cut;
        right.position = frame;
        right.fadeIn = 0;
        limitFades (left);
        limitFades (right);

        const auto id = right.id;
        t->clips.push_back (std::move (right));
        t->sortClips();
        return id;
    }

    int splitAt (Session& s, juce::int64 frame, const std::vector<int>& trackIds)
    {
        int count = 0;
        for (auto& t : s.tracks)
        {
            if (! appliesTo (t, trackIds))
                continue;

            std::vector<int> crossing;
            for (const auto& c : t.clips)
                if (frame > c.position && frame < c.getEnd())
                    crossing.push_back (c.id);

            for (auto id : crossing)
                if (splitClip (s, id, frame) != 0)
                    ++count;
        }

        return count;
    }

    bool trimClipStart (Session& s, int clipId, juce::int64 newStart)
    {
        auto* t = s.findTrackOfClip (clipId);
        if (t == nullptr)
            return false;

        auto& c = *t->findClip (clipId);
        const auto end = c.getEnd();
        newStart = juce::jlimit (juce::jmax ((juce::int64) 0, c.position - c.sourceStart), end - 1, newStart);
        const auto delta = newStart - c.position;
        if (delta == 0)
            return false;

        c.sourceStart += delta;
        c.position = newStart;
        c.length = end - newStart;
        limitFades (c);
        t->sortClips();
        return true;
    }

    bool trimClipEnd (Session& s, int clipId, juce::int64 newEnd)
    {
        auto* c = s.findTrackOfClip (clipId) != nullptr ? s.findTrackOfClip (clipId)->findClip (clipId) : nullptr;
        if (c == nullptr)
            return false;

        newEnd = juce::jlimit (c->position + 1, c->position + c->audio.getNumFrames() - c->sourceStart, newEnd);
        if (newEnd == c->getEnd())
            return false;

        c->length = newEnd - c->position;
        limitFades (*c);
        return true;
    }

    bool setClipFades (Session& s, int clipId, juce::int64 fadeIn, juce::int64 fadeOut)
    {
        auto* t = s.findTrackOfClip (clipId);
        if (t == nullptr)
            return false;

        auto& c = *t->findClip (clipId);
        c.fadeIn = fadeIn;
        c.fadeOut = fadeOut;
        limitFades (c);
        return true;
    }

    bool setClipGain (Session& s, int clipId, float gainDb)
    {
        auto* t = s.findTrackOfClip (clipId);
        if (t == nullptr)
            return false;

        t->findClip (clipId)->gainDb = juce::jlimit (-96.0f, 24.0f, gainDb);
        return true;
    }

    int deleteClips (Session& s, const std::vector<int>& clipIds, bool ripple)
    {
        int count = 0;

        for (auto& t : s.tracks)
        {
            // Delete from right to left, so ripple shifts stay correct.
            std::vector<Clip> removed;
            for (const auto& c : t.clips)
                if (contains (clipIds, c.id))
                    removed.push_back (c);

            std::sort (removed.begin(), removed.end(), [] (const Clip& a, const Clip& b) { return a.position > b.position; });

            for (const auto& r : removed)
            {
                t.clips.erase (std::remove_if (t.clips.begin(), t.clips.end(), [&r] (const Clip& c) { return c.id == r.id; }),
                               t.clips.end());
                ++count;

                if (! ripple)
                    continue;

                // Close the gap only where nothing else still covers it.
                juce::int64 gapStart = r.position, gapEnd = r.getEnd();
                for (const auto& c : t.clips)
                    if (c.position < gapEnd && c.getEnd() > gapStart && c.position < r.position)
                        gapStart = juce::jmax (gapStart, c.getEnd());

                const auto shift = juce::jmax ((juce::int64) 0, gapEnd - gapStart);
                for (auto& c : t.clips)
                    if (c.position >= gapEnd)
                        c.position -= shift;
            }

            t.sortClips();
        }

        return count;
    }

    void deleteRange (Session& s, FrameRange range, bool ripple, const std::vector<int>& trackIds)
    {
        if (range.isEmpty())
            return;

        for (auto& t : s.tracks)
        {
            if (! appliesTo (t, trackIds))
                continue;

            std::vector<Clip> kept;
            for (auto c : t.clips)
            {
                if (c.getEnd() <= range.getStart() || c.position >= range.getEnd())
                {
                    if (ripple && c.position >= range.getEnd())
                        c.position -= range.getLength();

                    kept.push_back (c);
                    continue;
                }

                // Left part
                if (c.position < range.getStart())
                {
                    auto left = c;
                    left.length = range.getStart() - c.position;
                    left.fadeOut = juce::jmin (left.fadeOut, left.length);
                    limitFades (left);
                    kept.push_back (left);
                }

                // Right part
                if (c.getEnd() > range.getEnd())
                {
                    auto right = c;
                    if (c.position < range.getStart())
                        right.id = s.createId();

                    const auto cut = range.getEnd() - c.position;
                    right.sourceStart += cut;
                    right.length -= cut;
                    right.position = ripple ? range.getStart() : range.getEnd();
                    right.fadeIn = 0;
                    limitFades (right);
                    kept.push_back (right);
                }
            }

            t.clips = std::move (kept);
            t.sortClips();
        }
    }

    juce::int64 snap (const Session& s, juce::int64 frame, juce::int64 tolerance,
                      const std::vector<int>& ignoreClips, const std::vector<juce::int64>& extraTargets)
    {
        auto best = frame;
        auto bestDistance = tolerance + 1;

        const auto consider = [&] (juce::int64 target)
        {
            const auto d = std::abs (target - frame);
            if (d < bestDistance)
            {
                best = target;
                bestDistance = d;
            }
        };

        consider (0);
        for (auto t : extraTargets)
            consider (t);

        for (const auto& m : s.markers)
            consider (m.position);

        for (const auto& t : s.tracks)
            for (const auto& c : t.clips)
                if (! contains (ignoreClips, c.id))
                {
                    consider (c.position);
                    consider (c.getEnd());
                }

        return bestDistance <= tolerance ? best : frame;
    }
}
