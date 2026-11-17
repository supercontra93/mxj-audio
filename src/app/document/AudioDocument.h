// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/edit/AudioState.h"
#include "formats/import/AudioImporter.h"

#include <juce_events/juce_events.h>

namespace mxj
{
    /** One open file: its audio and markers, its loading state, the file it
        belongs to (none for a new recording) and whether it has unsaved changes.

        Loading runs on a background thread so the window never freezes. The
        waveform can be drawn while loading (getLoadingPeaks() fills up); editing
        and playing wait until the state is `ready`. The audio is changed only by
        the Workspace through setAudioState(), so every change is an undo step.
        Message thread only, except where noted.
    */
    class AudioDocument : private juce::Thread
    {
    public:
        enum class State { loading, ready, failed };

        /** Reads the file header and starts loading. Returns nullptr (with an
            error message) if the file cannot be opened at all. */
        static std::unique_ptr<AudioDocument> open (const juce::File& file, BlockStore& store,
                                                    const juce::File& peakCacheFolder, juce::String& errorOut);

        /** A new, empty document without a file (for recording). */
        static std::unique_ptr<AudioDocument> createEmpty (const juce::String& name, int numChannels, double sampleRate);

        /** A document rebuilt from the crash recovery journal: ready, with unsaved changes. */
        static std::unique_ptr<AudioDocument> createRestored (const juce::String& name, const AudioFileInfo& info,
                                                              AudioState state);

        ~AudioDocument() override;

        State getState() const noexcept                    { return state.load(); }
        bool isReady() const noexcept                      { return getState() == State::ready; }
        bool isLoading() const noexcept                    { return getState() == State::loading; }

        /** 0 to 1. Any thread. */
        double getProgress() const noexcept                { return importer != nullptr ? importer->getProgress() : 1.0; }

        /** The file as it was opened or last saved: format, bit depth, ... For a
            new document only channels and sample rate are set. */
        const AudioFileInfo& getInfo() const noexcept      { return info; }

        /** The file this document is saved to, or an empty File for a new recording. */
        juce::File getFile() const                         { return info.file; }
        bool hasFile() const                               { return info.file != juce::File(); }

        /** File name, or the name given to a new document. */
        juce::String getDisplayName() const;

        /** Peaks of the file while it loads (drawn growing from left to right). */
        std::shared_ptr<const PeakCache> getLoadingPeaks() const { return importer != nullptr ? importer->getPeaks() : nullptr; }

        //==============================================================================
        /** Audio and markers. Empty until the state is `ready`. */
        const AudioState& getAudioState() const noexcept   { return audioState; }
        const AudioSequence& getAudio() const noexcept     { return audioState.audio; }
        const MarkerList& getMarkers() const noexcept      { return audioState.markers; }

        int getNumChannels() const noexcept                { return info.numChannels; }
        double getSampleRate() const noexcept              { return info.sampleRate; }

        /** Length in frames: the audio once ready, the announced length while loading. */
        juce::int64 getLength() const noexcept             { return isReady() ? audioState.audio.getNumFrames() : info.numFrames; }

        /** Replaces audio and markers (Workspace only: edits and undo). `revision`
            identifies the state, so undoing back to the saved state is not "modified". */
        void setAudioState (AudioState newState, juce::int64 revision);
        juce::int64 getRevision() const noexcept           { return revision; }

        bool isModified() const noexcept                   { return revision != savedRevision; }

        /** After saving: the document now belongs to `file` (Save, Save As). */
        void markSaved (const AudioFileInfo& savedAs, juce::int64 savedAtRevision);

        /** After restoring from the journal: nothing has been saved yet. */
        void markUnsaved()                                 { savedRevision = -1; }

        juce::String getError() const                      { return error; }

        /** How long loading took, and whether the peak file cache was used. */
        double getLoadSeconds() const noexcept             { return loadSeconds; }
        bool usedCachedPeaks() const noexcept              { return importer != nullptr && importer->usedCachedPeaks(); }

        /** Called on the message thread when loading has finished or failed. */
        std::function<void()> onLoadFinished;

    private:
        AudioDocument();
        explicit AudioDocument (std::unique_ptr<AudioImporter>);

        void run() override;
        void finishLoading (AudioImporter::Result);

        std::unique_ptr<AudioImporter> importer;
        AudioFileInfo info;
        juce::String name;
        std::atomic<State> state { State::loading };
        AudioState audioState;
        juce::int64 revision = 0, savedRevision = 0;
        juce::String error;
        double loadSeconds = 0.0;
        juce::uint32 startTime = 0;

        JUCE_DECLARE_WEAK_REFERENCEABLE (AudioDocument)
        JUCE_DECLARE_NON_COPYABLE (AudioDocument)
    };
}
