// SPDX-License-Identifier: AGPL-3.0-only
#include "core/edit/EditOperations.h"

#include <algorithm>

namespace mxj::edits
{
    namespace
    {
        FrameRange clampRange (const AudioState& s, FrameRange r)
        {
            return r.getIntersectionWith ({ 0, s.audio.getNumFrames() });
        }

        juce::int64 clampPosition (const AudioState& s, juce::int64 p)
        {
            return juce::jlimit ((juce::int64) 0, s.audio.getNumFrames(), p);
        }

        /** Replaces the range by `processed` (same length), markers stay. */
        Result replaceInPlace (const AudioState& s, FrameRange range, const AudioSequence& processed)
        {
            Result r { s, range, false };
            if (range.isEmpty())
                return r;

            r.changed = r.state.audio.replace (range.getStart(), range.getLength(), processed);
            return r;
        }
    }

    Result erase (const AudioState& s, FrameRange range)
    {
        range = clampRange (s, range);
        Result r { s, { range.getStart(), range.getStart() }, false };
        if (range.isEmpty())
            return r;

        r.state.audio.erase (range.getStart(), range.getLength());
        r.state.markers = markersAfterErase (s.markers, range.getStart(), range.getLength());
        r.changed = true;
        return r;
    }

    Result trim (const AudioState& s, FrameRange range)
    {
        range = clampRange (s, range);
        Result r { s, {}, false };
        if (range.isEmpty() || range.getLength() == s.audio.getNumFrames())
            return r;

        r.state.audio = s.audio.getRange (range.getStart(), range.getLength());
        r.state.markers.clear();
        for (const auto& m : s.markers)
            if (range.contains (m.position) || m.position == range.getEnd())
                r.state.markers.push_back ({ m.position - range.getStart(), m.name });

        r.selection = { 0, range.getLength() };
        r.changed = true;
        return r;
    }

    Result paste (const AudioState& s, FrameRange range, const AudioSequence& clipIn)
    {
        range = clampRange (s, { clampPosition (s, range.getStart()), range.getEnd() });
        Result r { s, range, false };

        if (clipIn.isEmpty() && range.isEmpty())
            return r;

        // An empty new file takes the format of whatever comes in first.
        const bool emptyFile = s.audio.isEmpty() && s.audio.getNumChannels() == 0;
        const auto clip = emptyFile ? clipIn : derived::withChannels (clipIn, s.audio.getNumChannels());

        if (! clip.isEmpty() && ! emptyFile && ! juce::approximatelyEqual (clip.getSampleRate(), s.audio.getSampleRate()))
            return r;

        if (! r.state.audio.replace (range.getStart(), range.getLength(), clip))
            return r;

        auto markers = markersAfterErase (s.markers, range.getStart(), range.getLength());
        r.state.markers = markersAfterInsert (markers, range.getStart(), clip.getNumFrames());
        r.selection = { range.getStart(), range.getStart() + clip.getNumFrames() };
        r.changed = true;
        return r;
    }

    Result mixPaste (const AudioState& s, FrameRange range, const AudioSequence& clipIn)
    {
        const auto start = clampPosition (s, range.getStart());
        Result r { s, { start, start }, false };

        if (clipIn.isEmpty())
            return r;

        if (s.audio.isEmpty())
            return paste (s, { start, start }, clipIn);

        const auto clip = derived::withChannels (clipIn, s.audio.getNumChannels());
        if (! juce::approximatelyEqual (clip.getSampleRate(), s.audio.getSampleRate()))
            return r;

        const auto under = s.audio.getRange (start, clip.getNumFrames());
        const auto mixed = derived::mix (under, clip);

        if (! r.state.audio.replace (start, under.getNumFrames(), mixed))
            return r;

        r.selection = { start, start + clip.getNumFrames() };
        r.changed = true;
        return r;
    }

    Result silence (const AudioState& s, FrameRange range)
    {
        range = clampRange (s, range);
        return replaceInPlace (s, range, derived::silence (s.audio.getNumChannels(), s.audio.getSampleRate(), range.getLength()));
    }

    Result insertSilence (const AudioState& s, FrameRange range, juce::int64 numFrames)
    {
        if (numFrames <= 0 || s.audio.getNumChannels() <= 0)
            return { s, range, false };

        return paste (s, range, derived::silence (s.audio.getNumChannels(), s.audio.getSampleRate(), numFrames));
    }

    Result fadeIn (const AudioState& s, FrameRange range)
    {
        range = clampRange (s, range);
        return replaceInPlace (s, range, derived::gainRamp (s.audio.getRange (range.getStart(), range.getLength()), 0.0f, 1.0f));
    }

    Result fadeOut (const AudioState& s, FrameRange range)
    {
        range = clampRange (s, range);
        return replaceInPlace (s, range, derived::gainRamp (s.audio.getRange (range.getStart(), range.getLength()), 1.0f, 0.0f));
    }

    Result gain (const AudioState& s, FrameRange range, float gainFactor)
    {
        range = clampRange (s, range);
        return replaceInPlace (s, range, derived::gainRamp (s.audio.getRange (range.getStart(), range.getLength()),
                                                            gainFactor, gainFactor, GainRampSource::Curve::linear));
    }

    //==============================================================================
    size_t addMarker (MarkerList& markers, Marker m)
    {
        const auto it = std::upper_bound (markers.begin(), markers.end(), m.position,
                                          [] (juce::int64 p, const Marker& other) { return p < other.position; });
        return (size_t) (markers.insert (it, std::move (m)) - markers.begin());
    }

    juce::String nextMarkerName (const MarkerList& markers, const juce::String& baseName)
    {
        int highest = 0;
        for (const auto& m : markers)
            if (m.name.startsWith (baseName + " "))
                highest = juce::jmax (highest, m.name.fromLastOccurrenceOf (" ", false, false).getIntValue());

        return baseName + " " + juce::String (juce::jmax (highest, 0) + 1);
    }

    MarkerList markersAfterErase (const MarkerList& markers, juce::int64 start, juce::int64 length)
    {
        if (length <= 0)
            return markers;

        MarkerList result;
        const auto end = start + length;

        for (const auto& m : markers)
        {
            if (m.position < start)
                result.push_back (m);
            else if (m.position >= end)
                result.push_back ({ m.position - length, m.name });
            // inside the removed audio: gone
        }

        return result;
    }

    MarkerList markersAfterInsert (const MarkerList& markers, juce::int64 position, juce::int64 length)
    {
        if (length <= 0)
            return markers;

        MarkerList result;
        for (const auto& m : markers)
            result.push_back (m.position >= position ? Marker { m.position + length, m.name } : m);

        return result;
    }
}
