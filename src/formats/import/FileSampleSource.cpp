// SPDX-License-Identifier: AGPL-3.0-only
#include "formats/import/FileSampleSource.h"

namespace mxj
{
    FileSampleSource::FileSampleSource (juce::File f, std::unique_ptr<juce::AudioFormatReader> r)
        : file (std::move (f)), reader (std::move (r))
    {
        jassert (reader != nullptr);
    }

    bool FileSampleSource::read (float* const* dest, int numDestChannels, juce::int64 startFrame, int numFrames) const
    {
        const auto range = clipReadRange (dest, numDestChannels, startFrame, numFrames, getNumFrames());
        if (range.isEmpty())
            return true;

        // One pointer per channel of the file; JUCE skips null pointers.
        const int fileChannels = getNumChannels();
        const int offset = (int) (range.getStart() - startFrame);
        float* pointers[64] {};

        for (int c = 0; c < juce::jmin (fileChannels, numDestChannels, 64); ++c)
            pointers[c] = dest[c] + offset;

        const std::lock_guard<std::mutex> lock (readerLock);
        return reader->read (pointers, juce::jmin (fileChannels, 64), range.getStart(), (int) range.getLength());
    }
}
