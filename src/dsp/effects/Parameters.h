// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>
#include <vector>

namespace mxj::dsp
{
    /** Describes one parameter of an effect. The interface draws a control for it
        automatically (master plan, section 8: "parameters are declared, MXJ draws
        the interface"), so effect authors never write interface code.

        Names and units are English source strings; the app translates them. */
    struct ParameterInfo
    {
        enum class Kind
        {
            number, ///< a slider between minValue and maxValue
            choice, ///< one of `choices` (value = index)
            toggle  ///< on (1) or off (0)
        };

        juce::String id;           ///< stable, stored in presets: never rename
        juce::String name;
        Kind kind = Kind::number;
        float minValue = 0.0f;
        float maxValue = 1.0f;
        float defaultValue = 0.0f;
        juce::String unit;         ///< "dB", "Hz", "ms", "%", ":1" ...
        float midValue = 0.0f;     ///< value at the middle of the slider (log-like scales); 0 = linear
        int decimals = 1;
        juce::StringArray choices;

        /** Keeps a value inside the range (and on a whole number for choices and toggles). */
        float limit (float v) const;

        /** The value as text, with its unit: "-12.0 dB", "1.20 kHz", "On". */
        juce::String format (float v) const;

        static ParameterInfo number (juce::String id, juce::String name, float minValue, float maxValue,
                                     float defaultValue, juce::String unit, int decimals = 1, float midValue = 0.0f);
        static ParameterInfo choice (juce::String id, juce::String name, juce::StringArray choices, int defaultIndex = 0);
        static ParameterInfo toggle (juce::String id, juce::String name, bool defaultOn);
    };

    using ParameterList = std::vector<ParameterInfo>;

    /** The part of an effect's settings that is not a list of numbers: the
        state of a hosted VST3 or CLAP plugin (master plan, section 7). The
        plugins layer implements it; the dsp layer only passes it along.

        Shared by the processors made from the same ParameterValues, so the
        plugin window and the sound always belong to the same plugin.
        Message thread (save and restore may also run on a worker).
    */
    class EffectState
    {
    public:
        virtual ~EffectState() = default;

        /** The current state as text (base64), for presets, racks and projects. */
        virtual juce::String save() = 0;

        /** Loads a state from save(). Does nothing if it is the state it already has. */
        virtual void restore (const juce::String& saved) = 0;
    };

    /** The current values of one effect's parameters, shared between the
        interface (writes) and the processor (reads, also on the audio thread).
        Lock free: each value is an atomic float, so a slider moves the sound
        while it plays (live preview).

        Also carries a few read-outs the processor reports back to the interface,
        such as the gain reduction of a compressor. */
    class ParameterValues
    {
    public:
        explicit ParameterValues (const ParameterList& list);

        int size() const noexcept { return count; }

        float get (int index) const noexcept     { return values[(size_t) index].load (std::memory_order_relaxed); }
        void set (int index, float v) noexcept   { values[(size_t) index].store (v, std::memory_order_relaxed); }

        /** All values, in parameter order. */
        std::vector<float> getAll() const;

        /** Sets values from a list (missing ones are left alone). */
        void setAll (const std::vector<float>& newValues);

        /** Last gain reduction of a dynamics effect, in dB (0 = none). Written by the processor. */
        std::atomic<float> gainReductionDb { 0.0f };

        /** Hosted plugins only (else nullptr). Set when the values are made. */
        std::shared_ptr<EffectState> state;

    private:
        int count;
        std::unique_ptr<std::atomic<float>[]> values;
    };

    using ParameterValuesPtr = std::shared_ptr<ParameterValues>;

    /** Converts between values and a position 0..1 on a slider, honouring midValue. */
    float toProportion (const ParameterInfo&, float value);
    float fromProportion (const ParameterInfo&, float proportion);

    inline float dbToGain (float db) noexcept   { return db <= -150.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }
    inline float gainToDb (float gain) noexcept { return gain <= 1.0e-8f ? -160.0f : 20.0f * std::log10 (gain); }
}
