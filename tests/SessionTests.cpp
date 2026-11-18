// SPDX-License-Identifier: AGPL-3.0-only
#include "core/audio/DerivedSources.h"
#include "core/session/SessionEdits.h"
#include "core/session/SessionFile.h"
#include "engine/mix/SessionMixer.h"
#include "engine/playback/PlaybackEngine.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace mxj;
using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double rate = 48000.0;

    AudioSequence constant (int channels, int frames, float value)
    {
        juce::AudioBuffer<float> b (channels, frames);
        for (int c = 0; c < channels; ++c)
            juce::FloatVectorOperations::fill (b.getWritePointer (c), value, frames);

        return AudioSequence::fromSource (std::make_shared<MemorySampleSource> (std::move (b), rate), 10000);
    }

    juce::AudioBuffer<float> render (const Session& s, juce::int64 start, int frames)
    {
        SessionMixer mixer;
        mixer.setSession (s);
        mixer.seek (start);
        juce::AudioBuffer<float> out (2, frames);
        mixer.read (out.getArrayOfWritePointers(), frames);
        return out;
    }

    /** A session with one audio track holding one clip of `value` at `position`. */
    Session oneClip (float value, int frames = 4800, juce::int64 position = 0, int channels = 1)
    {
        Session s;
        const auto track = sessionEdits::addTrack (s, Track::Kind::audio, "Track 1");
        sessionEdits::addClip (s, track, sessionEdits::makeClip (s, "clip", constant (channels, frames, value), position));
        return s;
    }

    int firstTrack (const Session& s) { return s.tracks.front().id; }
    int firstClip (const Session& s)  { return s.tracks.front().clips.front().id; }
}

TEST_CASE ("Automation lanes join their points with straight lines", "[session]")
{
    AutomationLane lane;
    CHECK_THAT (lane.valueAt (100, -3.0f), WithinAbs (-3.0, 1e-6)); // no points: the fader

    lane.add ({ 1000, 0.0f });
    lane.add ({ 0, -10.0f });
    lane.add ({ 2000, -20.0f });
    REQUIRE (lane.points.size() == 3);
    CHECK (lane.points.front().frame == 0);

    CHECK_THAT (lane.valueAt (-5, 0.0f), WithinAbs (-10.0, 1e-6));
    CHECK_THAT (lane.valueAt (500, 0.0f), WithinAbs (-5.0, 1e-6));
    CHECK_THAT (lane.valueAt (1500, 0.0f), WithinAbs (-10.0, 1e-6));
    CHECK_THAT (lane.valueAt (99999, 0.0f), WithinAbs (-20.0, 1e-6));

    lane.add ({ 1000, 6.0f }); // same frame: replaced
    CHECK (lane.points.size() == 3);
    CHECK_THAT (lane.valueAt (1000, 0.0f), WithinAbs (6.0, 1e-6));
}

TEST_CASE ("Clips are split, trimmed, moved and deleted without touching the audio", "[session]")
{
    auto s = oneClip (0.5f, 10000, 1000);
    const auto clip = firstClip (s);
    const auto track = firstTrack (s);

    const auto right = sessionEdits::splitClip (s, clip, 4000);
    REQUIRE (right != 0);
    REQUIRE (s.tracks[0].clips.size() == 2);
    const auto* l = s.findClip (clip);
    const auto* r = s.findClip (right);
    CHECK (l->length == 3000);
    CHECK (r->position == 4000);
    CHECK (r->sourceStart == 3000);
    CHECK (r->length == 7000);
    CHECK (sessionEdits::splitClip (s, clip, 1000) == 0); // not strictly inside

    // Trimming cannot reveal more than the clip's audio.
    CHECK (sessionEdits::trimClipStart (s, right, 0));
    CHECK (s.findClip (right)->position == 1000);
    CHECK (s.findClip (right)->sourceStart == 0);
    CHECK (s.findClip (right)->length == 10000);

    CHECK (sessionEdits::trimClipEnd (s, clip, 999999));
    CHECK (s.findClip (clip)->length == 10000);

    // Moving to a second track.
    const auto second = sessionEdits::addTrack (s, Track::Kind::audio, "Track 2");
    CHECK (sessionEdits::moveClip (s, right, second, 20000));
    CHECK (s.findTrackOfClip (right)->id == second);
    CHECK (s.findClip (right)->position == 20000);
    CHECK (s.getLength() == 30000);

    CHECK (sessionEdits::deleteClips (s, { clip }, false) == 1);
    CHECK (s.findTrack (track)->clips.empty());
}

