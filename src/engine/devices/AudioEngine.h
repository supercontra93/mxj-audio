// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "engine/playback/PlaybackEngine.h"

#include <juce_audio_devices/juce_audio_devices.h>

namespace mxj
{
    /** Owns the audio device and the playback engine.

        Windows: WASAPI (shared, exclusive and low latency modes) and ASIO.
        The chosen device is stored as an XML string in the settings
        (keys::audioDeviceState) and restored at start-up. If the saved device is
        missing, the system default is used. Message thread only.
    */
    class AudioEngine
    {
    public:
        AudioEngine();
        ~AudioEngine();

        /** Opens the device described by `savedState` (may be empty). Phase 1
            opens outputs only; recording arrives in phase 2. Returns an error
            message, or an empty string when a device is running. */
        juce::String initialise (const juce::String& savedState);

        /** The current device choice, for the settings file. */
        juce::String getState() const;

        /** Closes the device. Called before the program quits. */
        void shutdown();

        juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
        PlaybackEngine& getPlayback() noexcept               { return playback; }

        bool hasRunningDevice();

        /** e.g. "Windows Audio - Speakers (Realtek) - 48000 Hz - 480 samples (10.0 ms)". */
        juce::String describeDevice() const;

    private:
        juce::AudioDeviceManager deviceManager;
        PlaybackEngine playback;
        bool callbackAdded = false;

        JUCE_DECLARE_NON_COPYABLE (AudioEngine)
    };
}
