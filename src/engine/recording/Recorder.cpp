// SPDX-License-Identifier: AGPL-3.0-only
#include "engine/recording/Recorder.h"

#include <cmath>
#include <thread>

namespace mxj
{
    //==============================================================================
    class Recorder::WriterThread final : public juce::Thread
    {
    public:
        explicit WriterThread (Recorder& o) : Thread ("MXJ recording writer"), owner (o)
        {
            startThread (Priority::high);
        }

        ~WriterThread() override
        {
            stopThread (10000);
        }

        void run() override
        {
            auto lastFlush = juce::Time::getMillisecondCounter();

            while (! threadShouldExit())
            {
                owner.drain();

                // Push data to the disk every second, so a crash loses little.
                const auto now = juce::Time::getMillisecondCounter();
                if (now - lastFlush > 1000)
                {
                    lastFlush = now;
                    const std::lock_guard<std::mutex> lock (owner.segmentLock);
                    if (! owner.segments.empty() && owner.segments.back().writer != nullptr)
                        owner.segments.back().writer->flush();
                }

                wait (10);
            }
        }

    private:
        Recorder& owner;
    };

    //==============================================================================
    Recorder::Recorder() = default;

    Recorder::~Recorder()
    {
        stop();
    }

    bool Recorder::start (BlockStore& s, int numChannels, double sampleRate)
    {
        if (isRecording() || numChannels < 1 || numChannels > 2 || sampleRate <= 0)
            return false;

        store = &s;
        channels = numChannels;
        rate = sampleRate;
        segmentFrames = (juce::int64) (segmentSeconds * sampleRate);
        framesWritten = 0;
        dropped = 0;
        writeFailed = false;
        deviceStopped = false;
        drainBuffer.setSize (channels, fifoFrames);

        {
            const std::lock_guard<std::mutex> lock (segmentLock);
            segments.clear();
        }

        if (startSegment() == nullptr)
            return false;

        fifo.reset(); // the audio thread does not capture yet
        for (auto& p : peakLevels)
            p = 0.0f;

        writerThread = std::make_unique<WriterThread> (*this);
        capturing = true;
        return true;
    }

    AudioSequence Recorder::stop()
    {
        if (! isRecording())
            return {};

        capturing = false;
        while (inCallback.load())
            std::this_thread::yield();

        writerThread = nullptr; // joins; the thread drained before it ended
        drain();                // anything that arrived in between

        AudioSequence take (channels, rate);
        const std::lock_guard<std::mutex> lock (segmentLock);

        for (auto& seg : segments)
        {
            if (seg.writer != nullptr)
                finishSegment (seg);

            if (seg.source != nullptr && seg.source->getNumFrames() > 0)
                take.append (AudioSequence::fromSource (seg.source));
        }

        segments.clear();
        store = nullptr;
        return take;
    }

    Recorder::Segment* Recorder::startSegment()
    {
        Segment seg;
        seg.writer = store->createWriter (channels, rate);
        seg.peaks = std::make_shared<PeakCache> (channels, segmentFrames);
        seg.start = framesWritten.load();

        const std::lock_guard<std::mutex> lock (segmentLock);
        segments.push_back (std::move (seg));
        return &segments.back();
    }

    bool Recorder::finishSegment (Segment& seg)
    {
        seg.peaks->finishEarly();
        seg.source = seg.writer->finish();
        seg.writer = nullptr;

        if (seg.source == nullptr)
            return false;

        seg.source->setPeaks (seg.peaks);
        return true;
    }

