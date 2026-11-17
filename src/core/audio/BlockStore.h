// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/audio/SampleSource.h"

#include <mutex>

namespace mxj
{
    /** A SampleSource stored as a raw file of interleaved 32-bit float frames.
        Created by BlockStore::Writer. The file is deleted when the last
        reference to the source goes away. */
    class BlockFileSource final : public SampleSource
    {
    public:
        BlockFileSource (juce::File file, int numChannels, double sampleRate, juce::int64 numFrames);
        ~BlockFileSource() override;

        int getNumChannels() const override         { return channels; }
        juce::int64 getNumFrames() const override   { return frames; }
        double getSampleRate() const override       { return rate; }

        bool read (float* const* dest, int numDestChannels,
                   juce::int64 startFrame, int numFrames) const override;

        juce::File getFile() const { return file; }

        /** {"type": "block", "path", "channels", "rate", "frames"}. */
        juce::var describe() const override;

    private:
        juce::File file;
        int channels;
        double rate;
        juce::int64 frames;

        mutable std::mutex streamLock;
        mutable std::unique_ptr<juce::FileInputStream> stream;
        mutable juce::HeapBlock<float> interleaved;
        mutable int interleavedCapacity = 0;
    };

    /** The folder where decoded and processed audio is kept while the program runs.

        Each program run gets its own sub folder (cache/blocks/run-...), guarded by
        an inter-process lock. Folders left behind by a crash are removed by the
        next run (cleanUpAbandonedFolders). Nothing here is meant to be kept:
        saving a file or project (phase 2 and 5) writes the audio elsewhere.
    */
    class BlockStore
    {
    public:
        /** Creates a new run folder inside `parentFolder`. */
        explicit BlockStore (const juce::File& parentFolder);
        ~BlockStore();

        /** Writes one new source. Not thread safe: use one writer per thread. */
        class Writer
        {
        public:
            /** Appends frames (one pointer per channel). Returns false on a write error. */
            bool append (const float* const* channelData, int numFrames);

            /** Finishes the file and returns the source, or nullptr on error. */
            std::shared_ptr<BlockFileSource> finish();

            juce::int64 getNumFramesWritten() const noexcept { return framesWritten; }

            /** Pushes buffered frames to the disk (so a crash loses little). */
            void flush() { if (out != nullptr) out->flush(); }

            juce::File getFile() const { return file; }
            int getNumChannels() const noexcept { return channels; }
            double getSampleRate() const noexcept { return rate; }

        private:
            friend class BlockStore;
            Writer (juce::File, int numChannels, double sampleRate);

            juce::File file;
            int channels;
            double rate;
            juce::int64 framesWritten = 0;
            std::unique_ptr<juce::FileOutputStream> out;
            juce::HeapBlock<float> interleaved;
            int interleavedCapacity = 0;
            bool failed = false;
        };

        /** Starts a new block file. Thread safe. */
        std::unique_ptr<Writer> createWriter (int numChannels, double sampleRate);

        juce::File getFolder() const { return folder; }

        /** A new, unused file name in the run folder (for copies that are not
            float blocks, e.g. the original of a file that is being saved over).
            Files there are deleted with the folder. Thread safe. */
        juce::File createFileName (const juce::String& extension);

        /** Takes over a block file of an earlier run (crash recovery): moves it
            into this run's folder and returns it as a source, or nullptr if the
            file is missing or has the wrong size. */
        std::shared_ptr<BlockFileSource> adopt (const juce::File& blockFile, int numChannels,
                                                double sampleRate, juce::int64 numFrames);

        /** Name of the crash recovery journal inside a run folder. */
        static constexpr const char* journalFileName = "recovery.json";

        /** The newest run folder in `parentFolder` whose program is no longer
            running and which holds a recovery journal, or an empty File. */
        static juce::File findRecoverableFolder (const juce::File& parentFolder);

        /** Deletes run folders in `parentFolder` whose program is no longer running,
            except `keep` (a folder whose work may still be recovered).
            Returns the number of folders deleted. */
        static int cleanUpAbandonedFolders (const juce::File& parentFolder, const juce::File& keep = {});

    private:
        static juce::String lockNameFor (const juce::File& runFolder);

        juce::File folder;
        std::unique_ptr<juce::InterProcessLock> runLock;
        std::atomic<int> nextFileNumber { 1 };

        JUCE_DECLARE_NON_COPYABLE (BlockStore)
    };
}
