// SPDX-License-Identifier: AGPL-3.0-only
#include "formats/import/AudioImporter.h"
#include "formats/import/FileSampleSource.h"

namespace mxj
{
    namespace
    {
        constexpr int chunkFrames = 1 << 16;
    }

    std::unique_ptr<AudioImporter> AudioImporter::create (const juce::File& file, BlockStore& store,
                                                          const juce::File& peakCacheFolder, juce::String& errorOut)
    {
        AudioFileInfo info;
        if (! audioFormats::readInfo (file, info, errorOut))
            return nullptr;

        if (info.numChannels > 64)
        {
            errorOut = TRANS ("Files with more than 64 channels are not supported.");
            return nullptr;
        }

        auto reader = audioFormats::createReader (file, errorOut);
        if (reader == nullptr)
            return nullptr;

        return std::unique_ptr<AudioImporter> (new AudioImporter (std::move (info), std::move (reader), store, peakCacheFolder));
    }

    AudioImporter::AudioImporter (AudioFileInfo i, std::unique_ptr<juce::AudioFormatReader> r, BlockStore& s, juce::File folder)
        : info (std::move (i)), reader (std::move (r)), store (s), peakFolder (std::move (folder))
    {
        if (info.isUncompressed && peakFolder != juce::File())
        {
            if (auto cached = PeakCache::load (getPeakFileFor (peakFolder, info), info.numChannels, info.numFrames))
            {
                peaks = std::move (cached);
                peaksFromCache = true;
            }
        }

        if (peaks == nullptr)
            peaks = std::make_shared<PeakCache> (info.numChannels, info.numFrames);
    }

    juce::File AudioImporter::getPeakFileFor (const juce::File& peakCacheFolder, const AudioFileInfo& info)
    {
        // Any change to the file (path, size, time) gives a different name.
        const auto key = info.file.getFullPathName()
                       + "|" + juce::String (info.file.getSize())
                       + "|" + juce::String (info.file.getLastModificationTime().toMilliseconds());

        return peakCacheFolder.getChildFile (juce::String::toHexString (key.hashCode64()) + ".mxjpeaks");
    }

    AudioImporter::Result AudioImporter::run (const std::function<bool()>& shouldContinue)
    {
        const auto result = info.isUncompressed ? readInPlace (shouldContinue) : decodeToStore (shouldContinue);

        if (result == Result::ok)
            progress = 1.0;

        return result;
    }

    AudioImporter::Result AudioImporter::readInPlace (const std::function<bool()>& shouldContinue)
    {
        auto source = std::make_shared<FileSampleSource> (info.file, std::move (reader));

        if (! peaksFromCache)
        {
            juce::AudioBuffer<float> buffer (info.numChannels, chunkFrames);

            for (juce::int64 pos = 0; pos < info.numFrames; pos += chunkFrames)
            {
                if (shouldContinue != nullptr && ! shouldContinue())
                    return Result::cancelled;

                const auto count = (int) juce::jmin ((juce::int64) chunkFrames, info.numFrames - pos);
                if (! source->read (buffer.getArrayOfWritePointers(), info.numChannels, pos, count))
                {
                    error = TRANS ("Reading the file failed.");
                    return Result::failed;
                }

                peaks->append (buffer.getArrayOfReadPointers(), count);
                progress = (double) (pos + count) / (double) juce::jmax ((juce::int64) 1, info.numFrames);
            }

            if (info.numFrames == 0)
                peaks->finishEarly();

            if (peakFolder != juce::File() && peakFolder.createDirectory())
                peaks->save (getPeakFileFor (peakFolder, info));
        }

        sequence = AudioSequence::fromSource (source);
        return Result::ok;
    }

    AudioImporter::Result AudioImporter::decodeToStore (const std::function<bool()>& shouldContinue)
    {
        auto writer = store.createWriter (info.numChannels, info.sampleRate);
        juce::AudioBuffer<float> buffer (info.numChannels, chunkFrames);

        for (juce::int64 pos = 0; pos < info.numFrames; pos += chunkFrames)
        {
            if (shouldContinue != nullptr && ! shouldContinue())
            {
                writer->finish(); // the returned source deletes the file again
                return Result::cancelled;
            }

            const auto count = (int) juce::jmin ((juce::int64) chunkFrames, info.numFrames - pos);
            if (! reader->read (buffer.getArrayOfWritePointers(), info.numChannels, pos, count))
            {
                writer->finish();
                error = TRANS ("Decoding the file failed. It may be damaged.");
                return Result::failed;
            }

            if (! writer->append (buffer.getArrayOfReadPointers(), count))
            {
                error = TRANS ("Could not write temporary audio data. Is the disk full?");
                return Result::failed;
            }

            peaks->append (buffer.getArrayOfReadPointers(), count);
            progress = (double) (pos + count) / (double) juce::jmax ((juce::int64) 1, info.numFrames);
        }

        if (info.numFrames == 0)
            peaks->finishEarly();

        reader = nullptr;
        const auto source = writer->finish();

        if (source == nullptr)
        {
            error = TRANS ("Could not write temporary audio data. Is the disk full?");
            return Result::failed;
        }

        sequence = AudioSequence::fromSource (source);
        return Result::ok;
    }
}
