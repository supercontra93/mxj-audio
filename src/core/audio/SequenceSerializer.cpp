// SPDX-License-Identifier: AGPL-3.0-only
#include "core/audio/SequenceSerializer.h"
#include "core/audio/DerivedSources.h"

namespace mxj
{
    namespace
    {
        juce::var object (std::initializer_list<std::pair<const char*, juce::var>> properties)
        {
            auto* o = new juce::DynamicObject();
            for (const auto& [name, value] : properties)
                o->setProperty (name, value);
            return juce::var (o);
        }
    }

    //==============================================================================
    juce::var SequenceWriter::write (const AudioSequence& seq)
    {
        juce::Array<juce::var> blocks;

        for (const auto& b : seq.getBlocks())
        {
            const auto index = addSource (b.source);
            if (index < 0)
                return {};

            blocks.add (juce::Array<juce::var> { index, b.sourceStart, b.length });
        }

        return object ({ { "channels", seq.getNumChannels() }, { "rate", seq.getSampleRate() }, { "blocks", blocks } });
    }

    int SequenceWriter::addSource (const SampleSourcePtr& source)
    {
        if (failed || source == nullptr)
            return -1;

        if (const auto it = indices.find (source.get()); it != indices.end())
            return it->second;

        juce::var description;

        // Derived sources first write their inputs, so the table stays in dependency order.
        if (const auto* s = dynamic_cast<const SilenceSource*> (source.get()))
        {
            description = object ({ { "type", "silence" }, { "channels", s->getNumChannels() },
                                    { "rate", s->getSampleRate() }, { "frames", s->getNumFrames() } });
        }
        else if (const auto* g = dynamic_cast<const GainRampSource*> (source.get()))
        {
            const auto input = write (g->getInput());
            description = object ({ { "type", "gain" }, { "input", input }, { "start", g->getStartGain() },
                                    { "end", g->getEndGain() }, { "curve", g->getCurve() == GainRampSource::Curve::sine ? "sine" : "linear" } });
            failed = failed || input.isVoid();
        }
        else if (const auto* m = dynamic_cast<const MixSource*> (source.get()))
        {
            const auto a = write (m->getFirst());
            const auto b = write (m->getSecond());
            description = object ({ { "type", "mix" }, { "a", a }, { "b", b } });
            failed = failed || a.isVoid() || b.isVoid();
        }
        else if (const auto* c = dynamic_cast<const ChannelMapSource*> (source.get()))
        {
            const auto input = write (c->getInput());
            description = object ({ { "type", "channels" }, { "input", input }, { "channels", c->getNumChannels() } });
            failed = failed || input.isVoid();
        }
        else
        {
            description = source->describe();
        }

        if (failed || ! description.isObject() || ! description.hasProperty ("type"))
        {
            failed = true;
            return -1;
        }

        const auto index = sources.size();
        sources.append (description);
        indices[source.get()] = index;
        return index;
    }

    //==============================================================================
    SequenceReader::SequenceReader (const juce::var& sources, const LeafFactory& factory)
    {
        if (! sources.isArray())
        {
            error = "No source table";
            return;
        }

        for (const auto& d : *sources.getArray())
        {
            const auto type = d["type"].toString();
            SampleSourcePtr source;

            if (type == "silence")
            {
                source = std::make_shared<SilenceSource> ((int) d["channels"], (double) d["rate"], (juce::int64) d["frames"]);
            }
            else if (type == "gain")
            {
                source = std::make_shared<GainRampSource> (read (d["input"]), (float) d["start"], (float) d["end"],
                                                           d["curve"].toString() == "sine" ? GainRampSource::Curve::sine
                                                                                           : GainRampSource::Curve::linear);
            }
            else if (type == "mix")
            {
                auto a = read (d["a"]);
                auto b = read (d["b"]);
                if (! a.hasSameFormat (b) && ! a.isEmpty() && ! b.isEmpty())
                    error = "Mixed sequences differ in format";
                source = std::make_shared<MixSource> (std::move (a), std::move (b));
            }
            else if (type == "channels")
            {
                source = std::make_shared<ChannelMapSource> (read (d["input"]), (int) d["channels"]);
            }
            else if (factory != nullptr)
            {
                source = factory (d, error);
            }

            if (source == nullptr && error.isEmpty())
                error = "Unknown source type: " + type;

            if (error.isNotEmpty())
                return;

            table.push_back (std::move (source));
        }
    }

    AudioSequence SequenceReader::read (const juce::var& v)
    {
        AudioSequence seq ((int) v["channels"], (double) v["rate"]);
        if (! v["blocks"].isArray())
            return seq;

        for (const auto& b : *v["blocks"].getArray())
        {
            const auto index = (int) b[0];
            if (! juce::isPositiveAndBelow (index, (int) table.size()))
            {
                error = "Bad source index";
                return seq;
            }

            if (! seq.append ({ table[(size_t) index], (juce::int64) b[1], (juce::int64) b[2] }))
            {
                error = "A block does not fit its source";
                return seq;
            }
        }

        return seq;
    }
}
