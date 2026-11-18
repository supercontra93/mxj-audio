// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "dsp/effects/Parameters.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <functional>

namespace mxj::dsp
{
    /** The sound processing part of one effect.

        The same processor serves live preview (on the audio thread) and applying
        the effect to the file (on a background thread), so they always sound the
        same. Real-time rules (CLAUDE.md, rule 4): prepare() may allocate;
        process() and reset() must not allocate, lock, touch files or log.

        A processor reads its parameter values at the start of each process()
        call from the shared ParameterValues, so the interface can change them
        while it runs.
    */
    class EffectProcessor
    {
    public:
        explicit EffectProcessor (ParameterValuesPtr v) : params (std::move (v)) {}
        virtual ~EffectProcessor() = default;

        /** Called before processing and whenever the format changes. */
        virtual void prepare (double sampleRate, int maximumBlockSize, int numChannels) = 0;

        /** Clears filter state, delay lines and envelopes. */
        virtual void reset() = 0;

        /** Processes `numFrames` frames in place. `numChannels` is at most the
            number given to prepare(), `numFrames` at most the block size. */
        virtual void process (float* const* channels, int numChannels, int numFrames) = 0;

        /** Frames the output lags behind the input (look-ahead). Applying the
            effect to a file removes this delay. */
        virtual int getLatencyFrames() const { return 0; }

        /** Why processing failed (a script error in an MXJ plugin), or empty.
            Read after applying the effect on a worker thread. */
        virtual juce::String getError() const { return {}; }

        const ParameterValues& getParameters() const noexcept { return *params; }

    protected:
        float param (int index) const noexcept { return params->get (index); }

        ParameterValuesPtr params;
    };

    /** A preset: named parameter values. Factory presets come with the effect,
        user presets are stored by the services layer (PresetStore). */
    struct Preset
    {
        juce::String name;
        std::vector<float> values; ///< in parameter order; missing ones keep their default
    };

    /** Everything about one kind of effect: its name, its parameters, factory
        presets and how to make a processor. Registered in the EffectCatalogue. */
    struct EffectType
    {
        enum class Kind
        {
            realtime,  ///< processes audio as it plays: live preview and effect rack
            generator, ///< makes new audio (tone, noise); inserted at the cursor
            offline,   ///< needs the whole selection first (normalize, loudness, speed)
            script     ///< MXJ (Lua) plugin effect: applied chunk by chunk on a worker thread,
                       ///< never on the audio thread, so no live preview and no rack
        };

        juce::String id;        ///< stable, stored in presets and racks: never rename
        juce::String name;      ///< English, translated by the app
        juce::String category;  ///< menu group, English
        juce::String description;
        Kind kind = Kind::realtime;
        ParameterList parameters;
        std::vector<Preset> factoryPresets;

        /** Makes a new processor that reads `values`. */
        std::function<std::unique_ptr<EffectProcessor> (ParameterValuesPtr values)> create;

        /** Optional: the effect's gain in dB at `hz`, for drawing a frequency
            response curve (equalisers and filters). */
        std::function<float (const std::vector<float>& values, double sampleRate, double hz)> response;

        /** True if the effect shows a gain reduction meter (dynamics). */
        bool showsGainReduction = false;

        /** Where the effect comes from, for the interface: empty for built-in
            effects, else "VST3", "CLAP" or "MXJ plugin". */
        juce::String source;

        /** Hosted VST3 and CLAP plugins: makes the shared state of new values.
            `instance` names one live plugin: values made with the same non-empty
            name share one plugin (the effect rack of a track and its mixer
            strip), an empty name makes a separate plugin (applying an effect,
            mixdown). `saved` is a state from EffectState::save() to load. */
        std::function<std::shared_ptr<EffectState> (const juce::String& instance, const juce::String& saved)> createState;

        bool isRealtime() const noexcept { return kind == Kind::realtime; }
        bool isHostedPlugin() const noexcept { return createState != nullptr; }

        /** Index of a parameter by id, or -1. */
        int indexOf (const juce::String& parameterId) const;

        /** Default values of all parameters. */
        std::vector<float> getDefaults() const;

        /** New shared values set to the defaults (with a new, separate plugin
            state for hosted plugins). */
        ParameterValuesPtr createValues() const;

        /** New shared values set to `values`; for hosted plugins with the plugin
            `instance` (see createState) loaded with `savedState`. */
        ParameterValuesPtr createValues (const std::vector<float>& values, const juce::String& instance,
                                         const juce::String& savedState) const;
    };
}
