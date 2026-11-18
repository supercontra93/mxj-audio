// SPDX-License-Identifier: AGPL-3.0-only
#include "plugins/host/HostedPlugin.h"

namespace mxj::plugins
{
    namespace
    {
        bool onMessageThread()
        {
            auto* mm = juce::MessageManager::getInstanceWithoutCreating();
            return mm == nullptr || mm->isThisTheMessageThread();
        }

        /** Runs `f` on the message thread and waits for it. */
        template <typename F>
        void runOnMessageThread (F&& f)
        {
            if (onMessageThread())
                f();
            else
                juce::MessageManager::callSync (std::forward<F> (f));
        }

        /** Asks for stereo in and out, else mono, else keeps what the plugin offers. */
        void chooseLayout (juce::AudioPluginInstance& p)
        {
            for (const auto& set : { juce::AudioChannelSet::stereo(), juce::AudioChannelSet::mono() })
            {
                juce::AudioProcessor::BusesLayout layout;
                if (p.getBusCount (true) > 0)
                    layout.inputBuses.add (set);
                if (p.getBusCount (false) > 0)
                    layout.outputBuses.add (set);

                for (int i = 1; i < p.getBusCount (true); ++i)
                    layout.inputBuses.add (juce::AudioChannelSet::disabled());
                for (int i = 1; i < p.getBusCount (false); ++i)
                    layout.outputBuses.add (juce::AudioChannelSet::disabled());

                if (p.setBusesLayout (layout))
                    return;
            }
        }
    }

    //==============================================================================
    HostedPlugin::HostedPlugin (juce::PluginDescription d, InstanceFactory f, juce::String savedState)
        : description (std::move (d)), factory (std::move (f)), lastState (std::move (savedState))
    {
    }

    HostedPlugin::~HostedPlugin()
    {
        if (instance == nullptr)
            return;

        instance->removeListener (this);

        // Plugins expect to be deleted on the message thread.
        if (! onMessageThread())
            juce::MessageManager::callAsync ([p = std::shared_ptr<juce::AudioPluginInstance> (std::move (instance))]() mutable { p.reset(); });
    }

    juce::String HostedPlugin::save()
    {
        if (! edited.load() && (lastState.isNotEmpty() || instance == nullptr))
            return lastState;

        runOnMessageThread ([this]
        {
            if (instance == nullptr)
                return;

            edited = false;
            juce::MemoryBlock data;
            instance->getStateInformation (data);
            lastState = data.getSize() > 0 ? data.toBase64Encoding() : juce::String();
        });

        return lastState;
    }

    void HostedPlugin::restore (const juce::String& saved)
    {
        if (saved.isEmpty() || saved == lastState)
            return;

        runOnMessageThread ([this, saved]
        {
            lastState = saved;
            edited = false;

            if (instance == nullptr)
                return; // loaded with the plugin

            juce::MemoryBlock data;
            if (data.fromBase64Encoding (saved) && data.getSize() > 0)
                instance->setStateInformation (data.getData(), (int) data.getSize());

            edited = false;
        });
    }

    juce::AudioPluginInstance* HostedPlugin::getInstance()
    {
        jassert (onMessageThread());
        if (! triedToLoad)
            load();

        return instance.get();
    }

    juce::String HostedPlugin::getError() const
    {
        return error;
    }

    void HostedPlugin::load()
    {
        triedToLoad = true;
        instance = factory != nullptr ? factory (description, 48000.0, 512, error) : nullptr;

        if (instance == nullptr)
        {
            if (error.isEmpty())
                error = "The plugin could not be loaded: " + description.name;
            return;
        }

        chooseLayout (*instance);

        if (lastState.isNotEmpty())
        {
            juce::MemoryBlock data;
            if (data.fromBase64Encoding (lastState) && data.getSize() > 0)
                instance->setStateInformation (data.getData(), (int) data.getSize());
        }

        edited = false;
        instance->addListener (this);
    }

    void HostedPlugin::prepare (double sampleRate, int maximumBlockSize)
    {
        if (ready.load() && juce::approximatelyEqual (sampleRate, preparedRate)
            && juce::jmin (maximumBlockSize, maxPluginBlock) <= preparedBlock)
            return;

        runOnMessageThread ([this, sampleRate, maximumBlockSize] { prepareOnMessageThread (sampleRate, maximumBlockSize); });
    }

