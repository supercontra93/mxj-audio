// SPDX-License-Identifier: AGPL-3.0-only
#include "app/document/AudioDocument.h"

namespace mxj
{
    std::unique_ptr<AudioDocument> AudioDocument::open (const juce::File& file, BlockStore& store,
                                                        const juce::File& peakCacheFolder, juce::String& errorOut)
    {
        auto importer = AudioImporter::create (file, store, peakCacheFolder, errorOut);
        if (importer == nullptr)
            return nullptr;

        std::unique_ptr<AudioDocument> doc (new AudioDocument (std::move (importer)));
        doc->startThread (Priority::normal);
        return doc;
    }

    std::unique_ptr<AudioDocument> AudioDocument::createEmpty (const juce::String& name, int numChannels, double sampleRate)
    {
        std::unique_ptr<AudioDocument> doc (new AudioDocument());
        doc->name = name;
        doc->info.numChannels = numChannels;
        doc->info.sampleRate = sampleRate;
        doc->audioState.audio = AudioSequence (numChannels, sampleRate);
        doc->state = State::ready;
        return doc;
    }

    std::unique_ptr<AudioDocument> AudioDocument::createRestored (const juce::String& name, const AudioFileInfo& fileInfo,
                                                                  AudioState restored)
    {
        std::unique_ptr<AudioDocument> doc (new AudioDocument());
        doc->name = name;
        doc->info = fileInfo;
        doc->info.numChannels = restored.audio.getNumChannels();
        doc->info.sampleRate = restored.audio.getSampleRate();
        doc->info.numFrames = restored.audio.getNumFrames();
        doc->audioState = std::move (restored);
        doc->state = State::ready;
        doc->savedRevision = -1;
        return doc;
    }

    AudioDocument::AudioDocument() : Thread ("MXJ file loader") {}

    AudioDocument::AudioDocument (std::unique_ptr<AudioImporter> i)
        : Thread ("MXJ file loader"), importer (std::move (i)),
          startTime (juce::Time::getMillisecondCounter())
    {
        info = importer->getInfo();
    }

    AudioDocument::~AudioDocument()
    {
        signalThreadShouldExit();
        stopThread (10000);
    }

    juce::String AudioDocument::getDisplayName() const
    {
        return hasFile() ? info.file.getFileName() : name;
    }

    void AudioDocument::setAudioState (AudioState newState, juce::int64 newRevision)
    {
        audioState = std::move (newState);
        revision = newRevision;
    }

    void AudioDocument::markSaved (const AudioFileInfo& savedAs, juce::int64 savedAtRevision)
    {
        const auto channels = info.numChannels;
        const auto rate = info.sampleRate;
        info = savedAs;
        info.numChannels = channels;
        info.sampleRate = rate;
        info.numFrames = audioState.audio.getNumFrames();
        savedRevision = savedAtRevision;
    }

    void AudioDocument::run()
    {
        const auto result = importer->run ([this] { return ! threadShouldExit(); });

        if (result == AudioImporter::Result::cancelled)
            return;

        juce::MessageManager::callAsync ([weak = juce::WeakReference<AudioDocument> (this), result]
        {
            if (weak != nullptr)
                weak->finishLoading (result);
        });
    }

    void AudioDocument::finishLoading (AudioImporter::Result result)
    {
        loadSeconds = (juce::Time::getMillisecondCounter() - startTime) / 1000.0;

        if (result == AudioImporter::Result::ok)
        {
            audioState.audio = importer->getSequence();
            audioState.markers = info.markers;
            info.numFrames = audioState.audio.getNumFrames();
            state = State::ready;
        }
        else
        {
            error = importer->getError();
            state = State::failed;
        }

        if (onLoadFinished != nullptr)
            onLoadFinished();
    }
}