TEST_CASE ("Ripple delete closes the gap on the same track", "[session]")
{
    Session s;
    const auto t = sessionEdits::addTrack (s, Track::Kind::audio, "T");
    const auto a = sessionEdits::addClip (s, t, sessionEdits::makeClip (s, "a", constant (1, 1000, 0.1f), 0));
    const auto b = sessionEdits::addClip (s, t, sessionEdits::makeClip (s, "b", constant (1, 1000, 0.1f), 1000));
    const auto c = sessionEdits::addClip (s, t, sessionEdits::makeClip (s, "c", constant (1, 1000, 0.1f), 2500));

    sessionEdits::deleteClips (s, { b }, true);
    CHECK (s.findClip (a)->position == 0);
    CHECK (s.findClip (c)->position == 1500);

    // A time range: the parts inside go, the rest moves left.
    sessionEdits::deleteRange (s, { 500, 1700 }, true);
    REQUIRE (s.tracks[0].clips.size() == 2);
    CHECK (s.tracks[0].clips[0].length == 500);
    CHECK (s.tracks[0].clips[1].position == 500);
    CHECK (s.tracks[0].clips[1].length == 800);
    CHECK (s.tracks[0].clips[1].sourceStart == 200);
}

TEST_CASE ("Snapping finds the nearest clip edge", "[session]")
{
    auto s = oneClip (0.1f, 1000, 5000);
    CHECK (sessionEdits::snap (s, 5980, 50) == 6000);
    CHECK (sessionEdits::snap (s, 5900, 50) == 5900);
    CHECK (sessionEdits::snap (s, 30, 50) == 0);
    CHECK (sessionEdits::snap (s, 5010, 50, { firstClip (s) }) == 5010); // the moving clip is ignored
    CHECK (sessionEdits::snap (s, 7010, 50, {}, { 7000 }) == 7000);
}

TEST_CASE ("Removing a bus sends its tracks to the master", "[session]")
{
    auto s = oneClip (0.1f);
    const auto bus = sessionEdits::addTrack (s, Track::Kind::bus, "Bus");
    auto& t = s.tracks.front();
    t.outputId = bus;
    t.sends.push_back ({ bus, -6.0f });

    CHECK (sessionEdits::removeTrack (s, bus));
    CHECK (s.tracks.front().outputId == 0);
    CHECK (s.tracks.front().sends.empty());
}

TEST_CASE ("Mute and solo decide which tracks are heard", "[session]")
{
    Session s;
    const auto a = sessionEdits::addTrack (s, Track::Kind::audio, "A");
    const auto b = sessionEdits::addTrack (s, Track::Kind::audio, "B");
    const auto bus = sessionEdits::addTrack (s, Track::Kind::bus, "Bus");
    s.findTrack (b)->outputId = bus;

    CHECK (s.isAudible (*s.findTrack (a)));
    s.findTrack (a)->solo = true;
    CHECK (s.isAudible (*s.findTrack (a)));
    CHECK_FALSE (s.isAudible (*s.findTrack (b)));
    CHECK_FALSE (s.isAudible (*s.findTrack (bus)));

    // Soloing the bus keeps the track feeding it.
    s.findTrack (a)->solo = false;
    s.findTrack (bus)->solo = true;
    CHECK (s.isAudible (*s.findTrack (b)));
    CHECK_FALSE (s.isAudible (*s.findTrack (a)));

    s.findTrack (bus)->solo = false;
    s.findTrack (a)->mute = true;
    CHECK_FALSE (s.isAudible (*s.findTrack (a)));
}

TEST_CASE ("The mixer plays clips with clip gain, volume and pan", "[session][mixer]")
{
    auto s = oneClip (0.5f, 4800, 1000);

    auto out = render (s, 0, 6000);
    CHECK_THAT (out.getSample (0, 500), WithinAbs (0.0, 1e-6));   // before the clip
    CHECK_THAT (out.getSample (0, 2000), WithinAbs (0.5, 1e-6));  // mono plays on both sides
    CHECK_THAT (out.getSample (1, 2000), WithinAbs (0.5, 1e-6));
    CHECK_THAT (out.getSample (0, 5900), WithinAbs (0.0, 1e-6));  // after it

    sessionEdits::setClipGain (s, firstClip (s), -6.0206f);
    s.tracks[0].pan = 1.0f; // hard right: the left side goes silent
    out = render (s, 1000, 2000);
    CHECK_THAT (out.getSample (0, 1500), WithinAbs (0.0, 1e-5));
    CHECK_THAT (out.getSample (1, 1500), WithinAbs (0.25, 1e-4));

    s.tracks[0].pan = 0.0f;
    s.tracks[0].volumeDb = 6.0206f;
    s.master.volumeDb = -6.0206f;
    out = render (s, 1000, 2000);
    CHECK_THAT (out.getSample (0, 1500), WithinAbs (0.25, 1e-4));
}

