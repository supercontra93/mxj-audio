// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "engine/playback/PlaybackEngine.h"
#include "engine/recording/Recorder.h"

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

        /** Opens the device described by `savedState` (may be empty). Only the
            outputs are opened: inputs are switched on just while recording, so
            the microphone is not in use (and Windows shows no microphone icon)
            the rest of the time. Returns an error message, or an empty string
            when a device is running. */
        juce::String initialise (const juce::String& savedState);

        /** The current device choice, for the settings file. */
        juce::String getState() const;

        /** Closes the device. Called before the program quits. */
        void shutdown();

        juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
        PlaybackEngine& getPlayback() noexcept               { return playback; }

        /** Plays the multitrack session (phase 5); separate from the file's playback. */
        PlaybackEngine& getSessionPlayback() noexcept        { return sessionPlayback; }
        Recorder& getRecorder() noexcept                     { return recorder; }

        //==============================================================================
        /** Where recording takes its audio from. */
        struct InputChoice
        {
            juce::String deviceName; ///< Windows Audio: input device; empty = the system default. ASIO: ignored.
            int firstChannel = 0;    ///< first input channel of the device
            int numChannels = 1;     ///< 1 = mono, 2 = stereo
        };

        /** Input devices of the current driver type (for ASIO: the open device). */
        juce::StringArray getInputDeviceNames();

        /** Input channel names of the device that recording would use. */
        juce::StringArray getInputChannelNames (const juce::String& deviceName);

        /** Switches the inputs on and starts a take in `store` (both playbacks stop). Returns an error
            message, or an empty string when recording runs. */
        juce::String startRecording (BlockStore& store, const InputChoice& input);

        /** Stops the take, switches the inputs off again and returns the take. */
        AudioSequence stopRecording();

        bool isRecording() const noexcept { return recorder.isRecording(); }

        bool hasRunningDevice();

        /** e.g. "Windows Audio - Speakers (Realtek) - 48000 Hz - 480 samples (10.0 ms)". */
        juce::String describeDevice() const;

    private:
        juce::AudioDeviceManager deviceManager;
        void setInputsEnabled (const InputChoice* input, juce::String& error);

        PlaybackEngine playback, sessionPlayback;
        Recorder recorder;
        bool callbackAdded = false;

        JUCE_DECLARE_NON_COPYABLE (AudioEngine)
    };
}
