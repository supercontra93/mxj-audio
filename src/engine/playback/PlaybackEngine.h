// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/audio/AudioSequence.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <mutex>

namespace mxj
{
    /** Plays one AudioSequence through the audio device.

        Three threads take part:
        - the message thread calls play(), stop() and setSequence();
        - a disk thread (owned here) reads the sequence ahead, converts the sample
          rate when the file and the device differ, and writes into a lock-free FIFO;
        - the audio thread only copies from the FIFO to the device.

        The audio callback allocates no memory, takes no lock and touches no file
        (CLAUDE.md, rule 4). Mono files play on both speakers; files with more than
        two channels play their first two channels in phase 1.
    */
    class PlaybackEngine : public juce::AudioIODeviceCallback
    {
    public:
        PlaybackEngine();
        ~PlaybackEngine() override;

        /** Stops playback and sets what to play. Message thread. */
        void setSequence (AudioSequence sequence);

        /** Starts playing from `startFrame` (in frames of the sequence).
            Returns false if there is no sequence or no running audio device.
            Message thread. */
        bool play (juce::int64 startFrame);

        /** Stops playback. Message thread. */
        void stop();

        bool isPlaying() const noexcept { return state.load() == State::playing; }

        /** True once after playback reached the end of the sequence by itself.
            The UI polls this from a timer. Message thread. */
        bool takeFinishedFlag();

        /** The frame of the sequence that is being heard now. Any thread. */
        juce::int64 getPosition() const noexcept;

        /** Device frames sent to the output since play(). Any thread. */
        juce::int64 getDeviceFramesPlayed() const noexcept { return framesPlayed.load(); }

        /** The frame playback was started from. */
        juce::int64 getStartPosition() const noexcept { return startPosition.load(); }

        /** Times the disk thread could not keep up since play() (for the log). */
        int getUnderrunCount() const noexcept { return underruns.load(); }

        /** Plays a quiet 440 Hz tone instead of the sequence, to test the device. */
        void setTestToneEnabled (bool shouldPlay) noexcept { testTone = shouldPlay; }
        bool isTestToneEnabled() const noexcept { return testTone.load(); }

        //==============================================================================
        /** Sets the device format without a device. Called by audioDeviceAboutToStart;
            tests call it directly and then drive audioDeviceIOCallbackWithContext. */
        void prepare (double deviceSampleRate, int maximumBlockSize);

        /** Waits until the disk thread has filled the FIFO for the current play()
            (tests and the smoke test). Returns false on timeout. */
        bool waitUntilBuffered (int timeoutMs) const;

        // AudioIODeviceCallback
        void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                               float* const* outputChannelData, int numOutputChannels,
                                               int numSamples, const juce::AudioIODeviceCallbackContext&) override;
        void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
        void audioDeviceStopped() override;

        /** Frames the FIFO holds (about 1.4 s at 48 kHz). */
        static constexpr int fifoFrames = 1 << 16;

    private:
        enum class State { stopped, playing, finished };

        class DiskThread;

        /** Stops the audio thread from touching the FIFO and waits until a
            callback that is running right now has returned. */
        void haltAudioThread();

        // Shared with the audio thread (lock free).
        std::atomic<State> state { State::stopped };
        std::atomic<bool> inCallback { false };
        std::atomic<bool> testTone { false };
        std::atomic<bool> finishedFlag { false };
        std::atomic<bool> deviceRunning { false };
        std::atomic<double> deviceRate { 0.0 };
        std::atomic<juce::int64> framesPlayed { 0 };   ///< device frames played since play()
        std::atomic<juce::int64> startPosition { 0 };
        std::atomic<double> speedRatio { 1.0 };        ///< sequence frames per device frame
        std::atomic<int> underruns { 0 };
        double tonePhase = 0.0;

        juce::AbstractFifo fifo { fifoFrames };
        juce::AudioBuffer<float> fifoBuffer { 2, fifoFrames };
        std::atomic<bool> endOfSequenceWritten { false };

        std::unique_ptr<DiskThread> diskThread;

        JUCE_DECLARE_NON_COPYABLE (PlaybackEngine)
    };
}
