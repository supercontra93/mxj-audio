// SPDX-License-Identifier: AGPL-3.0-only
#include "engine/playback/PlaybackEngine.h"

#include <cmath>
#include <thread>

namespace mxj
{
    //==============================================================================
    /** Reads the sequence ahead of the audio thread. Allowed to allocate and to
        touch the disk; never shares a lock with the audio thread. */
    class PlaybackEngine::DiskThread final : public juce::Thread
    {
    public:
        explicit DiskThread (PlaybackEngine& o) : Thread ("MXJ playback disk"), owner (o)
        {
            startThread (Priority::high);
        }

        ~DiskThread() override
        {
            signalThreadShouldExit();
            wake.signal();
            stopThread (4000);
        }

        void setSequence (AudioSequence s)
        {
            const std::lock_guard<std::mutex> lock (requestLock);
            sequence = std::move (s);
        }

        bool hasAudio()
        {
            const std::lock_guard<std::mutex> lock (requestLock);
            return ! sequence.isEmpty();
        }

        double getSequenceRate()
        {
            const std::lock_guard<std::mutex> lock (requestLock);
            return sequence.getSampleRate();
        }

        juce::int64 getSequenceLength()
        {
            const std::lock_guard<std::mutex> lock (requestLock);
            return sequence.getNumFrames();
        }

        /** Returns the request number to wait for with waitForStart(). */
        int requestStart (juce::int64 position, double ratio, juce::int64 end)
        {
            int gen;
            {
                const std::lock_guard<std::mutex> lock (requestLock);
                request = { Request::start, position, ratio, end };
                gen = ++requestGeneration;
            }
            wake.signal();
            return gen;
        }

        void requestHalt()
        {
            {
                const std::lock_guard<std::mutex> lock (requestLock);
                request = { Request::halt, 0, 1.0, -1 };
                ++requestGeneration;
            }
            wake.signal();
        }

        /** Waits until the start request `gen` has reset and filled the FIFO. */
        bool waitForStart (int gen, int timeoutMs) const
        {
            const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

            while (startedGeneration.load() < gen)
            {
                const auto now = juce::Time::getMillisecondCounter();
                if (now >= deadline)
                    return false;

                started.wait ((int) (deadline - now));
            }

            return true;
        }

        int getLastRequestGeneration()
        {
            const std::lock_guard<std::mutex> lock (requestLock);
            return requestGeneration;
        }

        void run() override
        {
            while (! threadShouldExit())
            {
                Request r;
                int gen;
                {
                    const std::lock_guard<std::mutex> lock (requestLock);
                    r = request;
                    gen = requestGeneration;
                    request.type = Request::none;

                    if (r.type == Request::start)
                        playing = sequence; // a copy of the block list, not of the audio
                }

                if (r.type == Request::start)
                {
                    begin (r.position, r.ratio, r.end);
                    fill();
                    startedGeneration = gen;
                    started.signal();
                }
                else if (r.type == Request::halt)
                {
                    active = false;
                    playing = {};
                }
                else if (active)
                {
                    fill();
                }

                wake.wait (active ? 5 : -1);
            }
        }

    private:
        struct Request
        {
            enum Type { none, start, halt } type = none;
            juce::int64 position = 0;
            double ratio = 1.0;
            juce::int64 end = -1; ///< stop here; -1 = end of the sequence
        };

        void begin (juce::int64 position, double ratio, juce::int64 end)
        {
            endPos = end < 0 ? playing.getNumFrames() : juce::jlimit ((juce::int64) 0, playing.getNumFrames(), end);
            owner.fifo.reset(); // safe: the audio thread is halted until we signal `started`
            owner.endOfSequenceWritten = false;
            readPos = juce::jlimit ((juce::int64) 0, playing.getNumFrames(), position);
            speed = ratio;
            resample = ! juce::approximatelyEqual (speed, 1.0);
            ended = false;
            active = true;

            for (auto& i : interpolators)
                i.reset();

            const auto inputCapacity = (int) std::ceil (chunkFrames * juce::jmax (1.0, speed)) + 16;
            input.setSize (2, inputCapacity, false, false, true);
            output.setSize (2, chunkFrames, false, false, true);
        }

