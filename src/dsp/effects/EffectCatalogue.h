// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "dsp/effects/Effect.h"

#include <atomic>
#include <memory>
#include <vector>

namespace mxj::dsp
{
    /** The list of built-in effect types. Thread safe (built once, then read only). */
    namespace catalogue
    {
        /** All effect types, in menu order. */
        const std::vector<EffectType>& all();

        /** The type with this id, or nullptr. */
        const EffectType* find (const juce::String& id);

        /** Menu groups, in order. English. */
        juce::StringArray categories();
    }

    //==============================================================================
    /** One effect with its parameter values: what a preset, an effect rack slot
        or a "apply this effect" request stores. A value; serialisable. */
    struct EffectSettings
    {
        juce::String typeId;
        std::vector<float> values; ///< in parameter order
        bool enabled = true;

        /** Default values of the type (an empty typeId if the type is unknown). */
        static EffectSettings defaultsFor (const juce::String& typeId);

        /** {"type": id, "enabled": bool, "values": {parameterId: value, ...}}.
            Values are stored by parameter id, so adding parameters later keeps
            old presets working. */
        juce::var toVar() const;

        /** Reads toVar() output. Unknown parameters are ignored, missing ones get
            their default. Returns settings with an empty typeId if the type is unknown. */
        static EffectSettings fromVar (const juce::var&);
    };

    using ChainSettings = std::vector<EffectSettings>;

    juce::var chainToVar (const ChainSettings&);
    ChainSettings chainFromVar (const juce::var&);

    //==============================================================================
    /** Processors run one after the other: the effect rack, or a single effect.

        Built and prepared on the message thread (or a worker), then used by one
        thread at a time. process() follows the real-time rules, so the chain can
        run on the audio thread for live preview.
    */
    class ProcessorChain
    {
    public:
        ProcessorChain() = default;

        /** Adds a processor. `enabled` may be shared with the interface to bypass
            the effect while it plays (nullptr = always on). */
        void add (std::unique_ptr<EffectProcessor> processor, std::shared_ptr<std::atomic<bool>> enabled = nullptr);

        /** A chain for applying effects: one processor per enabled entry, each with
            its own copy of the values. Unknown and realtime-incapable types are skipped. */
        static std::unique_ptr<ProcessorChain> create (const ChainSettings&);

        void prepare (double sampleRate, int maximumBlockSize, int numChannels);
        void reset();

        /** Processes in place; blocks longer than the prepared size are split. Real-time safe. */
        void process (float* const* channels, int numChannels, int numFrames);

        /** Total look-ahead of the enabled processors. */
        int getLatencyFrames() const;

        bool isEmpty() const noexcept { return entries.empty(); }
        int size() const noexcept { return (int) entries.size(); }
        double getSampleRate() const noexcept { return preparedRate; }

    private:
        struct Entry
        {
            std::unique_ptr<EffectProcessor> processor;
            std::shared_ptr<std::atomic<bool>> enabled;
        };

        std::vector<Entry> entries;
        double preparedRate = 0.0;
        int blockSize = 0;
    };
}
