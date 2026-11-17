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

        if (callbackAdded)
        {
            deviceManager.removeAudioCallback (&playback);
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
}
