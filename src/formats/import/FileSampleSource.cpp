// SPDX-License-Identifier: AGPL-3.0-only
#include "formats/import/FileSampleSource.h"
#include "formats/import/AudioFileFormats.h"

#include <algorithm>

namespace mxj
{
    namespace
    {
        /** Every live FileSampleSource, so saving over a file can find its readers. */
        struct Registry
        {
            std::mutex lock;
            std::vector<FileSampleSource*> sources;
        };

        Registry& registry()
        {
            static Registry r;
            return r;
        }
    }

    FileSampleSource::FileSampleSource (juce::File f, std::unique_ptr<juce::AudioFormatReader> r)
        : file (std::move (f)), reader (std::move (r)),
          channels ((int) reader->numChannels), frames (reader->lengthInSamples), rate (reader->sampleRate)
    {
        const std::lock_guard<std::mutex> lock (registry().lock);
        registry().sources.push_back (this);
    }

    FileSampleSource::~FileSampleSource()
    {
        const std::lock_guard<std::mutex> lock (registry().lock);
        auto& v = registry().sources;
        v.erase (std::remove (v.begin(), v.end(), this), v.end());
    }

    bool FileSampleSource::relocateAllReading (const juce::File& target, const std::function<juce::File()>& makeCopyName)
    {
        // Held throughout, so no source is destroyed while we use it.
        const std::lock_guard<std::mutex> lock (registry().lock);
        juce::File copy;

        for (auto* source : registry().sources)
        {
            if (source->getFile() != target)
                continue;

            if (copy == juce::File())
            {
                copy = makeCopyName();
                if (! target.copyFileTo (copy))
                {
                    copy.deleteFile();
                    return false;
                }
            }

            if (! source->relocate (copy))
                return false;
        }

        return true;
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

    juce::File FileSampleSource::getFile() const
    {
        const std::lock_guard<std::mutex> lock (readerLock);
        return file;
    }

    bool FileSampleSource::relocate (const juce::File& copy)
    {
        juce::String error;
        auto newReader = audioFormats::createReader (copy, error);

        if (newReader == nullptr || newReader->lengthInSamples != reader->lengthInSamples
            || newReader->numChannels != reader->numChannels)
            return false;

        const std::lock_guard<std::mutex> lock (readerLock);
        reader = std::move (newReader);
        file = copy;
        return true;
    }

    juce::var FileSampleSource::describe() const
    {
        const auto f = getFile();
        auto* o = new juce::DynamicObject();
        o->setProperty ("type", "file");
        o->setProperty ("path", f.getFullPathName());
        o->setProperty ("size", f.getSize());
        o->setProperty ("modified", f.getLastModificationTime().toMilliseconds());
        return juce::var (o);
    }
}
