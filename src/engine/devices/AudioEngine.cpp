// SPDX-License-Identifier: AGPL-3.0-only
#include "engine/devices/AudioEngine.h"

namespace mxj
{
    AudioEngine::AudioEngine() = default;

    AudioEngine::~AudioEngine()
    {
        shutdown();
    }

    juce::String AudioEngine::initialise (const juce::String& savedState)
    {
        std::unique_ptr<juce::XmlElement> xml;
        if (savedState.isNotEmpty())
            xml = juce::parseXML (savedState);

        auto error = deviceManager.initialise (0, 2, xml.get(), true);

        if (! callbackAdded)
        {
            deviceManager.addAudioCallback (&playback);
            deviceManager.addAudioCallback (&recorder);
            callbackAdded = true;
        }

        if (error.isEmpty() && deviceManager.getCurrentAudioDevice() == nullptr)
            error = TRANS ("No audio output device was found.");

        return error;
    }

    juce::String AudioEngine::getState() const
    {
        if (const auto xml = deviceManager.createStateXml())
            return xml->toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());

        return {};
    }

    void AudioEngine::shutdown()
    {
        playback.stop();
        recorder.stop();

        if (callbackAdded)
        {
            deviceManager.removeAudioCallback (&playback);
            deviceManager.removeAudioCallback (&recorder);
            callbackAdded = false;
        }

        deviceManager.closeAudioDevice();
    }

    bool AudioEngine::hasRunningDevice()
    {
        auto* device = deviceManager.getCurrentAudioDevice();
        return device != nullptr && device->isPlaying();
    }

    juce::String AudioEngine::describeDevice() const
    {
        auto* device = deviceManager.getCurrentAudioDevice();
        if (device == nullptr)
            return TRANS ("No audio device");

        const auto rate = device->getCurrentSampleRate();
        const auto buffer = device->getCurrentBufferSizeSamples();

        return device->getTypeName() + " - " + device->getName()
             + " - " + juce::String (juce::roundToInt (rate)) + " Hz"
             + " - " + juce::String (buffer) + " " + TRANS ("samples")
             + " (" + juce::String (rate > 0 ? 1000.0 * buffer / rate : 0.0, 1) + " ms)";
    }

    //==============================================================================
    juce::StringArray AudioEngine::getInputDeviceNames()
    {
        auto* type = deviceManager.getCurrentDeviceTypeObject();
        if (type == nullptr)
            return {};

        if (! type->hasSeparateInputsAndOutputs())
        {
            auto* device = deviceManager.getCurrentAudioDevice();
            return device != nullptr ? juce::StringArray { device->getName() } : juce::StringArray();
        }

        type->scanForDevices();
        return type->getDeviceNames (true);
    }

    juce::StringArray AudioEngine::getInputChannelNames (const juce::String& deviceName)
    {
        auto* type = deviceManager.getCurrentDeviceTypeObject();
        auto* current = deviceManager.getCurrentAudioDevice();
        if (type == nullptr)
            return {};

        // The open device knows its input channels (ASIO, or the same device for both).
        if (current != nullptr && (! type->hasSeparateInputsAndOutputs() || deviceName.isEmpty()
                                   || deviceName == deviceManager.getAudioDeviceSetup().inputDeviceName))
        {
            const auto names = current->getInputChannelNames();
            if (! names.isEmpty())
                return names;
        }

        // Windows Audio inputs: most are mono or stereo; offer two channels.
        return { TRANS ("Input") + " 1", TRANS ("Input") + " 2" };
    }

    void AudioEngine::setInputsEnabled (const InputChoice* input, juce::String& error)
    {
        auto setup = deviceManager.getAudioDeviceSetup();
        setup.useDefaultInputChannels = false;
        setup.inputChannels.clear();

        if (input != nullptr)
        {
            auto* type = deviceManager.getCurrentDeviceTypeObject();
            auto name = input->deviceName;

            if (type != nullptr && type->hasSeparateInputsAndOutputs())
            {
                type->scanForDevices();
                const auto names = type->getDeviceNames (true);
                if (name.isEmpty() || ! names.contains (name))
                    name = names[juce::jmax (0, type->getDefaultDeviceIndex (true))];
            }
            else
            {
                name = setup.outputDeviceName;
            }

            setup.inputDeviceName = name;
            setup.inputChannels.setRange (juce::jmax (0, input->firstChannel), juce::jlimit (1, 2, input->numChannels), true);
        }
        else
        {
            setup.inputDeviceName = {};
        }

        error = deviceManager.setAudioDeviceSetup (setup, true);
    }

    juce::String AudioEngine::startRecording (BlockStore& store, const InputChoice& input)
    {
        if (recorder.isRecording())
            return {};

        playback.stop();

        juce::String error;
        setInputsEnabled (&input, error);

        auto* device = deviceManager.getCurrentAudioDevice();
        if (error.isEmpty() && (device == nullptr || device->getActiveInputChannels().countNumberOfSetBits() == 0))
            error = TRANS ("No input could be opened. Check the recording device in Edit > Audio Device Settings.");

        if (error.isEmpty() && ! recorder.start (store, juce::jlimit (1, 2, input.numChannels), device->getCurrentSampleRate()))
            error = TRANS ("Could not write temporary audio data. Is the disk full?");

        if (error.isNotEmpty())
        {
            juce::String ignored;
            setInputsEnabled (nullptr, ignored);
        }

        return error;
    }

    AudioSequence AudioEngine::stopRecording()
    {
        if (! recorder.isRecording())
            return {};

        auto take = recorder.stop();
        juce::String ignored;
        setInputsEnabled (nullptr, ignored);
        return take;
    }
}
