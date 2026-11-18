// SPDX-License-Identifier: AGPL-3.0-only
#include "core/session/SessionFile.h"

#include <algorithm>

namespace mxj::sessionFile
{
    namespace
    {
        juce::var lane (const AutomationLane& l)
        {
            juce::Array<juce::var> points;
            for (const auto& p : l.points)
                points.add (juce::Array<juce::var> { p.frame, p.value });

            return points;
        }

        AutomationLane readLane (const juce::var& v)
        {
            AutomationLane l;
            if (const auto* a = v.getArray())
                for (const auto& p : *a)
                    l.add ({ (juce::int64) p[0], (float) p[1] });

            return l;
        }

        const char* kindName (Track::Kind k)
        {
            return k == Track::Kind::bus ? "bus" : k == Track::Kind::master ? "master" : "audio";
        }

        Track::Kind kindFrom (const juce::String& s)
        {
            return s == "bus" ? Track::Kind::bus : s == "master" ? Track::Kind::master : Track::Kind::audio;
        }

        juce::var trackToVar (const Track& t, SequenceWriter& w, bool& failed)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("id", t.id);
            o->setProperty ("kind", kindName (t.kind));
            o->setProperty ("name", t.name);
            o->setProperty ("colour", t.colour);
            o->setProperty ("volume", t.volumeDb);
            o->setProperty ("pan", t.pan);
            o->setProperty ("mute", t.mute);
            o->setProperty ("solo", t.solo);
            o->setProperty ("armed", t.armed);
            o->setProperty ("output", t.outputId);
            o->setProperty ("showAutomation", t.showAutomation);
            o->setProperty ("effects", t.effects);
            o->setProperty ("volumeAutomation", lane (t.volumeAutomation));
            o->setProperty ("panAutomation", lane (t.panAutomation));

            juce::Array<juce::var> sends;
            for (const auto& s : t.sends)
                sends.add (juce::Array<juce::var> { s.busId, s.levelDb });
            o->setProperty ("sends", sends);

            juce::Array<juce::var> clips;
            for (const auto& c : t.clips)
            {
                auto* co = new juce::DynamicObject();
                co->setProperty ("id", c.id);
                co->setProperty ("name", c.name);
                co->setProperty ("sourceStart", c.sourceStart);
                co->setProperty ("length", c.length);
                co->setProperty ("position", c.position);
                co->setProperty ("gain", c.gainDb);
                co->setProperty ("fadeIn", c.fadeIn);
                co->setProperty ("fadeOut", c.fadeOut);
                co->setProperty ("muted", c.muted);

                const auto audio = w.write (c.audio);
                failed = failed || audio.isVoid();
                co->setProperty ("audio", audio);
                clips.add (juce::var (co));
            }
            o->setProperty ("clips", clips);

            return juce::var (o);
        }