    void HostedPlugin::prepareOnMessageThread (double sampleRate, int maximumBlockSize)
    {
        if (! triedToLoad)
            load();

        if (instance == nullptr)
            return;

        if (ready.load() && juce::approximatelyEqual (sampleRate, preparedRate)
            && juce::jmin (maximumBlockSize, maxPluginBlock) <= preparedBlock)
            return;

        lock();
        ready = false;

        // Large blocks (applying an effect uses big ones) are cut into pieces: some plugins cannot take more.
        const auto block = juce::jlimit (64, maxPluginBlock, juce::jmax (maximumBlockSize, preparedBlock));
        instance->releaseResources();
        instance->setRateAndBufferSizeDetails (sampleRate, block);
        instance->prepareToPlay (sampleRate, block);
        instance->setNonRealtime (false);

        pluginIns = instance->getTotalNumInputChannels();
        pluginOuts = instance->getTotalNumOutputChannels();
        work.setSize (juce::jmax (2, pluginIns, pluginOuts), block, false, true, false);
        midi.ensureSize (1024);
        latency = instance->getLatencySamples();
        preparedRate = sampleRate;
        preparedBlock = block;

        ready = true;
        unlock();
    }

    void HostedPlugin::lock() noexcept
    {
        while (busy.exchange (true, std::memory_order_acquire))
            juce::Thread::yield();
    }

    void HostedPlugin::process (float* const* channels, int numChannels, int numFrames) noexcept
    {
        if (numChannels <= 0 || numFrames <= 0 || ! ready.load (std::memory_order_acquire))
            return;

        if (busy.exchange (true, std::memory_order_acquire))
            return; // being prepared, or played by another processor: leave the audio unchanged

        if (instance == nullptr)
        {
            unlock();
            return;
        }

        for (int start = 0; start < numFrames; start += preparedBlock)
        {
            const auto n = juce::jmin (preparedBlock, numFrames - start);
            float* part[16];
            const auto chans = juce::jmin (numChannels, 16);
            for (int c = 0; c < chans; ++c)
                part[c] = channels[c] + start;

            processPart (part, chans, n);
        }

        unlock();
    }

    void HostedPlugin::processPart (float* const* channels, int numChannels, int numFrames) noexcept
    {
        // In: our channels into the plugin's (mono audio goes to both sides,
        // stereo is mixed for a mono plugin).
        const auto width = work.getNumChannels();
        for (int c = 0; c < width; ++c)
        {
            auto* dest = work.getWritePointer (c);
            if (c < pluginIns)
            {
                if (pluginIns == 1 && numChannels >= 2)
                {
                    juce::FloatVectorOperations::copyWithMultiply (dest, channels[0], 0.5f, numFrames);
                    juce::FloatVectorOperations::addWithMultiply (dest, channels[1], 0.5f, numFrames);
                }
                else
                {
                    juce::FloatVectorOperations::copy (dest, channels[juce::jmin (c, numChannels - 1)], numFrames);
                }
            }
            else
            {
                juce::FloatVectorOperations::clear (dest, numFrames);
            }
        }

        juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), width, numFrames); // no allocation (few channels)
        midi.clear();
        instance->processBlock (view, midi);

        // Out: the plugin's channels back into ours.
        if (pluginOuts > 0)
            for (int c = 0; c < numChannels; ++c)
                juce::FloatVectorOperations::copy (channels[c], work.getReadPointer (juce::jmin (c, pluginOuts - 1)), numFrames);
    }

    void HostedPlugin::reset() noexcept
    {
        if (! ready.load() || busy.exchange (true, std::memory_order_acquire))
            return;

        if (instance != nullptr)
            instance->reset();

        unlock();
    }

    //==============================================================================
    HostedPluginProcessor::HostedPluginProcessor (dsp::ParameterValuesPtr values, std::shared_ptr<HostedPlugin> p)
        : EffectProcessor (std::move (values)), plugin (std::move (p))
    {
    }

    void HostedPluginProcessor::prepare (double sampleRate, int maximumBlockSize, int)
    {
        if (plugin != nullptr)
            plugin->prepare (sampleRate, maximumBlockSize);
    }

    void HostedPluginProcessor::reset()
    {
        if (plugin != nullptr)
            plugin->reset();
    }

    void HostedPluginProcessor::process (float* const* channels, int numChannels, int numFrames)
    {
        if (plugin != nullptr)
            plugin->process (channels, numChannels, numFrames);
    }

    int HostedPluginProcessor::getLatencyFrames() const
    {
        return plugin != nullptr ? plugin->getLatencyFrames() : 0;
    }

    juce::String HostedPluginProcessor::getError() const
    {
        return plugin != nullptr ? plugin->getError() : juce::String ("No plugin");
    }
}
