// SPDX-License-Identifier: AGPL-3.0-only
#include "TestHelpers.h"
#include "core/audio/DerivedSources.h"
#include "core/audio/SequenceSerializer.h"
#include "core/edit/EditOperations.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>

using namespace mxj;
using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double rate = 48000.0;

    /** Frame i of channel c holds a unique value, so every edit can be checked exactly. */
    float valueAt (int channel, juce::int64 frame) { return (float) ((channel + 1) * 100000 + frame) / 1.0e6f; }

    AudioSequence makeAudio (int channels, int frames, juce::int64 blockFrames = 1000)
    {
        juce::AudioBuffer<float> b (channels, frames);
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < frames; ++i)
                b.setSample (c, i, valueAt (c, i));
        return AudioSequence::fromSource (std::make_shared<MemorySampleSource> (std::move (b), rate), blockFrames);
    }

    std::vector<float> readAll (const AudioSequence& seq, int channel = 0)
    {
        juce::AudioBuffer<float> b (juce::jmax (1, seq.getNumChannels()), (int) juce::jmax ((juce::int64) 1, seq.getNumFrames()));
        seq.read (b.getArrayOfWritePointers(), b.getNumChannels(), 0, (int) seq.getNumFrames());
        return { b.getReadPointer (channel), b.getReadPointer (channel) + seq.getNumFrames() };
    }
}

TEST_CASE ("Cutting a range keeps the audio around it in order", "[edit]")
{
    AudioState s { makeAudio (2, 10000), {} };
    const auto r = edits::erase (s, { 2500, 4000 });

    REQUIRE (r.changed);
    REQUIRE (r.state.audio.getNumFrames() == 8500);
    const auto out = readAll (r.state.audio, 1);
    CHECK (out[2499] == valueAt (1, 2499));
    CHECK (out[2500] == valueAt (1, 4000));
    CHECK (out[8499] == valueAt (1, 9999));

    // The original state is untouched: undo only needs the old value.
    CHECK (s.audio.getNumFrames() == 10000);
}

TEST_CASE ("Paste inserts, replaces a selection, and mixes", "[edit]")
{
    const AudioState s { makeAudio (1, 1000, 100), {} };
    const auto clip = s.audio.getRange (100, 50);

    SECTION ("insert at the cursor")
    {
        const auto r = edits::paste (s, { 10, 10 }, clip);
        REQUIRE (r.state.audio.getNumFrames() == 1050);
        CHECK (r.selection == FrameRange (10, 60));
        const auto out = readAll (r.state.audio);
        CHECK (out[9] == valueAt (0, 9));
        CHECK (out[10] == valueAt (0, 100));
        CHECK (out[59] == valueAt (0, 149));
        CHECK (out[60] == valueAt (0, 10));
    }

    SECTION ("replace the selection")
    {
        const auto r = edits::paste (s, { 0, 500 }, clip);
        REQUIRE (r.state.audio.getNumFrames() == 550);
        CHECK (readAll (r.state.audio)[50] == valueAt (0, 500));
    }

    SECTION ("mix paste adds and can grow the audio")
    {
        const auto r = edits::mixPaste (s, { 980, 980 }, clip);
        REQUIRE (r.state.audio.getNumFrames() == 1030);
        const auto out = readAll (r.state.audio);
        CHECK_THAT (out[980], WithinAbs (valueAt (0, 980) + valueAt (0, 100), 1.0e-6));
        CHECK_THAT (out[1000], WithinAbs (valueAt (0, 120), 1.0e-6));
    }

    SECTION ("a mono clip pasted into stereo audio plays on both channels")
    {
        const AudioState stereo { makeAudio (2, 100), {} };
        const auto r = edits::paste (stereo, { 0, 0 }, clip);
        REQUIRE (r.state.audio.getNumChannels() == 2);
        CHECK (readAll (r.state.audio, 1)[0] == valueAt (0, 100));
    }

    SECTION ("a clip with another sample rate is refused")
    {
        juce::AudioBuffer<float> b (1, 10);
        b.clear();
        const auto other = AudioSequence::fromSource (std::make_shared<MemorySampleSource> (b, 44100.0));
        CHECK_FALSE (edits::paste (s, { 0, 0 }, other).changed);
    }
}

TEST_CASE ("Trim, silence and insert silence", "[edit]")
{
    const AudioState s { makeAudio (1, 1000, 100), { { 100, "a" }, { 500, "b" }, { 900, "c" } } };

    const auto trimmed = edits::trim (s, { 400, 600 });
    REQUIRE (trimmed.state.audio.getNumFrames() == 200);
    CHECK (readAll (trimmed.state.audio)[0] == valueAt (0, 400));
    REQUIRE (trimmed.state.markers.size() == 1);
    CHECK (trimmed.state.markers[0] == Marker { 100, "b" });

    const auto silenced = edits::silence (s, { 10, 20 });
    const auto out = readAll (silenced.state.audio);
    CHECK (out[9] == valueAt (0, 9));
    CHECK (out[10] == 0.0f);
    CHECK (out[19] == 0.0f);
    CHECK (out[20] == valueAt (0, 20));
    CHECK (silenced.state.markers == s.markers);

    const auto inserted = edits::insertSilence (s, { 300, 300 }, 50);
    REQUIRE (inserted.state.audio.getNumFrames() == 1050);
    CHECK (readAll (inserted.state.audio)[320] == 0.0f);
    CHECK (inserted.state.markers[1].position == 550);
}