TEST_CASE ("The mixer follows mute, solo and live fader changes", "[session][mixer]")
{
    auto s = oneClip (0.5f, 48000);
    const auto b = sessionEdits::addTrack (s, Track::Kind::audio, "B");
    sessionEdits::addClip (s, b, sessionEdits::makeClip (s, "b", constant (1, 48000, 0.25f), 0));

    SessionMixer mixer;
    mixer.setSession (s);
    mixer.seek (0);
    juce::AudioBuffer<float> out (2, 2048);

    mixer.read (out.getArrayOfWritePointers(), 2048);
    CHECK_THAT (out.getSample (0, 2000), WithinAbs (0.75, 1e-5));

    // Solo B: only B stays (after a short ramp).
    s.findTrack (b)->solo = true;
    mixer.setSession (s);
    mixer.read (out.getArrayOfWritePointers(), 2048);
    mixer.read (out.getArrayOfWritePointers(), 2048);
    CHECK_THAT (out.getSample (0, 2000), WithinAbs (0.25, 1e-5));

    // A live fader move on B, without a new session.
    mixer.setTrackVolume (b, -120.0f);
    mixer.read (out.getArrayOfWritePointers(), 2048);
    mixer.read (out.getArrayOfWritePointers(), 2048);
    CHECK_THAT (out.getSample (0, 2000), WithinAbs (0.0, 1e-5));
}

TEST_CASE ("Volume automation overrides the fader", "[session][mixer]")
{
    auto s = oneClip (1.0f, 48000);
    s.tracks[0].volumeDb = -60.0f;
    s.tracks[0].volumeAutomation.add ({ 0, 0.0f });
    s.tracks[0].volumeAutomation.add ({ 48000, -6.0206f });

    const auto out = render (s, 0, 48000);
    // Evaluated once per chunk of the mixer (1024 frames) and ramped: close, not exact.
    CHECK_THAT (out.getSample (0, 1000), WithinAbs (1.0, 0.02));
    CHECK_THAT (out.getSample (0, 47000), WithinAbs (0.5, 0.02));
    CHECK (out.getSample (0, 24000) < 0.9f);
    CHECK (out.getSample (0, 24000) > 0.6f);
}

TEST_CASE ("Clip fades and automatic crossfades", "[session][mixer]")
{
    auto s = oneClip (1.0f, 10000);
    sessionEdits::setClipFades (s, firstClip (s), 1000, 1000);
    auto out = render (s, 0, 10000);
    CHECK_THAT (out.getSample (0, 0), WithinAbs (0.0, 1e-4));
    CHECK_THAT (out.getSample (0, 500), WithinAbs (std::sin (juce::MathConstants<double>::halfPi * 0.5), 1e-3));
    CHECK_THAT (out.getSample (0, 5000), WithinAbs (1.0, 1e-6));
    CHECK (out.getSample (0, 9999) < 0.01f);

    // Two overlapping clips fade into each other with equal power.
    Session x;
    const auto t = sessionEdits::addTrack (x, Track::Kind::audio, "T");
    sessionEdits::addClip (x, t, sessionEdits::makeClip (x, "a", constant (1, 10000, 1.0f), 0));
    sessionEdits::addClip (x, t, sessionEdits::makeClip (x, "b", constant (1, 10000, 1.0f), 8000));
    out = render (x, 0, 18000);
    CHECK_THAT (out.getSample (0, 4000), WithinAbs (1.0, 1e-6));
    CHECK_THAT (out.getSample (0, 9000), WithinAbs (2.0 * std::sin (juce::MathConstants<double>::halfPi * 0.5), 1e-3));
    CHECK_THAT (out.getSample (0, 15000), WithinAbs (1.0, 1e-6));
}