        void fill()
        {
            while (active && ! ended && owner.fifo.getFreeSpace() >= chunkFrames && ! threadShouldExit())
            {
                const auto remaining = endPos - readPos;

                if (remaining <= 0)
                {
                    finishSequence();
                    break;
                }

                int produced;

                if (! resample)
                {
                    produced = (int) juce::jmin ((juce::int64) chunkFrames, remaining);
                    playing.read (output.getArrayOfWritePointers(), 2, readPos, produced);
                    readPos += produced;
                }
                else
                {
                    produced = (int) juce::jmin ((juce::int64) chunkFrames, (juce::int64) std::ceil ((double) remaining / speed));
                    const auto inputFrames = (int) std::ceil (produced * speed) + 8;
                    playing.read (input.getArrayOfWritePointers(), 2, readPos, inputFrames);

                    int used = 0;
                    for (int c = 0; c < 2; ++c)
                        used = interpolators[c].process (speed, input.getReadPointer (c), output.getWritePointer (c),
                                                         produced, inputFrames, 0);
                    readPos += used;
                }

                if (playing.getNumChannels() == 1)
                    output.copyFrom (1, 0, output, 0, 0, produced);

                write (produced);
            }
        }

        void write (int numFrames)
        {
            int start1, size1, start2, size2;
            owner.fifo.prepareToWrite (numFrames, start1, size1, start2, size2);

            for (int c = 0; c < 2; ++c)
            {
                if (size1 > 0) owner.fifoBuffer.copyFrom (c, start1, output, c, 0, size1);
                if (size2 > 0) owner.fifoBuffer.copyFrom (c, start2, output, c, size1, size2);
            }

            owner.fifo.finishedWrite (size1 + size2);
        }

        void finishSequence()
        {
            ended = true;
            owner.endOfSequenceWritten.store (true, std::memory_order_release);
        }

        static constexpr int chunkFrames = 2048;

        PlaybackEngine& owner;

        std::mutex requestLock;     // message thread <-> disk thread only
        AudioSequence sequence;     // guarded by requestLock
        Request request;            // guarded by requestLock
        int requestGeneration = 0;  // guarded by requestLock

        std::atomic<int> startedGeneration { 0 };
        juce::WaitableEvent wake;
        mutable juce::WaitableEvent started;

        // Disk thread only.
        AudioSequence playing;
        juce::int64 readPos = 0, endPos = 0;
        double speed = 1.0;
        bool resample = false, ended = false, active = false;
        juce::WindowedSincInterpolator interpolators[2];
        juce::AudioBuffer<float> input, output;
    };

    //==============================================================================
    PlaybackEngine::PlaybackEngine()
        : diskThread (std::make_unique<DiskThread> (*this)) {}

    PlaybackEngine::~PlaybackEngine()
    {
        haltAudioThread();
        diskThread = nullptr;
    }

    void PlaybackEngine::setSequence (AudioSequence sequence)
    {
        stop();
        diskThread->setSequence (std::move (sequence));
    }

    bool PlaybackEngine::play (juce::int64 startFrame, juce::int64 endFrame)
    {
        stop();

        const auto rate = deviceRate.load();
        if (! deviceRunning.load() || rate <= 0 || ! diskThread->hasAudio())
            return false;

        const auto ratio = diskThread->getSequenceRate() / rate;
        startFrame = juce::jlimit ((juce::int64) 0, juce::jmax ((juce::int64) 0, diskThread->getSequenceLength() - 1), startFrame);

        startPosition = startFrame;
        framesPlayed = 0;
        speedRatio = ratio;
        underruns = 0;
        finishedFlag = false;

        const auto gen = diskThread->requestStart (startFrame, ratio, endFrame);
        if (! diskThread->waitForStart (gen, 3000))
        {
            diskThread->requestHalt();
            return false;
        }

        state = State::playing;
        return true;
    }