        Track trackFrom (const juce::var& v, SequenceReader& reader, double sampleRate)
        {
            Track t;
            t.id = (int) v["id"];
            t.kind = kindFrom (v["kind"].toString());
            t.name = v["name"].toString();
            t.colour = (int) v["colour"];
            t.volumeDb = (float) v.getProperty ("volume", 0.0);
            t.pan = juce::jlimit (-1.0f, 1.0f, (float) v.getProperty ("pan", 0.0));
            t.mute = (bool) v["mute"];
            t.solo = (bool) v["solo"];
            t.armed = (bool) v["armed"];
            t.outputId = (int) v["output"];
            t.showAutomation = (bool) v["showAutomation"];
            t.effects = v["effects"];
            t.volumeAutomation = readLane (v["volumeAutomation"]);
            t.panAutomation = readLane (v["panAutomation"]);

            if (const auto* sends = v["sends"].getArray())
                for (const auto& s : *sends)
                    t.sends.push_back ({ (int) s[0], (float) s[1] });

            if (const auto* clips = v["clips"].getArray())
            {
                for (const auto& cv : *clips)
                {
                    Clip c;
                    c.id = (int) cv["id"];
                    c.name = cv["name"].toString();
                    c.audio = reader.read (cv["audio"]);
                    if (c.audio.getNumChannels() == 0)
                        c.audio = AudioSequence (1, sampleRate);

                    c.sourceStart = juce::jlimit ((juce::int64) 0, c.audio.getNumFrames(), (juce::int64) cv["sourceStart"]);
                    c.length = juce::jlimit ((juce::int64) 0, c.audio.getNumFrames() - c.sourceStart, (juce::int64) cv["length"]);
                    c.position = juce::jmax ((juce::int64) 0, (juce::int64) cv["position"]);
                    c.gainDb = (float) cv["gain"];
                    c.fadeIn = juce::jlimit ((juce::int64) 0, c.length, (juce::int64) cv["fadeIn"]);
                    c.fadeOut = juce::jlimit ((juce::int64) 0, c.length - c.fadeIn, (juce::int64) cv["fadeOut"]);
                    c.muted = (bool) cv["muted"];

                    if (c.length > 0)
                        t.clips.push_back (std::move (c));
                }
            }

            t.sortClips();
            return t;
        }
    }

    juce::var toVar (const Session& s, SequenceWriter& w)
    {
        bool failed = false;
        auto* o = new juce::DynamicObject();
        o->setProperty ("sampleRate", s.sampleRate);
        o->setProperty ("nextId", s.nextId);

        juce::Array<juce::var> tracks;
        for (const auto& t : s.tracks)
            tracks.add (trackToVar (t, w, failed));
        o->setProperty ("tracks", tracks);
        o->setProperty ("master", trackToVar (s.master, w, failed));

        juce::Array<juce::var> markers;
        for (const auto& m : s.markers)
            markers.add (juce::Array<juce::var> { m.position, m.name });
        o->setProperty ("markers", markers);

        return failed ? juce::var() : juce::var (o);
    }

    bool fromVar (const juce::var& json, int version, SequenceReader& reader, Session& out, juce::String& error)
    {
        if (version < 1 || version > schemaVersion)
        {
            error = "Unsupported project version " + juce::String (version);
            return false;
        }

        if (! json.isObject())
        {
            error = "No session in the project";
            return false;
        }

        Session s;
        s.sampleRate = (double) json.getProperty ("sampleRate", 48000.0);
        if (s.sampleRate < 8000.0 || s.sampleRate > 384000.0)
        {
            error = "Invalid sample rate";
            return false;
        }

        int highestId = 0;

        if (const auto* tracks = json["tracks"].getArray())
        {
            for (const auto& tv : *tracks)
            {
                auto t = trackFrom (tv, reader, s.sampleRate);
                if (t.kind == Track::Kind::master || t.id <= 0 || s.findTrack (t.id) != nullptr)
                    continue;

                highestId = juce::jmax (highestId, t.id);
                for (const auto& c : t.clips)
                    highestId = juce::jmax (highestId, c.id);

                s.tracks.push_back (std::move (t));
            }
        }

        if (json["master"].isObject())
        {
            s.master = trackFrom (json["master"], reader, s.sampleRate);
            s.master.id = 0;
            s.master.kind = Track::Kind::master;
            s.master.clips.clear();
        }

        // Routing to something that no longer exists goes to the master.
        for (auto& t : s.tracks)
        {
            const auto* target = s.findTrack (t.outputId);
            if (target == nullptr || target->isAudio() || ! t.isAudio())
                t.outputId = 0;

            t.sends.erase (std::remove_if (t.sends.begin(), t.sends.end(), [&s] (const Send& x)
                           {
                               const auto* b = s.findTrack (x.busId);
                               return b == nullptr || ! b->isBus();
                           }), t.sends.end());
        }

        if (const auto* markers = json["markers"].getArray())
            for (const auto& m : *markers)
                s.markers.push_back ({ (juce::int64) m[0], m[1].toString() });

        std::sort (s.markers.begin(), s.markers.end(), [] (const Marker& a, const Marker& b) { return a.position < b.position; });

        s.nextId = juce::jmax ((int) json["nextId"], highestId + 1);

        if (reader.failed())
        {
            error = reader.getError();
            return false;
        }

        out = std::move (s);
        return true;
    }
}
