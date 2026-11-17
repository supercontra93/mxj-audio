// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

namespace mxj
{
    /** What we know about an audio file before importing it. */
    struct AudioFileInfo
    {
        juce::File file;
        juce::String formatName;     ///< e.g. "WAV file", as JUCE names it
        int numChannels = 0;
        double sampleRate = 0.0;
        juce::int64 numFrames = 0;
        int bitsPerSample = 0;
        bool isUncompressed = false; ///< WAV or AIFF: read directly, no decoding needed

        double getLengthInSeconds() const { return sampleRate > 0 ? (double) numFrames / sampleRate : 0.0; }
    };

    /** The audio file formats MXJ AUDIO can open.

        Phase 1: WAV, AIFF, FLAC, Ogg Vorbis, Opus and MP3, all decoded by code
        inside JUCE (licences in THIRD_PARTY.md). M4A, AAC and WMA through the
        decoders built into Windows come later (docs/later.md). Thread safe.
    */
    namespace audioFormats
    {
        /** The shared format manager. Use it only to create readers. */
        juce::AudioFormatManager& getManager();

        /** File chooser pattern, e.g. "*.wav;*.flac;*.mp3". */
        juce::String getWildcard();

        /** True if the file extension belongs to a format we can open. */
        bool canOpen (const juce::File& file);

        /** Opens a reader, or returns nullptr. `errorOut` explains why. */
        std::unique_ptr<juce::AudioFormatReader> createReader (const juce::File& file, juce::String& errorOut);

        /** Reads the header of a file. Returns false (with an error) if it cannot be opened. */
        bool readInfo (const juce::File& file, AudioFileInfo& info, juce::String& errorOut);
    }
}
