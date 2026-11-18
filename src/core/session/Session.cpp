// SPDX-License-Identifier: AGPL-3.0-only
#include "core/session/Session.h"

#include <algorithm>

namespace mxj
{
    //==============================================================================
    float AutomationLane::valueAt (juce::int64 frame, float fallback) const noexcept
    {
        if (points.empty())
            return fallback;

        if (frame <= points.front().frame)
            return points.front().value;

        if (frame >= points.back().frame)
            return points.back().value;

        const auto next = std::upper_bound (points.begin(), points.end(), frame,
                                            [] (juce::int64 f, const AutomationPoint& p) { return f < p.frame; });
        const auto& b = *next;
        const auto& a = *(next - 1);

        if (b.frame == a.frame)
            return b.value;

        const auto t = (double) (frame - a.frame) / (double) (b.frame - a.frame);
        return (float) (a.value + (b.value - a.value) * t);
    }

    int AutomationLane::add (AutomationPoint point)
    {
        const auto it = std::lower_bound (points.begin(), points.end(), point.frame,
                                          [] (const AutomationPoint& p, juce::int64 f) { return p.frame < f; });

        if (it != points.end() && it->frame == point.frame)
        {
            it->value = point.value;
            return (int) (it - points.begin());
        }

        return (int) (points.insert (it, point) - points.begin());
    }

    //==============================================================================
    juce::int64 Track::getEnd() const noexcept
    {
        juce::int64 end = 0;
        for (const auto& c : clips)
            end = juce::jmax (end, c.getEnd());

        return end;
    }

    const Clip* Track::findClip (int clipId) const
    {
        for (const auto& c : clips)
            if (c.id == clipId)
                return &c;

        return nullptr;
    }

    Clip* Track::findClip (int clipId)
    {
        return const_cast<Clip*> (std::as_const (*this).findClip (clipId));
    }

    const Clip* Track::clipAt (juce::int64 frame) const
    {
        const Clip* found = nullptr;
        for (const auto& c : clips)
            if (c.getRange().contains (frame))
                found = &c; // sorted by position: the later one wins

        return found;
    }

    void Track::sortClips()
    {
        std::stable_sort (clips.begin(), clips.end(), [] (const Clip& a, const Clip& b) { return a.position < b.position; });
    }

    //==============================================================================
    Session::Session()
    {
        master.id = 0;
        master.kind = Track::Kind::master;
        master.name = "Master";
    }

    juce::int64 Session::getLength() const noexcept
    {
        juce::int64 end = 0;
        for (const auto& t : tracks)
            end = juce::jmax (end, t.getEnd());

        return end;
    }

    int Session::getNumAudioTracks() const noexcept
    {
        return (int) std::count_if (tracks.begin(), tracks.end(), [] (const Track& t) { return t.isAudio(); });
    }

    const Track* Session::findTrack (int trackId) const
    {
        if (trackId == 0)
            return &master;

        for (const auto& t : tracks)
            if (t.id == trackId)
                return &t;

        return nullptr;
    }

    Track* Session::findTrack (int trackId)
    {
        return const_cast<Track*> (std::as_const (*this).findTrack (trackId));
    }

    int Session::indexOfTrack (int trackId) const
    {
        for (size_t i = 0; i < tracks.size(); ++i)
            if (tracks[i].id == trackId)
                return (int) i;

        return -1;
    }

    const Track* Session::findTrackOfClip (int clipId) const
    {
        for (const auto& t : tracks)
            if (t.findClip (clipId) != nullptr)
                return &t;

        return nullptr;
    }

    Track* Session::findTrackOfClip (int clipId)
    {
        return const_cast<Track*> (std::as_const (*this).findTrackOfClip (clipId));
    }

    const Clip* Session::findClip (int clipId) const
    {
        const auto* t = findTrackOfClip (clipId);
        return t != nullptr ? t->findClip (clipId) : nullptr;
    }

    bool Session::hasSolo() const noexcept
    {
        return std::any_of (tracks.begin(), tracks.end(), [] (const Track& t) { return t.solo; });
    }

    bool Session::feeds (const Track& track, int busId)
    {
        if (! track.isAudio())
            return false;

        if (track.outputId == busId)
            return true;

        return std::any_of (track.sends.begin(), track.sends.end(), [busId] (const Send& s) { return s.busId == busId; });
    }

    bool Session::isAudible (const Track& track) const
    {
        if (track.kind == Track::Kind::master)
            return ! track.mute;

        if (track.mute)
            return false;

        if (! hasSolo() || track.solo)
            return true;

        for (const auto& other : tracks)
        {
            if (! other.solo || other.mute)
                continue;

            // A soloed track keeps the buses it plays into; a soloed bus keeps the tracks feeding it.
            if (track.isBus() && feeds (other, track.id))
                return true;

            if (track.isAudio() && other.isBus() && feeds (track, other.id))
                return true;
        }

        return false;
    }
}
