// SPDX-License-Identifier: AGPL-3.0-only
#include "formats/export/AudioExporter.h"
#include "formats/metadata/CueMarkers.h"

namespace mxj::audioExport
{
    namespace
    {
        constexpr int chunkFrames = 1 << 16;

        std::unique_ptr<juce::AudioFormat> createFormat (ExportFormat f)
        {
            switch (f)
            {
                case ExportFormat::wav:  return std::make_unique<juce::WavAudioFormat>();
                case ExportFormat::aiff: return std::make_unique<juce::AiffAudioFormat>();
                case ExportFormat::flac: return std::make_unique<juce::FlacAudioFormat>();
                case ExportFormat::ogg:  return std::make_unique<juce::OggVorbisAudioFormat>();
                case ExportFormat::opus: return std::make_unique<juce::OpusAudioFormat>();
            }

            return nullptr;
        }
    }

    juce::String getExtension (ExportFormat f)
    {
        switch (f)
        {
            case ExportFormat::wav:  return "wav";
            case ExportFormat::aiff: return "aiff";
            case ExportFormat::flac: return "flac";
            case ExportFormat::ogg:  return "ogg";
            case ExportFormat::opus: return "opus";
        }

        return {};
    }

    juce::String getDisplayName (ExportFormat f)
    {
        switch (f)
        {
            case ExportFormat::wav:  return "WAV";
            case ExportFormat::aiff: return "AIFF";
            case ExportFormat::flac: return "FLAC";
            case ExportFormat::ogg:  return "Ogg Vorbis";
            case ExportFormat::opus: return "Opus";
        }

        return {};
    }

