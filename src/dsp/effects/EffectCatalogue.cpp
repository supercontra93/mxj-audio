// SPDX-License-Identifier: AGPL-3.0-only
#include "dsp/effects/EffectCatalogue.h"
#include "dsp/effects/BuiltInEffects.h"

namespace mxj::dsp
{
    //==============================================================================
    int EffectType::indexOf (const juce::String& parameterId) const
    {
        for (size_t i = 0; i < parameters.size(); ++i)
            if (parameters[i].id == parameterId)
                return (int) i;

        return -1;
    }

    std::vector<float> EffectType::getDefaults() const
    {
        std::vector<float> v;
        v.reserve (parameters.size());
        for (const auto& p : parameters)
            v.push_back (p.defaultValue);

        return v;
    }

    ParameterValuesPtr EffectType::createValues() const
    {
        return std::make_shared<ParameterValues> (parameters);
    }

    //==============================================================================
    namespace catalogue
    {
        const std::vector<EffectType>& all()
        {
            static const std::vector<EffectType> types = []
            {
                using namespace builtin;
                return std::vector<EffectType> {
                    amplify(), normalize(), matchLoudness(), compressor(), limiter(), gate(), deEsser(),
                    parametricEq(), graphicEq(), filter(),
                    delay(), reverb(),
                    chorus(), flanger(), phaser(),
                    stereoTools(),
                    speed(),
                    noiseReduction(), adaptiveNoiseReduction(), humRemoval(), clickRemoval(), clipRepair(),
                    toneGenerator(), noiseGenerator()
                };
            }();

            return types;
        }

        const EffectType* find (const juce::String& id)
        {
            for (const auto& t : all())
                if (t.id == id)
                    return &t;

            return nullptr;
        }

        juce::StringArray categories()
        {
            juce::StringArray result;
            for (const auto& t : all())
                result.addIfNotAlreadyThere (t.category);

            return result;
        }
    }

    //==============================================================================
    EffectSettings EffectSettings::defaultsFor (const juce::String& typeId)
    {
        EffectSettings s;
        if (const auto* type = catalogue::find (typeId))
        {
            s.typeId = typeId;
            s.values = type->getDefaults();
        }

        return s;
    }

    juce::var EffectSettings::toVar() const
    {
        auto* object = new juce::DynamicObject();
        object->setProperty ("type", typeId);
        object->setProperty ("enabled", enabled);

        auto* values_ = new juce::DynamicObject();
        if (const auto* type = catalogue::find (typeId))
            for (size_t i = 0; i < juce::jmin (values.size(), type->parameters.size()); ++i)
                values_->setProperty (type->parameters[i].id, values[i]);

        object->setProperty ("values", juce::var (values_));
        return juce::var (object);
    }

    EffectSettings EffectSettings::fromVar (const juce::var& v)
    {
        auto s = defaultsFor (v.getProperty ("type", {}).toString());
        const auto* type = catalogue::find (s.typeId);
        if (type == nullptr)
            return {};

        s.enabled = (bool) v.getProperty ("enabled", true);

        if (const auto* stored = v.getProperty ("values", {}).getDynamicObject())
            for (const auto& [name, value] : stored->getProperties())
            {
                const auto index = type->indexOf (name.toString());
                if (index >= 0 && (value.isDouble() || value.isInt() || value.isInt64() || value.isBool()))
                    s.values[(size_t) index] = type->parameters[(size_t) index].limit ((float) (double) value);
            }

        return s;
    }

    juce::var chainToVar (const ChainSettings& chain)
    {
        juce::Array<juce::var> list;
        for (const auto& e : chain)
            list.add (e.toVar());

        return list;
    }

    ChainSettings chainFromVar (const juce::var& v)
    {
        ChainSettings chain;
        if (const auto* list = v.getArray())
            for (const auto& item : *list)
            {
                auto s = EffectSettings::fromVar (item);
                if (s.typeId.isNotEmpty())
                    chain.push_back (std::move (s));
            }

        return chain;
    }

    //==============================================================================
    void ProcessorChain::add (std::unique_ptr<EffectProcessor> processor, std::shared_ptr<std::atomic<bool>> enabled)
    {
        if (processor != nullptr)
            entries.push_back ({ std::move (processor), std::move (enabled) });
    }

    std::unique_ptr<ProcessorChain> ProcessorChain::create (const ChainSettings& settings)
    {
        auto chain = std::make_unique<ProcessorChain>();

        for (const auto& s : settings)
        {
            const auto* type = catalogue::find (s.typeId);
            if (type == nullptr || ! s.enabled || type->create == nullptr || type->kind == EffectType::Kind::offline)
                continue;

            auto values = type->createValues();
            values->setAll (s.values);
            chain->add (type->create (values));
        }

        return chain;
    }

    void ProcessorChain::prepare (double sampleRate, int maximumBlockSize, int numChannels)
    {
        preparedRate = sampleRate;
        blockSize = juce::jmax (1, maximumBlockSize);

        for (auto& e : entries)
            e.processor->prepare (sampleRate, blockSize, numChannels);
    }

    void ProcessorChain::reset()
    {
        for (auto& e : entries)
            e.processor->reset();
    }

    void ProcessorChain::process (float* const* channels, int numChannels, int numFrames)
    {
        if (blockSize <= 0)
            return;

        for (int start = 0; start < numFrames; start += blockSize)
        {
            const auto n = juce::jmin (blockSize, numFrames - start);
            float* offset[16];
            const auto chans = juce::jmin (numChannels, 16);
            for (int c = 0; c < chans; ++c)
                offset[c] = channels[c] + start;

            for (auto& e : entries)
                if (e.enabled == nullptr || e.enabled->load (std::memory_order_relaxed))
                    e.processor->process (offset, chans, n);
        }
    }

    int ProcessorChain::getLatencyFrames() const
    {
        int total = 0;
        for (const auto& e : entries)
            if (e.enabled == nullptr || e.enabled->load())
                total += e.processor->getLatencyFrames();

        return total;
    }
}
