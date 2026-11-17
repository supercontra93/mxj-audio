// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "app/document/AudioDocument.h"
#include "app/document/BackgroundJob.h"

namespace mxj
{
    /** Crash recovery (master plan, section 6; docs/adr/0006).

        While a document has unsaved changes, the Workspace keeps a small JSON
        journal in this run's block folder: which files and block files hold the
        audio and how the edits combine them (never the audio itself), plus the
        markers and a take that is being recorded. A normal exit deletes the
        folder. After a crash the folder stays; at the next start the program
        offers to rebuild the document from it.
    */
    namespace recovery
    {
        /** A take that is being recorded while the journal is written. */
        struct TakeInfo
        {
            juce::Array<juce::File> files; ///< segment block files, in order
            int numChannels = 0;
            double sampleRate = 0;
            FrameRange insertAt;           ///< where the take was going in the document
        };

        /** Writes the journal (atomically: temp file + rename). */
        bool write (const juce::File& journalFile, const AudioDocument& doc, const TakeInfo* take);

        /** The document name stored in a journal, for the question to the user. */
        juce::String readName (const juce::File& runFolder);

        struct Restored
        {
            juce::String name;
            AudioFileInfo info; ///< file it belonged to (may be empty)
            AudioState state;
        };

        /** Rebuilds the document of an earlier run: takes over its block files
            into `store`, reopens original files (which must be unchanged) and
            computes the peaks. Blocks; runs in a BackgroundJob. */
        bool restore (const juce::File& runFolder, BlockStore& store, Restored& out, BackgroundJob& job);
    }
}
