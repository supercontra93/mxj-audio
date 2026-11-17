// SPDX-License-Identifier: AGPL-3.0-only
#include "core/audio/BlockStore.h"

namespace mxj
{
    //==============================================================================
    BlockFileSource::BlockFileSource (juce::File f, int numChannels, double sampleRate, juce::int64 numFrames)
        : file (std::move (f)), channels (numChannels), rate (sampleRate), frames (numFrames) {}

    BlockFileSource::~BlockFileSource()
    {
        stream = nullptr;
        file.deleteFile();
    }

    bool BlockFileSource::read (float* const* dest, int numDestChannels, juce::int64 startFrame, int numFrames) const
    {
        const auto range = clipReadRange (dest, numDestChannels, startFrame, numFrames, frames);
        if (range.isEmpty())
            return true;

        const std::lock_guard<std::mutex> lock (streamLock);

        if (stream == nullptr)
        {
            stream = std::make_unique<juce::FileInputStream> (file);
            if (stream->failedToOpen())
            {
                stream = nullptr;
                return false;
            }
        }

        const auto count = (int) range.getLength();
        const auto needed = count * channels;
        if (needed > interleavedCapacity)
        {
            interleaved.realloc ((size_t) needed);
            interleavedCapacity = needed;
        }

        const auto bytes = (int) ((size_t) needed * sizeof (float));
        if (! stream->setPosition (range.getStart() * channels * (juce::int64) sizeof (float))
            || stream->read (interleaved.get(), bytes) != bytes)
            return false;

        const int offset = (int) (range.getStart() - startFrame);
        for (int c = 0; c < juce::jmin (numDestChannels, channels); ++c)
        {
            auto* d = dest[c] + offset;
            const auto* s = interleaved.get() + c;
            for (int i = 0; i < count; ++i)
                d[i] = s[i * channels];
        }

        return true;
    }

    //==============================================================================
    BlockStore::Writer::Writer (juce::File f, int numChannels, double sampleRate)
        : file (std::move (f)), channels (numChannels), rate (sampleRate)
    {
        out = std::make_unique<juce::FileOutputStream> (file, 1 << 20);
        failed = out->failedToOpen() || channels <= 0;
    }

    bool BlockStore::Writer::append (const float* const* channelData, int numFrames)
    {
        if (failed || numFrames <= 0)
            return ! failed;

        const auto needed = numFrames * channels;
        if (needed > interleavedCapacity)
        {
            interleaved.realloc ((size_t) needed);
            interleavedCapacity = needed;
        }

        for (int c = 0; c < channels; ++c)
        {
            auto* d = interleaved.get() + c;
            const auto* s = channelData[c];
            for (int i = 0; i < numFrames; ++i)
                d[i * channels] = s[i];
        }

        if (! out->write (interleaved.get(), (size_t) needed * sizeof (float)))
        {
            failed = true;
            return false;
        }

        framesWritten += numFrames;
        return true;
    }

    std::shared_ptr<BlockFileSource> BlockStore::Writer::finish()
    {
        if (out != nullptr)
        {
            out->flush();
            failed = failed || out->getStatus().failed();
            out = nullptr;
        }

        if (failed)
        {
            file.deleteFile();
            return nullptr;
        }

        return std::make_shared<BlockFileSource> (file, channels, rate, framesWritten);
    }

    //==============================================================================
    BlockStore::BlockStore (const juce::File& parentFolder)
    {
        parentFolder.createDirectory();
        folder = parentFolder.getNonexistentChildFile ("run-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S"), "", false);
        folder.createDirectory();

        runLock = std::make_unique<juce::InterProcessLock> (lockNameFor (folder));
        runLock->enter (0);
    }

    BlockStore::~BlockStore()
    {
        // Sources still alive delete their own files; this removes the rest.
        folder.deleteRecursively();
        runLock = nullptr;
    }

    std::unique_ptr<BlockStore::Writer> BlockStore::createWriter (int numChannels, double sampleRate)
    {
        const auto name = juce::String (nextFileNumber++).paddedLeft ('0', 6) + ".f32";
        return std::unique_ptr<Writer> (new Writer (folder.getChildFile (name), numChannels, sampleRate));
    }

    juce::String BlockStore::lockNameFor (const juce::File& runFolder)
    {
        return "mxj-audio-blocks-" + juce::String::toHexString (runFolder.getFullPathName().hashCode64());
    }

    int BlockStore::cleanUpAbandonedFolders (const juce::File& parentFolder)
    {
        int deleted = 0;

        for (const auto& entry : juce::RangedDirectoryIterator (parentFolder, false, "run-*", juce::File::findDirectories))
        {
            const auto runFolder = entry.getFile();
            juce::InterProcessLock lock (lockNameFor (runFolder));

            // If we can take the lock, the program that made the folder has ended.
            if (lock.enter (0))
            {
                if (runFolder.deleteRecursively())
                    ++deleted;
                lock.exit();
            }
        }

        return deleted;
    }
}
