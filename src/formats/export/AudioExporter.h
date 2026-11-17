// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/edit/AudioState.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <functional>
#include <optional>

namespace mxj
{
    /** The formats MXJ AUDIO can save, all written by code inside JUCE.
        MP3 and M4A need an extra encoder and come later (docs/later.md). */
    enum class ExportFormat { wav, aiff, flac, ogg, opus };

    struct ExportSettings
    {
        juce::File file;
        ExportFormat format = ExportFormat::wav;
        int bitsPerSample = 24;     ///< WAV 16/24/32, AIFF 16/24, FLAC 16/24; ignored by Ogg and Opus
        bool floatingPoint = false; ///< WAV with 32 bits: IEEE float instead of integers
        int qualityIndex = -1;      ///< index into getQualityOptions(); -1 = our default
        MarkerList markers;         ///< written as cue points (WAV only)
    };

    /** Writes an AudioSequence to an audio file.

        The file is written next to the target under a temporary name and only
        replaces the target when everything went well, so a failed or cancelled
        save never damages an existing file. Blocks; run it on a background thread.
    */
    namespace audioExport
    {
        enum class Result { ok, cancelled, failed };

        /** "wav", "aiff", "flac", "ogg", "opus". */
        juce::String getExtension (ExportFormat);

        /** "WAV", "AIFF", "FLAC", "Ogg Vorbis", "Opus". */
        juce::String getDisplayName (ExportFormat);

        /** The format that belongs to a file name, if we can write it. */
        std::optional<ExportFormat> formatForFile (const juce::File&);

        /** Bit depths offered for a format (empty for Ogg and Opus). */
        juce::Array<int> getBitDepths (ExportFormat);

        /** Bit rate / compression choices (empty for WAV and AIFF). */
        juce::StringArray getQualityOptions (ExportFormat);
        int getDefaultQualityIndex (ExportFormat);

        /** True if the format supports this sample rate. */
        bool supportsSampleRate (ExportFormat, double sampleRate);

        /** Writes the audio. `progress` (may be null) goes from 0 to 1.
            Returns failed with a translated message in `errorOut`. */
        Result write (const AudioSequence& audio, const ExportSettings& settings,
                      const std::function<bool()>& shouldContinue,
                      std::atomic<double>* progress, juce::String& errorOut);
    }
}