    void PlaybackEngine::stop()
    {
        haltAudioThread();
        diskThread->requestHalt();
    }

    bool PlaybackEngine::takeFinishedFlag()
    {
        return finishedFlag.exchange (false);
    }

    juce::int64 PlaybackEngine::getPosition() const noexcept
    {
        return startPosition.load() + (juce::int64) std::llround ((double) framesPlayed.load() * speedRatio.load());
    }

    bool PlaybackEngine::waitUntilBuffered (int timeoutMs) const
    {
        return diskThread->waitForStart (diskThread->getLastRequestGeneration(), timeoutMs);
    }

    void PlaybackEngine::haltAudioThread()
    {
        state = State::stopped;

        // A callback that began before the store above may still be reading the
        // FIFO; it finishes within one audio block.
        while (inCallback.load())
            std::this_thread::yield();
    }

    void PlaybackEngine::prepare (double sampleRate, int)
    {
        state = State::stopped; // the format changed: the UI notices and stops
        deviceRate = sampleRate;
        deviceRunning = sampleRate > 0;
        tonePhase = 0.0;
    }

    void PlaybackEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
    {
        prepare (device != nullptr ? device->getCurrentSampleRate() : 0.0,
                 device != nullptr ? device->getCurrentBufferSizeSamples() : 0);
    }

    void PlaybackEngine::audioDeviceStopped()
    {
        state = State::stopped;
        deviceRunning = false;
    }

    void PlaybackEngine::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                           float* const* outputs, int numOutputs,
                                                           int numSamples, const juce::AudioIODeviceCallbackContext&)
    {
        inCallback = true;

        for (int c = 0; c < numOutputs; ++c)
            if (outputs[c] != nullptr)
                juce::FloatVectorOperations::clear (outputs[c], numSamples);

        if (testTone.load())
        {
            const auto delta = juce::MathConstants<double>::twoPi * 440.0 / juce::jmax (1.0, deviceRate.load());
            for (int i = 0; i < numSamples; ++i)
            {
                const auto v = (float) (0.1 * std::sin (tonePhase));
                tonePhase = std::fmod (tonePhase + delta, juce::MathConstants<double>::twoPi);
                for (int c = 0; c < juce::jmin (numOutputs, 2); ++c)
                    if (outputs[c] != nullptr)
                        outputs[c][i] = v;
            }
        }
        else if (state.load() == State::playing)
        {
            const auto n = juce::jmin (numSamples, fifo.getNumReady());
            int start1, size1, start2, size2;
            fifo.prepareToRead (n, start1, size1, start2, size2);

            auto copyOut = [&] (int fifoStart, int count, int outOffset)
            {
                if (count <= 0)
                    return;

                const auto* left = fifoBuffer.getReadPointer (0, fifoStart);
                const auto* right = fifoBuffer.getReadPointer (1, fifoStart);

                if (numOutputs == 1 && outputs[0] != nullptr)
                {
                    juce::FloatVectorOperations::copyWithMultiply (outputs[0] + outOffset, left, 0.5f, count);
                    juce::FloatVectorOperations::addWithMultiply (outputs[0] + outOffset, right, 0.5f, count);
                    return;
                }

                if (numOutputs > 0 && outputs[0] != nullptr)
                    juce::FloatVectorOperations::copy (outputs[0] + outOffset, left, count);
                if (numOutputs > 1 && outputs[1] != nullptr)
                    juce::FloatVectorOperations::copy (outputs[1] + outOffset, right, count);
            };

            copyOut (start1, size1, 0);
            copyOut (start2, size2, size1);
            fifo.finishedRead (size1 + size2);
            framesPlayed += size1 + size2;

            if (n < numSamples)
            {
                if (endOfSequenceWritten.load (std::memory_order_acquire) && fifo.getNumReady() == 0)
                {
                    state = State::finished;
                    finishedFlag = true;
                }
                else
                {
                    ++underruns;
                }
            }
        }

        inCallback = false;
    }
}
