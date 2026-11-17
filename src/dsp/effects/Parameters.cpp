// SPDX-License-Identifier: AGPL-3.0-only
#include "dsp/effects/Parameters.h"

#include <cmath>

namespace mxj::dsp
{
    float ParameterInfo::limit (float v) const
    {
        if (! std::isfinite (v))
            v = defaultValue;

        if (kind == Kind::toggle)
            return v >= 0.5f ? 1.0f : 0.0f;

        if (kind == Kind::choice)
            return (float) juce::jlimit (0, juce::jmax (0, choices.size() - 1), juce::roundToInt (v));

        return juce::jlimit (minValue, maxValue, v);
    }

    juce::String ParameterInfo::format (float v) const
    {
        v = limit (v);

        if (kind == Kind::toggle)
            return v > 0.5f ? TRANS ("On") : TRANS ("Off");

        if (kind == Kind::choice)
            return TRANS (choices[(int) v]);

        if (unit == "Hz" && std::abs (v) >= 1000.0f)
            return juce::String (v / 1000.0f, 2) + " kHz";

        if (unit == "ms" && std::abs (v) >= 1000.0f)
            return juce::String (v / 1000.0f, 2) + " s";

        auto text = juce::String (v, decimals);
        if (unit.isEmpty())
            return text;

        return text + (unit.startsWith (":") || unit == "%" ? "" : " ") + unit;
    }

    ParameterInfo ParameterInfo::number (juce::String id, juce::String name, float minValue, float maxValue,
                                         float defaultValue, juce::String unit, int decimals, float midValue)
    {
        ParameterInfo p;
        p.id = std::move (id);
        p.name = std::move (name);
        p.kind = Kind::number;
        p.minValue = minValue;
        p.maxValue = maxValue;
        p.defaultValue = defaultValue;
        p.unit = std::move (unit);
        p.decimals = decimals;
        p.midValue = midValue;
        return p;
    }

    ParameterInfo ParameterInfo::choice (juce::String id, juce::String name, juce::StringArray choices, int defaultIndex)
    {
        ParameterInfo p;
        p.id = std::move (id);
        p.name = std::move (name);
        p.kind = Kind::choice;
        p.minValue = 0.0f;
        p.maxValue = (float) juce::jmax (0, choices.size() - 1);
        p.defaultValue = (float) defaultIndex;
        p.choices = std::move (choices);
        p.decimals = 0;
        return p;
    }

    ParameterInfo ParameterInfo::toggle (juce::String id, juce::String name, bool defaultOn)
    {
        ParameterInfo p;
        p.id = std::move (id);
        p.name = std::move (name);
        p.kind = Kind::toggle;
        p.minValue = 0.0f;
        p.maxValue = 1.0f;
        p.defaultValue = defaultOn ? 1.0f : 0.0f;
        p.decimals = 0;
        return p;
    }

    //==============================================================================
    ParameterValues::ParameterValues (const ParameterList& list)
        : count ((int) list.size()), values (std::make_unique<std::atomic<float>[]> (list.size()))
    {
        for (size_t i = 0; i < list.size(); ++i)
            values[i].store (list[i].defaultValue);
    }

    std::vector<float> ParameterValues::getAll() const
    {
        std::vector<float> result ((size_t) count);
        for (int i = 0; i < count; ++i)
            result[(size_t) i] = get (i);

        return result;
    }

    void ParameterValues::setAll (const std::vector<float>& newValues)
    {
        for (int i = 0; i < juce::jmin (count, (int) newValues.size()); ++i)
            set (i, newValues[(size_t) i]);
    }

    //==============================================================================
    // A slider with a middle value uses the curve  v = min + (max - min) * p^k,
    // with k chosen so that p = 0.5 gives midValue (as JUCE's skew factor).
    namespace
    {
        float skewFor (const ParameterInfo& p)
        {
            if (p.kind != ParameterInfo::Kind::number || p.midValue <= p.minValue || p.midValue >= p.maxValue)
                return 1.0f;

            const auto t = (p.midValue - p.minValue) / (p.maxValue - p.minValue);
            return std::log (t) / std::log (0.5f);
        }
    }

    float toProportion (const ParameterInfo& p, float value)
    {
        if (p.maxValue <= p.minValue)
            return 0.0f;

        const auto linear = juce::jlimit (0.0f, 1.0f, (p.limit (value) - p.minValue) / (p.maxValue - p.minValue));
        const auto k = skewFor (p);
        return k == 1.0f ? linear : std::pow (linear, 1.0f / k);
    }

    float fromProportion (const ParameterInfo& p, float proportion)
    {
        proportion = juce::jlimit (0.0f, 1.0f, proportion);
        const auto k = skewFor (p);
        const auto linear = k == 1.0f ? proportion : std::pow (proportion, k);
        return p.limit (p.minValue + linear * (p.maxValue - p.minValue));
    }
}
