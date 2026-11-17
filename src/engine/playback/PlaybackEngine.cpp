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

        // The device callback has been removed by now (AudioEngine::shutdown).
        delete pendingChain.exchange (nullptr);
        delete retiredChain.exchange (nullptr);
        delete activeChain;
        activeChain = nullptr;
    }

    //==============================================================================
    void PlaybackEngine::setEffectChain (std::unique_ptr<dsp::ProcessorChain> chain)
    {
        if (chain == nullptr)
            chain = std::make_unique<dsp::ProcessorChain>(); // an empty chain plays the audio unchanged

        const auto rate = deviceRate.load() > 0.0 ? deviceRate.load() : 48000.0;
        chain->prepare (rate, juce::jmax (deviceBlockSize, workFrames), 2);
        chainRate = chain->isEmpty() ? 0.0 : rate;

        waitingChain = std::move (chain);
        collectRetiredChain();
    }

    void PlaybackEngine::collectRetiredChain()
    {
        delete retiredChain.exchange (nullptr);

        if (waitingChain == nullptr)
            return;

        // A chain the audio thread has not taken yet is simply replaced.
        delete pendingChain.exchange (waitingChain.release());
    }

    void PlaybackEngine::adoptPendingChain() noexcept
    {
        // Only while the retired slot is free, so a retired chain is never overwritten.
        if (retiredChain.load (std::memory_order_acquire) != nullptr)
            return;

        if (auto* next = pendingChain.exchange (nullptr, std::memory_order_acq_rel))
        {
            if (activeChain != nullptr)
                retiredChain.store (activeChain, std::memory_order_release);

            activeChain = next;
        }
    }

    int PlaybackEngine::readRecentOutput (float* dest, int maxSamples)
    {
        const auto n = juce::jmin (maxSamples, tapFifo.getNumReady());
        const auto skip = tapFifo.getNumReady() - n;
        if (skip > 0)
            tapFifo.finishedRead (skip); // keep only the newest

        int start1, size1, start2, size2;
        tapFifo.prepareToRead (n, start1, size1, start2, size2);
        if (size1 > 0) std::copy_n (tapBuffer.data() + start1, size1, dest);
        if (size2 > 0) std::copy_n (tapBuffer.data() + start2, size2, dest + size1);
        tapFifo.finishedRead (size1 + size2);
        return size1 + size2;
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

        resetChainRequested = true;
        meter.requestReset();
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

    void PlaybackEngine::prepare (double sampleRate, int maximumBlockSize)
    {
        state = State::stopped; // the format changed: the UI notices and stops
        deviceRate = sampleRate;
        deviceRunning = sampleRate > 0;
        tonePhase = 0.0;

        if (maximumBlockSize > 0)
            deviceBlockSize = maximumBlockSize;

        // No callback runs now, so the active chain may be prepared here. The
        // workspace notices the new rate and builds a fresh chain as well.
        if (sampleRate > 0)
        {
            meter.prepare (sampleRate, 2);
            if (activeChain != nullptr)
                activeChain->prepare (sampleRate, juce::jmax (deviceBlockSize, workFrames), 2);
        }
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
        adoptPendingChain();

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
            if (resetChainRequested.exchange (false) && activeChain != nullptr)
                activeChain->reset();

            int taken = 0;

            for (int done = 0; done < numSamples;)
            {
                const auto n = juce::jmin (workFrames, numSamples - done);
                taken += renderBlock (n);

                const auto* left = work.getReadPointer (0);
                const auto* right = work.getReadPointer (1);

                if (numOutputs == 1 && outputs[0] != nullptr)
                {
                    juce::FloatVectorOperations::copyWithMultiply (outputs[0] + done, left, 0.5f, n);
                    juce::FloatVectorOperations::addWithMultiply (outputs[0] + done, right, 0.5f, n);
                }
                else
                {
                    if (numOutputs > 0 && outputs[0] != nullptr)
                        juce::FloatVectorOperations::copy (outputs[0] + done, left, n);
                    if (numOutputs > 1 && outputs[1] != nullptr)
                        juce::FloatVectorOperations::copy (outputs[1] + done, right, n);
                }

                done += n;
            }

            framesPlayed += taken;

            if (taken < numSamples)
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

    int PlaybackEngine::renderBlock (int numFrames) noexcept
    {
        work.clear (0, numFrames);

        const auto n = juce::jmin (numFrames, fifo.getNumReady());
        int start1, size1, start2, size2;
        fifo.prepareToRead (n, start1, size1, start2, size2);

        for (int c = 0; c < 2; ++c)
        {
            if (size1 > 0) work.copyFrom (c, 0, fifoBuffer, c, start1, size1);
            if (size2 > 0) work.copyFrom (c, size1, fifoBuffer, c, start2, size2);
        }

        fifo.finishedRead (size1 + size2);

        if (activeChain != nullptr && ! activeChain->isEmpty())
            activeChain->process (work.getArrayOfWritePointers(), 2, numFrames);

        meter.process (work.getArrayOfReadPointers(), 2, numFrames);

        // Spectrum tap: drop what does not fit; the analyser only needs recent audio.
        const auto toTap = juce::jmin (numFrames, tapFifo.getFreeSpace());
        tapFifo.prepareToWrite (toTap, start1, size1, start2, size2);
        const auto* left = work.getReadPointer (0);
        const auto* right = work.getReadPointer (1);

        for (int i = 0; i < size1; ++i)
            tapBuffer[(size_t) (start1 + i)] = (left[i] + right[i]) * 0.5f;
        for (int i = 0; i < size2; ++i)
            tapBuffer[(size_t) (start2 + i)] = (left[size1 + i] + right[size1 + i]) * 0.5f;

        tapFifo.finishedWrite (size1 + size2);
        return n;
    }
}
