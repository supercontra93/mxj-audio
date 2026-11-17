// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/audio/AudioSequence.h"
#include "core/audio/BlockStore.h"
#include "core/audio/PeakCache.h"
#include "formats/import/AudioFileFormats.h"

#include <atomic>
#include <functional>

namespace mxj
{
    /** Opens one audio file and turns it into an AudioSequence plus its PeakCache.

        Usage: create() on the message thread (fast, reads only the header), show
        getPeaks() while run() works on a background thread, then take
        getSequence() once run() has returned Result::ok.

        - WAV and AIFF are read in place (no copy). Their peaks are stored in the
          peak cache folder, so the second open of the same file is instant.
        - Compressed formats (FLAC, MP3, Ogg, Opus) are decoded once into the
          BlockStore as 32-bit float, so later reading is fast and exact.
    */
    class AudioImporter
    {
    public:
        enum class Result { ok, cancelled, failed };

        /** Returns nullptr and sets `errorOut` if the file cannot be opened.
            `peakCacheFolder` may be empty to disable the peak file cache. */
        static std::unique_ptr<AudioImporter> create (const juce::File& file, BlockStore& store,
                                                      const juce::File& peakCacheFolder, juce::String& errorOut);

        const AudioFileInfo& getInfo() const noexcept { return info; }

        /** Filled while run() works; safe to draw from any thread. */
        std::shared_ptr<const PeakCache> getPeaks() const { return peaks; }

        /** Does the work. Blocks; call on a background thread. Polls
            `shouldContinue` and returns Result::cancelled when it says false. */
        Result run (const std::function<bool()>& shouldContinue);

        /** 0 to 1. Safe to call from any thread. */
        double getProgress() const noexcept { return progress.load(); }

        /** Valid after run() returned Result::ok. */
        const AudioSequence& getSequence() const noexcept { return sequence; }

        /** Why run() failed. */
        juce::String getError() const { return error; }

        /** True if the peaks came from the peak file cache. */
        bool usedCachedPeaks() const noexcept { return peaksFromCache; }

        /** The cache file used for a given audio file (exposed for tests). */
        static juce::File getPeakFileFor (const juce::File& peakCacheFolder, const AudioFileInfo& info);

    private:
        AudioImporter (AudioFileInfo, std::unique_ptr<juce::AudioFormatReader>, BlockStore&, juce::File peakCacheFolder);

        Result readInPlace (const std::function<bool()>& shouldContinue);
        Result decodeToStore (const std::function<bool()>& shouldContinue);

        AudioFileInfo info;
        std::unique_ptr<juce::AudioFormatReader> reader;
        BlockStore& store;
        juce::File peakFolder;
        std::shared_ptr<PeakCache> peaks;
        bool peaksFromCache = false;

        AudioSequence sequence;
        juce::String error;
        std::atomic<double> progress { 0.0 };

        JUCE_DECLARE_NON_COPYABLE (AudioImporter)
    };
}
