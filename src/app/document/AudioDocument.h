// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "formats/import/AudioImporter.h"

#include <juce_events/juce_events.h>

namespace mxj
{
    /** One open audio file: its audio, its peaks and its loading state.

        Loading runs on a background thread so the window never freezes. The
        waveform can be drawn while loading (getPeaks() fills up); playing waits
        until the state is `ready`. Message thread only, except where noted.
    */
    class AudioDocument : private juce::Thread
    {
    public:
        enum class State { loading, ready, failed };

        /** Reads the file header and starts loading. Returns nullptr (with an
            error message) if the file cannot be opened at all. */
        static std::unique_ptr<AudioDocument> open (const juce::File& file, BlockStore& store,
                                                    const juce::File& peakCacheFolder, juce::String& errorOut);

        ~AudioDocument() override;

        State getState() const noexcept                    { return state.load(); }
        bool isReady() const noexcept                      { return getState() == State::ready; }

        /** 0 to 1. Any thread. */
        double getProgress() const noexcept                { return importer->getProgress(); }

        const AudioFileInfo& getInfo() const noexcept      { return importer->getInfo(); }
        std::shared_ptr<const PeakCache> getPeaks() const  { return importer->getPeaks(); }
        juce::File getFile() const                         { return getInfo().file; }

        /** Empty until the state is `ready`. */
        const AudioSequence& getSequence() const noexcept  { return sequence; }

        juce::String getError() const                      { return error; }

        /** How long loading took, and whether the peak file cache was used. */
        double getLoadSeconds() const noexcept             { return loadSeconds; }
        bool usedCachedPeaks() const noexcept              { return importer->usedCachedPeaks(); }

        /** Called on the message thread when loading has finished or failed. */
        std::function<void()> onLoadFinished;

    private:
        explicit AudioDocument (std::unique_ptr<AudioImporter>);

        void run() override;
        void finishLoading (AudioImporter::Result);

        std::unique_ptr<AudioImporter> importer;
        std::atomic<State> state { State::loading };
        AudioSequence sequence;
        juce::String error;
        double loadSeconds = 0.0;
        juce::uint32 startTime = 0;

        JUCE_DECLARE_WEAK_REFERENCEABLE (AudioDocument)
        JUCE_DECLARE_NON_COPYABLE (AudioDocument)
    };
}
