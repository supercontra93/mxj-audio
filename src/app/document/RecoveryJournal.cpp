// SPDX-License-Identifier: AGPL-3.0-only
#include "app/document/RecoveryJournal.h"
#include "core/audio/SequenceSerializer.h"
#include "core/edit/EditOperations.h"
#include "dsp/convert/Resample.h"
#include "formats/import/FileSampleSource.h"

#include <map>

namespace mxj::recovery
{
    namespace
    {
        constexpr int journalVersion = 1;

        juce::var fileInfoToVar (const AudioFileInfo& info)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("path", info.file.getFullPathName());
            o->setProperty ("format", info.formatName);
            o->setProperty ("bits", info.bitsPerSample);
            o->setProperty ("float", info.usesFloat);
            o->setProperty ("uncompressed", info.isUncompressed);
            return juce::var (o);
        }

        AudioFileInfo fileInfoFromVar (const juce::var& v)
        {
            AudioFileInfo info;
            if (v["path"].toString().isNotEmpty())
                info.file = juce::File (v["path"].toString());
            info.formatName = v["format"].toString();
            info.bitsPerSample = (int) v["bits"];
            info.usesFloat = (bool) v["float"];
            info.isUncompressed = (bool) v["uncompressed"];
            return info;
        }

        /** Reads a whole source once and attaches its peaks. */
        bool computePeaks (SampleSource& source, BackgroundJob& job)
        {
            const auto channels = source.getNumChannels();
            const auto frames = source.getNumFrames();
            auto peaks = std::make_shared<PeakCache> (channels, frames);
            juce::AudioBuffer<float> buffer (channels, 1 << 16);

            for (juce::int64 pos = 0; pos < frames; pos += buffer.getNumSamples())
            {
                if (! job.shouldContinue())
                    return false;

                const auto count = (int) juce::jmin ((juce::int64) buffer.getNumSamples(), frames - pos);
                source.read (buffer.getArrayOfWritePointers(), channels, pos, count);
                peaks->append (buffer.getArrayOfReadPointers(), count);
            }

            if (frames == 0)
                peaks->finishEarly();

            source.setPeaks (peaks);
            return true;
        }
    }

    bool write (const juce::File& journalFile, const AudioDocument& doc, const TakeInfo* take)
    {
        SequenceWriter writer;
        const auto audio = writer.write (doc.getAudio());
        if (audio.isVoid())
            return false;

        juce::Array<juce::var> markers;
        for (const auto& m : doc.getMarkers())
            markers.add (juce::Array<juce::var> { m.position, m.name });

        auto* root = new juce::DynamicObject();
        root->setProperty ("version", journalVersion);
        root->setProperty ("name", doc.getDisplayName());
        root->setProperty ("channels", doc.getNumChannels());
        root->setProperty ("rate", doc.getSampleRate());
        root->setProperty ("file", fileInfoToVar (doc.getInfo()));
        root->setProperty ("sources", writer.getSources());
        root->setProperty ("audio", audio);
        root->setProperty ("markers", markers);

        if (take != nullptr)
        {
            juce::Array<juce::var> files;
            for (const auto& f : take->files)
                files.add (f.getFullPathName());

            auto* t = new juce::DynamicObject();
            t->setProperty ("files", files);
            t->setProperty ("channels", take->numChannels);
            t->setProperty ("rate", take->sampleRate);
            t->setProperty ("start", take->insertAt.getStart());
            t->setProperty ("end", take->insertAt.getEnd());
            root->setProperty ("take", juce::var (t));
        }

        const auto temp = journalFile.getSiblingFile (journalFile.getFileName() + ".tmp");
        if (! temp.replaceWithText (juce::JSON::toString (juce::var (root), true)))
            return false;

        return temp.moveFileTo (journalFile);
    }

    juce::String readName (const juce::File& runFolder)
    {
        const auto json = juce::JSON::parse (runFolder.getChildFile (BlockStore::journalFileName));
        return json["name"].toString();
    }

    bool restore (const juce::File& runFolder, BlockStore& store, Restored& out, BackgroundJob& job)
    {
        const auto json = juce::JSON::parse (runFolder.getChildFile (BlockStore::journalFileName));
        if ((int) json["version"] != journalVersion)
        {
            job.error = TRANS ("The recovery information is damaged.");
            return false;
        }

        // Leaf sources by path, so each file becomes exactly one source object.
        std::map<juce::String, SampleSourcePtr> leaves;
        std::vector<std::shared_ptr<SampleSource>> needPeaks;

        auto factory = [&] (const juce::var& d, juce::String& error) -> SampleSourcePtr
        {
            const auto type = d["type"].toString();
            const juce::File file (d["path"].toString());
            const auto key = type + "|" + file.getFullPathName();

            if (const auto it = leaves.find (key); it != leaves.end())
                return it->second;

            std::shared_ptr<SampleSource> source;

            if (type == "block")
            {
                source = store.adopt (file, (int) d["channels"], (double) d["rate"], (juce::int64) d["frames"]);
                if (source == nullptr)
                    error = TRANS ("Some of the temporary audio is missing.");
            }
            else if (type == "file")
            {
                auto path = file;

                // A copy kept while saving over the original lives in the old run folder.
                if (file.isAChildOf (runFolder))
                {
                    path = store.createFileName (file.getFileExtension());
                    if (! file.moveFileTo (path))
                        path = juce::File();
                }
                else if (file.getSize() != (juce::int64) d["size"]
                         || file.getLastModificationTime().toMilliseconds() != (juce::int64) d["modified"])
                {
                    error = TRANS ("The original file has been changed or removed since:") + "\n" + file.getFullPathName();
                    return nullptr;
                }

                juce::String readerError;
                auto reader = path != juce::File() ? audioFormats::createReader (path, readerError) : nullptr;
                if (reader == nullptr)
                    error = TRANS ("The original file has been changed or removed since:") + "\n" + file.getFullPathName();
                else
                    source = std::make_shared<FileSampleSource> (path, std::move (reader));
            }

            if (source == nullptr)
                return nullptr;

            needPeaks.push_back (source);
            leaves[key] = source;
            return source;
        };

        SequenceReader reader (json["sources"], factory);
        auto audio = reader.read (json["audio"]);
        if (reader.failed())
        {
            job.error = reader.getError();
            return false;
        }

        if (audio.getNumChannels() == 0)
            audio = AudioSequence ((int) json["channels"], (double) json["rate"]);

        AudioState state { audio, {} };
        if (const auto* markers = json["markers"].getArray())
            for (const auto& m : *markers)
                edits::addMarker (state.markers, { (juce::int64) m[0], m[1].toString() });

        // A take that was being recorded when the program stopped.
        if (const auto& take = json["take"]; take.isObject())
        {
            const auto channels = (int) take["channels"];
            const auto rate = (double) take["rate"];
            AudioSequence takeAudio (channels, rate);

            if (const auto* files = take["files"].getArray())
            {
                for (const auto& f : *files)
                {
                    const juce::File file (f.toString());
                    const auto frames = channels > 0 ? file.getSize() / (channels * (juce::int64) sizeof (float)) : 0;
                    if (frames <= 0)
                        continue;

                    if (auto source = store.adopt (file, channels, rate, frames))
                    {
                        needPeaks.push_back (source);
                        takeAudio.append (AudioSequence::fromSource (source));
                    }
                }
            }

            if (! takeAudio.isEmpty())
            {
                for (auto& s : needPeaks)
                    if (s->getPeaks() == nullptr && ! computePeaks (*s, job))
                        return false;

                if (! juce::approximatelyEqual (rate, state.audio.getSampleRate()))
                    takeAudio = dsp::resample (takeAudio, state.audio.getSampleRate(), store, [&job] { return job.shouldContinue(); });

                const FrameRange range { (juce::int64) take["start"], (juce::int64) take["end"] };
                state = edits::paste (state, range, takeAudio).state;
            }
        }

        for (size_t i = 0; i < needPeaks.size(); ++i)
        {
            if (needPeaks[i]->getPeaks() == nullptr && ! computePeaks (*needPeaks[i], job))
                return false;

            job.progress = (double) (i + 1) / (double) needPeaks.size();
        }

        out.name = json["name"].toString();
        out.info = fileInfoFromVar (json["file"]);
        out.state = std::move (state);
        return true;
    }
}