    void Recorder::drain()
    {
        while (! writeFailed)
        {
            const auto ready = fifo.getNumReady();
            if (ready <= 0)
                return;

            Segment* current;
            {
                const std::lock_guard<std::mutex> lock (segmentLock);
                current = segments.empty() ? nullptr : &segments.back();
            }

            if (current == nullptr || current->writer == nullptr)
                return;

            if (current->frames >= segmentFrames)
            {
                {
                    const std::lock_guard<std::mutex> lock (segmentLock);
                    finishSegment (*current);
                }

                if (startSegment() == nullptr)
                    writeFailed = true;
                continue;
            }

            const auto count = (int) juce::jmin ((juce::int64) ready, segmentFrames - current->frames);
            int start1, size1, start2, size2;
            fifo.prepareToRead (count, start1, size1, start2, size2);

            for (int c = 0; c < channels; ++c)
            {
                if (size1 > 0) drainBuffer.copyFrom (c, 0, fifoBuffer, c, start1, size1);
                if (size2 > 0) drainBuffer.copyFrom (c, size1, fifoBuffer, c, start2, size2);
            }

            fifo.finishedRead (size1 + size2);
            const auto n = size1 + size2;

            if (! current->writer->append (drainBuffer.getArrayOfReadPointers(), n))
            {
                writeFailed = true;
                return;
            }

            current->peaks->append (drainBuffer.getArrayOfReadPointers(), n);
            current->frames += n;
            framesWritten += n;
        }
    }

    float Recorder::takePeakLevel (int channel) noexcept
    {
        return juce::isPositiveAndBelow (channel, 2) ? peakLevels[channel].exchange (0.0f) : 0.0f;
    }

    bool Recorder::getMinMax (int channel, juce::int64 startFrame, juce::int64 endFrame, float& minOut, float& maxOut) const
    {
        const std::lock_guard<std::mutex> lock (segmentLock);
        bool any = false;

        for (const auto& seg : segments)
        {
            const auto segEnd = seg.start + seg.peaks->getFramesReady();
            if (segEnd <= startFrame || seg.start >= endFrame)
                continue;

            float lo, hi;
            if (seg.peaks->getMinMax (channel, startFrame - seg.start, endFrame - seg.start, lo, hi))
            {
                minOut = any ? juce::jmin (minOut, lo) : lo;
                maxOut = any ? juce::jmax (maxOut, hi) : hi;
                any = true;
            }
        }

        return any;
    }

    juce::Array<juce::File> Recorder::getSegmentFiles() const
    {
        const std::lock_guard<std::mutex> lock (segmentLock);
        juce::Array<juce::File> files;

        for (const auto& seg : segments)
            files.add (seg.writer != nullptr ? seg.writer->getFile() : seg.source != nullptr ? seg.source->getFile() : juce::File());

        return files;
    }

    void Recorder::audioDeviceStopped()
    {
        if (capturing.load())
            deviceStopped = true;
    }

    void Recorder::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                                     float* const* outputs, int numOutputs,
                                                     int numSamples, const juce::AudioIODeviceCallbackContext&)
    {
        inCallback = true;

        // This callback produces no sound; the playback callback does.
        for (int c = 0; c < numOutputs; ++c)
            if (outputs[c] != nullptr)
                juce::FloatVectorOperations::clear (outputs[c], numSamples);

        if (capturing.load() && numSamples > 0)
        {
            const auto n = juce::jmin (numSamples, fifo.getFreeSpace());
            if (n < numSamples)
                dropped += numSamples - n;

            int start1, size1, start2, size2;
            fifo.prepareToWrite (n, start1, size1, start2, size2);

            for (int c = 0; c < channels; ++c)
            {
                // A mono input fills both channels of a stereo take.
                const float* in = numInputs > 0 ? inputs[juce::jmin (c, numInputs - 1)] : nullptr;

                if (in == nullptr)
                {
                    if (size1 > 0) fifoBuffer.clear (c, start1, size1);
                    if (size2 > 0) fifoBuffer.clear (c, start2, size2);
                    continue;
                }

                if (size1 > 0) fifoBuffer.copyFrom (c, start1, in, size1);
                if (size2 > 0) fifoBuffer.copyFrom (c, start2, in + size1, size2);

                const auto range = juce::FloatVectorOperations::findMinAndMax (in, numSamples);
                const auto level = juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd()));
                auto old = peakLevels[c].load();
                while (level > old && ! peakLevels[c].compare_exchange_weak (old, level)) {}
            }

            fifo.finishedWrite (size1 + size2); // the writer thread polls every 10 ms
        }

        inCallback = false;
    }
}
