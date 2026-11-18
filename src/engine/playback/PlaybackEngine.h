// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/audio/AudioSequence.h"
#include "engine/playback/PlaybackSource.h"
#include "dsp/analysis/LevelMeter.h"
#include "dsp/effects/EffectCatalogue.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <mutex>

namespace mxj
{
    /** Plays one AudioSequence (or another PlaybackSource) through the audio device.

        Three threads take part:
        - the message thread calls play(), stop() and setSequence();
        - a disk thread (owned here) reads the sequence ahead, converts the sample
          rate when the file and the device differ, and writes into a lock-free FIFO;
        - the audio thread only copies from the FIFO to the device.

        The audio callback allocates no memory, takes no lock and touches no file
        (CLAUDE.md, rule 4). Mono files play on both speakers; files with more than
        two channels play their first two channels in phase 1.

        Phase 3: what plays runs through an effect chain (live preview and the
        effect rack) and then through the level meter. A new chain is handed to
        the audio thread through an atomic pointer; the old one comes back the
        same way and is deleted on the message thread (never on the audio thread).
    */
    class PlaybackEngine : public juce::AudioIODeviceCallback
    {
    public:
        PlaybackEngine();
        ~PlaybackEngine() override;

        /** Stops playback and sets what to play. Message thread. */
        void setSequence (AudioSequence sequence);

        /** Stops playback and sets what to play (phase 5: a session mix). Message thread. */
        void setSource (std::shared_ptr<PlaybackSource> source);

        /** How far (device frames) the disk thread reads ahead. Less means
            mixer controls are heard sooner but the disk has less time; the
            default is the whole FIFO. Any thread. */
        void setLookahead (int frames) noexcept { lookahead = juce::jlimit (4096, fifoFrames, frames); }

        /** Starts playing from `startFrame` (in frames of the sequence) up to
            `endFrame` (-1 = the end). Returns false if there is no sequence or no
            running audio device. Message thread. */
        bool play (juce::int64 startFrame, juce::int64 endFrame = -1);

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
        /** Sets the effects heard while playing (nullptr = none). The chain is
            prepared here for the device (stereo, device sample rate). Message thread. */
        void setEffectChain (std::unique_ptr<dsp::ProcessorChain> chain);

        /** Sample rate the current effect chain was prepared for (0 = none). Message thread. */
        double getEffectChainSampleRate() const noexcept { return chainRate; }

        /** Deletes a chain the audio thread has let go of, and hands over a
            waiting one. Call regularly from a timer. Message thread. */
        void collectRetiredChain();

        /** A counter of another device callback (the recorder's captured frames)
            that is read in the callback where playback starts, so a take can be
            lined up with what played. The recorder's callback must come after
            this one (AudioEngine adds them in that order). Message thread. */
        void setSyncCounter (const std::atomic<juce::int64>* counter) noexcept { syncCounter = counter; }

        /** The sync counter's value when playback started, or -1. Any thread. */
        juce::int64 getSyncValueAtStart() const noexcept { return syncAtStart.load(); }

        /** Sample rate of the running device, or 0. */
        double getDeviceSampleRate() const noexcept { return deviceRunning.load() ? deviceRate.load() : 0.0; }

        /** Levels of what is playing (after the effects). */
        dsp::LevelMeter& getMeter() noexcept { return meter; }

        /** Copies up to `maxSamples` of the most recent output (mono, left + right)
            for the spectrum analyser, oldest first. Returns how many were copied.
            Message thread (one reader). */
        int readRecentOutput (float* dest, int maxSamples);

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

        /** Audio thread: takes a new chain if one waits and the retired slot is free. */
        void adoptPendingChain() noexcept;

        /** Audio thread: plays `numFrames` frames from the FIFO into the work buffer,
            then effects, meter and spectrum tap. Returns frames taken from the FIFO. */
        int renderBlock (int numFrames) noexcept;

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
        std::atomic<const std::atomic<juce::int64>*> syncCounter { nullptr };
        std::atomic<juce::int64> syncAtStart { -1 };
        double tonePhase = 0.0;

        juce::AbstractFifo fifo { fifoFrames };
        std::atomic<int> lookahead { fifoFrames };
        juce::AudioBuffer<float> fifoBuffer { 2, fifoFrames };
        std::atomic<bool> endOfSequenceWritten { false };

        std::unique_ptr<DiskThread> diskThread;

        // Effects: handed over through atomics (see setEffectChain).
        std::atomic<dsp::ProcessorChain*> pendingChain { nullptr }, retiredChain { nullptr };
        dsp::ProcessorChain* activeChain = nullptr;      ///< audio thread only (and while halted)
        std::unique_ptr<dsp::ProcessorChain> waitingChain; ///< message thread: not handed over yet
        std::atomic<bool> resetChainRequested { false };
        double chainRate = 0.0;
        int deviceBlockSize = 512;

        static constexpr int workFrames = 4096;
        juce::AudioBuffer<float> work { 2, workFrames };
        dsp::LevelMeter meter;

        // Spectrum tap: audio thread writes, message thread reads.
        static constexpr int tapFrames = 1 << 15;
        juce::AbstractFifo tapFifo { tapFrames };
        std::vector<float> tapBuffer = std::vector<float> ((size_t) tapFrames);

        JUCE_DECLARE_NON_COPYABLE (PlaybackEngine)
    };
}
