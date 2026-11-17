// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/audio/AudioSequence.h"
#include "core/audio/BlockStore.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <mutex>

namespace mxj
{
    /** Records the audio input into the block store.

        Three threads take part, as in playback:
        - the message thread calls start() and stop();
        - the audio thread copies the input into a lock-free FIFO and measures
          the level (no allocation, no lock, no file access: CLAUDE.md rule 4);
        - a writer thread (owned here) moves the FIFO into block files and builds
          their peaks, so the take can be drawn while it grows.

        The take is written in segments of ten minutes, each a block file with
        its own peaks, so a recording can last for hours without reserving
        memory up front. Segment files stay in the run folder until the take is
        used or dropped, so the crash recovery journal can point at them.
    */
    class Recorder : public juce::AudioIODeviceCallback
    {
    public:
        Recorder();
        ~Recorder() override;

        /** Starts a take with `numChannels` channels (1 or 2) at `sampleRate`
            (the device rate). The audio callback must already receive input.
            Returns false if a take is running or the store cannot be written. */
        bool start (BlockStore& store, int numChannels, double sampleRate);

        /** Stops the take and returns it (empty if nothing was recorded). */
        AudioSequence stop();

        bool isRecording() const noexcept                { return writerThread != nullptr; }
        int getNumChannels() const noexcept              { return channels; }
        double getSampleRate() const noexcept            { return rate; }

        /** Frames written to disk so far. Any thread. */
        juce::int64 getFramesRecorded() const noexcept   { return framesWritten.load(); }

        /** Highest absolute sample of a channel since the last call (for the level
            meter), and resets it. Any thread. */
        float takePeakLevel (int channel) noexcept;

        /** Input frames lost because the disk could not keep up. */
        juce::int64 getDroppedFrames() const noexcept    { return dropped.load(); }

        /** True once after the device stopped during a take (unplugged, changed). */
        bool takeDeviceStoppedFlag() noexcept            { return deviceStopped.exchange (false); }

        /** Peaks of the take while it records (message thread). */
        bool getMinMax (int channel, juce::int64 startFrame, juce::int64 endFrame, float& minOut, float& maxOut) const;

        /** The block files of the take so far (for the recovery journal). */
        juce::Array<juce::File> getSegmentFiles() const;

        /** Ten minutes. */
        static constexpr double segmentSeconds = 600.0;

        /** About 5 s at 48 kHz: the disk may stall that long without losing audio. */
        static constexpr int fifoFrames = 1 << 18;

        // AudioIODeviceCallback
        void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                               float* const* outputChannelData, int numOutputChannels,
                                               int numSamples, const juce::AudioIODeviceCallbackContext&) override;
        void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
        void audioDeviceStopped() override;

    private:
        class WriterThread;

        struct Segment
        {
            std::shared_ptr<PeakCache> peaks;
            std::shared_ptr<BlockFileSource> source;   ///< set when finished
            std::unique_ptr<BlockStore::Writer> writer; ///< set while recording
            juce::int64 start = 0;                      ///< first frame in the take
            juce::int64 frames = 0;
        };

        /** Writer thread: moves everything in the FIFO to disk. */
        void drain();
        bool finishSegment (Segment&);
        Segment* startSegment();

        // Shared with the audio thread (lock free).
        std::atomic<bool> capturing { false };
        std::atomic<bool> inCallback { false };
        std::atomic<bool> deviceStopped { false };
        std::atomic<juce::int64> dropped { 0 };
        std::atomic<float> peakLevels[2] { 0.0f, 0.0f };
        juce::AbstractFifo fifo { fifoFrames };
        juce::AudioBuffer<float> fifoBuffer { 2, fifoFrames };

        // Message thread and writer thread.
        BlockStore* store = nullptr;
        int channels = 0;
        double rate = 0.0;
        juce::int64 segmentFrames = 0;
        std::atomic<juce::int64> framesWritten { 0 };
        mutable std::mutex segmentLock;
        std::vector<Segment> segments;
        bool writeFailed = false;
        juce::AudioBuffer<float> drainBuffer;
        std::unique_ptr<WriterThread> writerThread;

        JUCE_DECLARE_NON_COPYABLE (Recorder)
    };
}