TEST_CASE ("Fades start or end in silence and keep the rest", "[edit]")
{
    juce::AudioBuffer<float> ones (2, 1000);
    for (int c = 0; c < 2; ++c)
        juce::FloatVectorOperations::fill (ones.getWritePointer (c), 1.0f, 1000);

    const AudioState s { AudioSequence::fromSource (std::make_shared<MemorySampleSource> (ones, rate)), {} };

    const auto in = readAll (edits::fadeIn (s, { 0, 100 }).state.audio);
    CHECK (in[0] == 0.0f);
    CHECK (in[50] > 0.5f);   // quarter sine rises quickly
    CHECK (in[99] == 1.0f);
    CHECK (in[100] == 1.0f);
    for (int i = 1; i < 100; ++i)
        REQUIRE (in[(size_t) i] >= in[(size_t) i - 1]);

    const auto out = readAll (edits::fadeOut (s, { 900, 1000 }).state.audio);
    CHECK (out[899] == 1.0f);
    CHECK (out[900] == 1.0f);
    CHECK (out[999] == 0.0f);

    // The peaks used for drawing follow the fade.
    float lo, hi;
    REQUIRE (edits::fadeOut (s, { 900, 1000 }).state.audio.getMinMax (0, 995, 1000, lo, hi));
    CHECK (hi < 0.2f);
}

TEST_CASE ("Markers follow the audio through edits", "[edit][markers]")
{
    MarkerList markers;
    edits::addMarker (markers, { 500, "b" });
    edits::addMarker (markers, { 100, "a" });
    edits::addMarker (markers, { 900, "c" });
    REQUIRE (markers.size() == 3);
    CHECK (markers[0].name == "a");
    CHECK (markers[2].name == "c");

    const AudioState s { makeAudio (1, 1000, 100), markers };
    const auto cut = edits::erase (s, { 400, 600 });
    REQUIRE (cut.state.markers.size() == 2);
    CHECK (cut.state.markers[0] == Marker { 100, "a" });
    CHECK (cut.state.markers[1] == Marker { 700, "c" });

    CHECK (edits::nextMarkerName (markers, "Marker") == "Marker 1");
    edits::addMarker (markers, { 1, "Marker 7" });
    CHECK (edits::nextMarkerName (markers, "Marker") == "Marker 8");
}

TEST_CASE ("Edits on a long file are instant and need no audio copy", "[edit][speed]")
{
    // Two hours of stereo audio at 48 kHz, made of silence blocks: nothing is read.
    const auto twoHours = (juce::int64) (2 * 3600 * rate);
    AudioSequence audio (2, rate);
    for (juce::int64 pos = 0; pos < twoHours; pos += AudioSequence::defaultBlockFrames)
        audio.append (derived::silence (2, rate, juce::jmin (AudioSequence::defaultBlockFrames, twoHours - pos)));

    AudioState s { audio, {} };
    const auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < 100; ++i)
    {
        const auto at = (juce::int64) (i * 60 * rate);
        auto clip = s.audio.getRange (at, 48000);
        s = edits::erase (s, { at, at + 48000 }).state;
        s = edits::paste (s, { at + 1000, at + 1000 }, clip).state;
    }

    const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
    CHECK (s.audio.getNumFrames() == twoHours);
    CHECK (ms / 200.0 < 50.0); // master plan: under 50 ms per edit
}

TEST_CASE ("A sequence with derived sources survives the recovery journal", "[edit][recovery]")
{
    /** A leaf the journal can describe (real ones are files and block files). */
    struct Leaf : SampleSource
    {
        explicit Leaf (SampleSourcePtr s) : inner (std::move (s)) {}
        int getNumChannels() const override         { return inner->getNumChannels(); }
        juce::int64 getNumFrames() const override   { return inner->getNumFrames(); }
        double getSampleRate() const override       { return inner->getSampleRate(); }
        bool read (float* const* d, int n, juce::int64 st, int f) const override { return inner->read (d, n, st, f); }
        juce::var describe() const override
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("type", "test");
            return juce::var (o);
        }
        SampleSourcePtr inner;
    };

    const auto memory = makeAudio (2, 2000, 5000).getBlocks()[0].source;
    const auto leaf = std::make_shared<Leaf> (memory);

    AudioState st { AudioSequence::fromSource (leaf, 500), {} };
    st = edits::fadeIn (st, { 0, 300 }).state;
    st = edits::silence (st, { 500, 600 }).state;
    st = edits::mixPaste (st, { 1000, 1000 }, st.audio.getRange (0, 400)).state;
    st = edits::paste (st, { 1500, 1500 }, derived::withChannels (st.audio.getRange (0, 100), 1)).state;

    SequenceWriter writer;
    const auto written = writer.write (st.audio);
    REQUIRE (written.isObject());

    // Through JSON text, as on disk.
    auto* root = new juce::DynamicObject();
    root->setProperty ("sources", writer.getSources());
    root->setProperty ("audio", written);
    const auto json = juce::JSON::parse (juce::JSON::toString (juce::var (root)));

    int created = 0;
    SequenceReader reader (json["sources"], [&] (const juce::var& d, juce::String&) -> SampleSourcePtr
    {
        ++created;
        return d["type"].toString() == "test" ? std::make_shared<Leaf> (memory) : nullptr;
    });

    const auto restored = reader.read (json["audio"]);
    REQUIRE_FALSE (reader.failed());
    CHECK (created == 1); // one leaf, shared by every block that uses it
    REQUIRE (restored.getNumFrames() == st.audio.getNumFrames());
    CHECK (readAll (restored, 0) == readAll (st.audio, 0));
    CHECK (readAll (restored, 1) == readAll (st.audio, 1));
}
