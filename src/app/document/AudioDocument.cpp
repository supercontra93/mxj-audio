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

    AudioDocument::AudioDocument (std::unique_ptr<AudioImporter> i)
        : Thread ("MXJ file loader"), importer (std::move (i)),
          startTime (juce::Time::getMillisecondCounter()) {}

    AudioDocument::~AudioDocument()
    {
        signalThreadShouldExit();
        stopThread (10000);
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
            sequence = importer->getSequence();
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