TEST_CASE ("Buses mix their tracks and sends, with effects", "[session][mixer]")
{
    auto s = oneClip (0.5f, 4800);
    const auto bus = sessionEdits::addTrack (s, Track::Kind::bus, "Bus");
    s.tracks[0].outputId = bus;
    s.findTrack (bus)->volumeDb = -6.0206f;

    auto out = render (s, 0, 2000);
    CHECK_THAT (out.getSample (0, 1500), WithinAbs (0.25, 1e-4));

    // Back to the master, plus a send to the bus at -6 dB: 0.5 + 0.5 * 0.5 * 0.5.
    s.tracks[0].outputId = 0;
    s.tracks[0].sends.push_back ({ bus, -6.0206f });
    out = render (s, 0, 2000);
    CHECK_THAT (out.getSample (0, 1500), WithinAbs (0.625, 1e-4));

    // An effect on the bus (Amplify -inf would be silence; use +6 dB).
    auto amp = dsp::EffectSettings::defaultsFor ("amplify");
    REQUIRE (amp.typeId.isNotEmpty());
    amp.values[0] = 6.0206f;
    s.findTrack (bus)->effects = dsp::chainToVar ({ amp });
    out = render (s, 0, 2000);
    CHECK_THAT (out.getSample (0, 1500), WithinAbs (0.75, 1e-3));
}

TEST_CASE ("Mixdown writes the session to new audio", "[session][mixer]")
{
    test::TempFolder temp;
    BlockStore store (temp.folder);

    auto s = oneClip (0.5f, 4800, 2400, 2);
    const auto mix = SessionMixer::mixdown (s, { 0, s.getLength() }, store);
    REQUIRE (mix.getNumChannels() == 2);
    CHECK (mix.getNumFrames() == 7200);
    CHECK (juce::approximatelyEqual (mix.getSampleRate(), rate));

    juce::AudioBuffer<float> b (2, 7200);
    REQUIRE (mix.read (b.getArrayOfWritePointers(), 2, 0, 7200));
    CHECK_THAT (b.getSample (0, 1000), WithinAbs (0.0, 1e-6));
    CHECK_THAT (b.getSample (1, 5000), WithinAbs (0.5, 1e-6));
}

TEST_CASE ("The mixer plays through the playback engine at another device rate", "[session][mixer]")
{
    auto s = oneClip (0.5f, 48000);
    auto mixer = std::make_shared<SessionMixer>();
    mixer->setSession (s);

    PlaybackEngine engine;
    engine.prepare (44100.0, 512);
    engine.setLookahead (8192);
    engine.setSource (mixer);
    REQUIRE (engine.play (0));

    juce::AudioBuffer<float> block (2, 512);
    float last = 0.0f;
    for (int i = 0; i < 40 && engine.isPlaying(); ++i)
    {
        engine.waitUntilBuffered (1000);
        juce::Thread::sleep (2);
        engine.audioDeviceIOCallbackWithContext (nullptr, 0, block.getArrayOfWritePointers(), 2, 512, {});
        last = block.getSample (0, 300);
    }

    CHECK_THAT (last, WithinAbs (0.5, 0.01)); // sample rate conversion: within 1 %
    engine.stop();
}

TEST_CASE ("A session survives being written to JSON and read back", "[session][file]")
{
    Session s;
    sessionEdits::addTrack (s, Track::Kind::audio, "T");
    sessionEdits::addClip (s, firstTrack (s), sessionEdits::makeClip (s, "clip",
                           AudioSequence::fromSource (std::make_shared<SilenceSource> (1, rate, 4800)), 100));
    const auto bus = sessionEdits::addTrack (s, Track::Kind::bus, "Reverb");
    auto& t = s.tracks.front();
    t.name = juce::String (juce::CharPointer_UTF8 ("Konu\xc5\x9fma"));
    t.volumeDb = -3.0f;
    t.pan = -0.25f;
    t.mute = true;
    t.sends.push_back ({ bus, -12.0f });
    t.volumeAutomation.add ({ 10, -1.0f });
    t.effects = dsp::chainToVar ({ dsp::EffectSettings::defaultsFor ("amplify") });
    sessionEdits::setClipFades (s, firstClip (s), 100, 200);
    sessionEdits::setClipGain (s, firstClip (s), 2.5f);
    s.markers.push_back ({ 300, "Intro" });

    SequenceWriter w;
    const auto json = sessionFile::toVar (s, w);
    REQUIRE (json.isObject());

    const auto text = juce::JSON::toString (json);
    const auto sources = juce::JSON::parse (juce::JSON::toString (w.getSources()));
    juce::String error;
    Session back;
    {
        SequenceReader reader (sources, nullptr);
        REQUIRE (sessionFile::fromVar (juce::JSON::parse (text), 1, reader, back, error));
    }

    REQUIRE (back.tracks.size() == 2);
    const auto& bt = back.tracks.front();
    CHECK (bt.name == t.name);
    CHECK_THAT (bt.volumeDb, WithinAbs (-3.0, 1e-6));
    CHECK_THAT (bt.pan, WithinAbs (-0.25, 1e-6));
    CHECK (bt.mute);
    REQUIRE (bt.sends.size() == 1);
    CHECK (bt.sends[0].busId == bus);
    CHECK (bt.volumeAutomation == t.volumeAutomation);
    CHECK (dsp::chainFromVar (bt.effects).size() == 1);
    REQUIRE (bt.clips.size() == 1);
    CHECK (bt.clips[0].position == 100);
    CHECK (bt.clips[0].fadeIn == 100);
    CHECK (bt.clips[0].fadeOut == 200);
    CHECK_THAT (bt.clips[0].gainDb, WithinAbs (2.5, 1e-6));
    CHECK (back.tracks[1].isBus());
    CHECK (back.markers.size() == 1);
    CHECK (back.nextId == s.nextId);

    CHECK_FALSE (sessionFile::fromVar (juce::JSON::parse (text), 99, *std::make_unique<SequenceReader> (juce::var(), nullptr), back, error));
}

