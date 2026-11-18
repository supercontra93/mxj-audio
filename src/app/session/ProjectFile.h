// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "app/document/BackgroundJob.h"
#include "core/audio/BlockStore.h"
#include "core/session/Session.h"

#include <map>

namespace mxj
{
    /** Saving and opening .mxj projects (master plan, section 6).

        A project is two things next to each other:
        - `Name.mxj`: JSON with a format name, a schema version, the session
          (core/session/SessionFile.h) and the table of audio sources;
        - `Name_data/`: audio that exists nowhere else (recordings, decoded MP3
          and FLAC files, converted audio), as raw 32-bit float files with a
          peak file each, so the project opens quickly.

        WAV and AIFF files used by the session stay where they are; the project
        refers to them by full path and by path relative to the project, so a
        project folder can be moved or copied to another computer.
    */
    namespace projectFile
    {
        inline constexpr const char* extension = ".mxj";
        inline constexpr const char* formatName = "MXJ AUDIO project";

        /** The data folder of a project file. */
        juce::File getDataFolder (const juce::File& projectFile);

        /** Temporary block files already copied into a data folder, so the next
            save does not copy them again. Kept by the caller between saves. */
        using CopiedFiles = std::map<juce::String, juce::File>;

        /** Writes the project (atomically: temp file + rename). Blocks; run it
            in a BackgroundJob. Sets job.error (translated) on failure. */
        bool save (const Session&, const juce::File& file, CopiedFiles& copied, BackgroundJob& job);

        /** Opens a project. Missing audio files become silence and are listed in
            `missingFiles`. Blocks; run it in a BackgroundJob. */
        bool load (const juce::File& file, BlockStore& store, const juce::File& peakCacheFolder,
                   Session& out, juce::StringArray& missingFiles, BackgroundJob& job);
    }
}
