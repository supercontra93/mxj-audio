// SPDX-License-Identifier: AGPL-3.0-only
#include "formats/import/AudioFileFormats.h"

#include <mutex>

namespace mxj::audioFormats
{
    juce::AudioFormatManager& getManager()
    {
        static juce::AudioFormatManager manager;
        static std::once_flag registered;
        std::call_once (registered, [] { manager.registerBasicFormats(); });
        return manager;
    }

    juce::String getWildcard()
    {
        return getManager().getWildcardForAllFormats();
    }

    bool canOpen (const juce::File& file)
    {
        return getManager().findFormatForFileExtension (file.getFileExtension()) != nullptr;
    }

    std::unique_ptr<juce::AudioFormatReader> createReader (const juce::File& file, juce::String& errorOut)
    {
        if (! file.existsAsFile())
        {
            errorOut = TRANS ("The file does not exist.");
            return nullptr;
        }

        if (! canOpen (file))
        {
            errorOut = TRANS ("This file type is not supported.");
            return nullptr;
        }

        std::unique_ptr<juce::AudioFormatReader> reader (getManager().createReaderFor (file));

        if (reader == nullptr)
        {
            errorOut = TRANS ("The file could not be read. It may be damaged or in an unsupported variant of its format.");
            return nullptr;
        }

        if (reader->numChannels == 0 || reader->sampleRate <= 0 || reader->lengthInSamples < 0)
        {
            errorOut = TRANS ("The file contains no usable audio.");
            return nullptr;
        }

        return reader;
    }

    bool readInfo (const juce::File& file, AudioFileInfo& info, juce::String& errorOut)
    {
        const auto reader = createReader (file, errorOut);
        if (reader == nullptr)
            return false;

        info.file = file;
        info.formatName = reader->getFormatName();
        info.numChannels = (int) reader->numChannels;
        info.sampleRate = reader->sampleRate;
        info.numFrames = reader->lengthInSamples;
        info.bitsPerSample = (int) reader->bitsPerSample;
        info.isUncompressed = dynamic_cast<juce::WavAudioFormat*> (getManager().findFormatForFileExtension (file.getFileExtension())) != nullptr
                           || dynamic_cast<juce::AiffAudioFormat*> (getManager().findFormatForFileExtension (file.getFileExtension())) != nullptr;
        return true;
    }
}