TEST_CASE ("A 10 track session with speech, music and a jingle mixes down (phase 5 acceptance)", "[session][mixer]")
{
    test::TempFolder temp;
    BlockStore store (temp.folder);

    // 2 seconds; music under speech, a jingle at the start, a bus for the voices.
    const int second = (int) rate;
    Session s;
    const auto voices = sessionEdits::addTrack (s, Track::Kind::bus, "Voices");
    for (int i = 0; i < 10; ++i)
        sessionEdits::addTrack (s, Track::Kind::audio, "Track " + juce::String (i + 1));

    REQUIRE (s.getNumAudioTracks() == 10);

    // Tracks 1-6: speech (short clips at different places), routed to the voices bus.
    for (int i = 0; i < 6; ++i)
    {
        auto& t = s.tracks[(size_t) i + 1];
        t.outputId = voices;
        sessionEdits::addClip (s, t.id, sessionEdits::makeClip (s, "speech", constant (1, second / 4, 0.01f), second / 2 + i * second / 8));
    }

    // Tracks 7-9: music, ducked by volume automation while the speech runs.
    for (int i = 6; i < 9; ++i)
    {
        auto& t = s.tracks[(size_t) i + 1];
        sessionEdits::addClip (s, t.id, sessionEdits::makeClip (s, "music", constant (2, 2 * second, 0.02f), 0));
        t.volumeAutomation.add ({ 0, 0.0f });
        t.volumeAutomation.add ({ second / 2, -20.0f });
    }

    // Track 10: the jingle, faded out.
    const auto jingleTrack = s.tracks[10].id;
    sessionEdits::addClip (s, jingleTrack, sessionEdits::makeClip (s, "jingle", constant (2, second / 2, 0.1f), 0));
    sessionEdits::setClipFades (s, s.tracks[10].clips.front().id, 0, second / 4);

    const auto mix = SessionMixer::mixdown (s, { 0, s.getLength() }, store);
    REQUIRE (mix.getNumChannels() == 2);
    CHECK (mix.getNumFrames() == 2 * second);

    juce::AudioBuffer<float> b (2, 2 * second);
    REQUIRE (mix.read (b.getArrayOfWritePointers(), 2, 0, 2 * second));

    // At the start: jingle + 3 x music at full level.
    CHECK_THAT (b.getSample (0, 100), WithinAbs (0.1 + 3 * 0.02, 0.01));

    // At 1.1 s two voices overlap (clip i runs from 0.5 + i/8 s for 0.25 s)
    // through the bus, and the music is down by 20 dB: 2 x 0.01 + 3 x 0.002.
    CHECK_THAT (b.getSample (0, second + second / 10), WithinAbs (0.026, 0.002));

    // Muting the voices bus leaves only the music.
    s.findTrack (voices)->mute = true;
    const auto muted = SessionMixer::mixdown (s, { 0, s.getLength() }, store);
    REQUIRE (muted.read (b.getArrayOfWritePointers(), 2, 0, 2 * second));
    CHECK_THAT (b.getSample (0, second + second / 10), WithinAbs (0.006, 0.001));
}
