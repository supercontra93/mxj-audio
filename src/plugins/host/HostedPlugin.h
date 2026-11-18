// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "dsp/effects/Effect.h"

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <atomic>
#include <functional>
#include <memory>

namespace mxj::plugins
{
    /** Makes plugin instances. Implemented by PluginHost. */
    using InstanceFactory = std::function<std::unique_ptr<juce::AudioPluginInstance> (const juce::PluginDescription&,
                                                                                     double sampleRate, int blockSize,
                                                                                     juce::String& error)>;

    /** One VST3 or CLAP plugin used as an effect: the EffectState of its
        ParameterValues. Every processor made from those values plays this
        same plugin, and the plugin window shows it.

        The plugin is loaded on first use (prepare or getInstance), always on the
        message thread. Loading, preparing, saving and restoring wait for the
        message thread when called from a worker.

        process() is real-time safe: it never waits. If another thread is
        preparing the plugin, or another processor is already running it, the
        audio passes unchanged for that block.
    */
    class HostedPlugin final : public dsp::EffectState,
                               private juce::AudioProcessorListener
    {
    public:
        HostedPlugin (juce::PluginDescription description, InstanceFactory factory, juce::String savedState);
        ~HostedPlugin() override;

        // EffectState
        juce::String save() override;
        void restore (const juce::String& saved) override;

        const juce::PluginDescription& getDescription() const noexcept { return description; }

        /** The plugin, loaded if needed. nullptr if it cannot be loaded (see getError()).
            Message thread only. */
        juce::AudioPluginInstance* getInstance();

        /** Why the plugin could not be loaded, or empty. */
        juce::String getError() const;

        /** Loads and prepares the plugin. Does nothing if it is already prepared
            for this rate and at least this block size. Not the audio thread. */
        void prepare (double sampleRate, int maximumBlockSize);

        /** Processes in place. Real-time safe. `numFrames` at most the prepared block size. */
        void process (float* const* channels, int numChannels, int numFrames) noexcept;

        /** Clears tails. Real-time safe (skipped if the plugin is busy). */
        void reset() noexcept;

        int getLatencyFrames() const noexcept { return latency.load(); }

        /** The plugin changed (a control moved in its window). Cleared by save(). */
        bool isEdited() const noexcept { return edited.load(); }
        void markEdited() noexcept     { edited = true; }

    private:
        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override { edited = true; }
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& d) override
        {
            if (d.parameterInfoChanged || d.nonParameterStateChanged || d.programChanged)
                edited = true;

            if (d.latencyChanged && instance != nullptr)
                latency = instance->getLatencySamples();
        }

        void load();
        void processPart (float* const* channels, int numChannels, int numFrames) noexcept;
        static constexpr int maxPluginBlock = 4096;
        void prepareOnMessageThread (double sampleRate, int maximumBlockSize);
        void lock() noexcept;
        void unlock() noexcept { busy.store (false, std::memory_order_release); }

        const juce::PluginDescription description;
        const InstanceFactory factory;

        std::unique_ptr<juce::AudioPluginInstance> instance;
        bool triedToLoad = false;
        juce::String error, lastState;
        double preparedRate = 0.0;
        int preparedBlock = 0;

        std::atomic<bool> busy { false }, ready { false }, edited { false };
        std::atomic<int> latency { 0 };
        juce::AudioBuffer<float> work;
        juce::MidiBuffer midi;
        int pluginIns = 0, pluginOuts = 0;

        JUCE_DECLARE_NON_COPYABLE (HostedPlugin)
    };

    /** The effect processor of a hosted plugin: forwards to the shared HostedPlugin. */
    class HostedPluginProcessor final : public dsp::EffectProcessor
    {
    public:
        HostedPluginProcessor (dsp::ParameterValuesPtr values, std::shared_ptr<HostedPlugin> plugin);

        void prepare (double sampleRate, int maximumBlockSize, int numChannels) override;
        void reset() override;
        void process (float* const* channels, int numChannels, int numFrames) override;
        int getLatencyFrames() const override;
        juce::String getError() const override;

    private:
        std::shared_ptr<HostedPlugin> plugin;
    };
}