    std::optional<ExportFormat> formatForFile (const juce::File& file)
    {
        const auto ext = file.getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".wave" || ext == ".bwf") return ExportFormat::wav;
        if (ext == ".aif" || ext == ".aiff")                  return ExportFormat::aiff;
        if (ext == ".flac")                                   return ExportFormat::flac;
        if (ext == ".ogg")                                    return ExportFormat::ogg;
        if (ext == ".opus")                                   return ExportFormat::opus;
        return std::nullopt;
    }

    juce::Array<int> getBitDepths (ExportFormat f)
    {
        switch (f)
        {
            case ExportFormat::wav:  return { 16, 24, 32 };
            case ExportFormat::aiff: return { 16, 24 };
            case ExportFormat::flac: return { 16, 24 };
            case ExportFormat::ogg:
            case ExportFormat::opus: return {};
        }

        return {};
    }

    juce::StringArray getQualityOptions (ExportFormat f)
    {
        switch (f)
        {
            case ExportFormat::flac: return juce::FlacAudioFormat().getQualityOptions();
            case ExportFormat::ogg:  return juce::OggVorbisAudioFormat().getQualityOptions();
            case ExportFormat::opus: return juce::OpusAudioFormat().getQualityOptions();
            case ExportFormat::wav:
            case ExportFormat::aiff: return {};
        }

        return {};
    }

    int getDefaultQualityIndex (ExportFormat f)
    {
        switch (f)
        {
            case ExportFormat::flac: return 5; // "5 (Default)"
            case ExportFormat::ogg:  return 6; // 192 kbps
            case ExportFormat::opus: return 5; // 128 kbps
            case ExportFormat::wav:
            case ExportFormat::aiff: return 0;
        }

        return 0;
    }

    bool supportsSampleRate (ExportFormat f, double sampleRate)
    {
        if (f == ExportFormat::wav || f == ExportFormat::flac)
            return sampleRate >= 1000.0 && sampleRate <= 384000.0;

        const auto format = createFormat (f);
        return format != nullptr && format->getPossibleSampleRates().contains (juce::roundToInt (sampleRate));
    }

    Result write (const AudioSequence& audio, const ExportSettings& settings,
                  const std::function<bool()>& shouldContinue,
                  std::atomic<double>* progress, juce::String& errorOut)
    {
        const auto format = createFormat (settings.format);
        const auto channels = audio.getNumChannels();

        if (format == nullptr || channels <= 0)
        {
            errorOut = TRANS ("There is no audio to save.");
            return Result::failed;
        }

        if (! supportsSampleRate (settings.format, audio.getSampleRate()))
        {
            errorOut = TRANS ("This format does not support the sample rate of the audio.") + " ("
                     + juce::String (juce::roundToInt (audio.getSampleRate())) + " Hz)";
            return Result::failed;
        }

        const auto target = settings.file;
        if (! target.getParentDirectory().createDirectory())
        {
            errorOut = TRANS ("The folder could not be created.");
            return Result::failed;
        }

        const auto temp = target.getSiblingFile (target.getFileNameWithoutExtension() + ".mxj-saving" + target.getFileExtension());
        temp.deleteFile();

        auto bits = settings.bitsPerSample;
        if (! getBitDepths (settings.format).isEmpty() && ! getBitDepths (settings.format).contains (bits))
            bits = 24;
        if (getBitDepths (settings.format).isEmpty())
            bits = 32;

        auto options = juce::AudioFormatWriterOptions{}
                           .withSampleRate (audio.getSampleRate())
                           .withNumChannels (channels)
                           .withBitsPerSample (bits)
                           .withQualityOptionIndex (settings.qualityIndex >= 0 ? settings.qualityIndex
                                                                               : getDefaultQualityIndex (settings.format));

        if (settings.format == ExportFormat::wav)
        {
            options = options.withSampleFormat (bits == 32 && settings.floatingPoint
                                                    ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                    : juce::AudioFormatWriterOptions::SampleFormat::integral);

            if (! settings.markers.empty())
                options = options.withMetadataValues (cueMarkers::toMetadata (settings.markers));
        }

        auto result = Result::ok;
        {
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (temp);
            if (static_cast<juce::FileOutputStream*> (stream.get())->failedToOpen())
            {
                errorOut = TRANS ("The file could not be created. Check that the folder is writable.");
                return Result::failed;
            }

            auto writer = format->createWriterFor (stream, options);
            if (writer == nullptr)
            {
                stream = nullptr;
                temp.deleteFile();
                errorOut = TRANS ("This format cannot store audio with these settings.");
                return Result::failed;
            }

            juce::AudioBuffer<float> buffer (channels, chunkFrames);
            const auto total = audio.getNumFrames();

            for (juce::int64 pos = 0; pos < total; pos += chunkFrames)
            {
                if (shouldContinue != nullptr && ! shouldContinue())
                {
                    result = Result::cancelled;
                    break;
                }

                const auto count = (int) juce::jmin ((juce::int64) chunkFrames, total - pos);
                if (! audio.read (buffer.getArrayOfWritePointers(), channels, pos, count))
                {
                    errorOut = TRANS ("Reading the audio failed.");
                    result = Result::failed;
                    break;
                }

                if (! writer->writeFromFloatArrays (buffer.getArrayOfReadPointers(), channels, count))
                {
                    errorOut = TRANS ("Writing the file failed. Is the disk full?");
                    result = Result::failed;
                    break;
                }

                if (progress != nullptr)
                    progress->store ((double) (pos + count) / (double) total);
            }

            // The writer finishes the file (headers, last frames) when it is deleted here.
        }

        if (result != Result::ok)
        {
            temp.deleteFile();
            return result;
        }

        if (temp.getSize() <= 0)
        {
            temp.deleteFile();
            errorOut = TRANS ("Writing the file failed. Is the disk full?");
            return Result::failed;
        }

        if (! temp.moveFileTo (target))
        {
            temp.deleteFile();
            errorOut = TRANS ("The file could not be replaced. Is it open in another program?");
            return Result::failed;
        }

        if (progress != nullptr)
            progress->store (1.0);

        return Result::ok;
    }
}
