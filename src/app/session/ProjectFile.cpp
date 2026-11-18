// SPDX-License-Identifier: AGPL-3.0-only
#include "app/session/ProjectFile.h"
#include "core/Version.h"
#include "core/audio/DerivedSources.h"
#include "core/audio/SequenceSerializer.h"
#include "core/session/SessionFile.h"
#include "formats/import/AudioImporter.h"
#include "services/logging/Log.h"

#include <algorithm>

namespace mxj::projectFile
{
    namespace
    {
        juce::File peakFileFor (const juce::File& dataFile)
        {
            return dataFile.withFileExtension (".peaks");
        }

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

    juce::File getDataFolder (const juce::File& projectFile)
    {
        return projectFile.getSiblingFile (projectFile.getFileNameWithoutExtension() + "_data");
    }

    bool save (const Session& session, const juce::File& file, CopiedFiles& copied, BackgroundJob& job)
    {
        SequenceWriter writer;
        const auto sessionJson = sessionFile::toVar (session, writer);
        auto sources = writer.getSources();

        if (sessionJson.isVoid() || ! sources.isArray())
        {
            job.error = TRANS ("Some audio of the session cannot be saved.");
            return false;
        }

        const auto dataFolder = getDataFolder (file);
        const auto projectFolder = file.getParentDirectory();
        auto& table = *sources.getArray();

        for (int i = 0; i < table.size(); ++i)
        {
            if (! job.shouldContinue())
                return false;

            job.progress = 0.95 * (double) i / (double) juce::jmax (1, table.size());
            auto d = table.getReference (i);
            const auto type = d["type"].toString();

            if (type == "block")
            {
                const juce::File source (d["path"].toString());
                juce::File target;

                if (source.isAChildOf (dataFolder))
                {
                    target = source;
                }
                else
                {
                    const auto known = copied.find (source.getFullPathName());
                    if (known != copied.end() && known->second.isAChildOf (dataFolder) && known->second.getSize() == source.getSize())
                    {
                        target = known->second;
                    }
                    else
                    {
                        if (! dataFolder.createDirectory())
                        {
                            job.error = TRANS ("Could not create the folder for the project's audio:") + "\n" + dataFolder.getFullPathName();
                            return false;
                        }

                        target = dataFolder.getNonexistentChildFile ("audio", ".f32", false);
                        if (! source.copyFileTo (target))
                        {
                            job.error = TRANS ("Could not copy audio into the project folder. Is the disk full?");
                            return false;
                        }

                        copied[source.getFullPathName()] = target;
                    }
                }

                // A peak file next to it, so the project opens without reading all audio.
                const auto peakFile = peakFileFor (target);
                if (! peakFile.existsAsFile())
                {
                    BlockFileSource temp (target, (int) d["channels"], (double) d["rate"], (juce::int64) d["frames"], false);
                    if (computePeaks (temp, job))
                        temp.getPeaks()->save (peakFile);
                }

                auto* o = new juce::DynamicObject();
                o->setProperty ("type", "data");
                o->setProperty ("file", target.getRelativePathFrom (dataFolder).replaceCharacter ('\\', '/'));
                o->setProperty ("channels", d["channels"]);
                o->setProperty ("rate", d["rate"]);
                o->setProperty ("frames", d["frames"]);
                table.set (i, juce::var (o));
            }
            else if (type == "file")
            {
                const juce::File source (d["path"].toString());
                AudioFileInfo info;
                juce::String error;
                audioFormats::readInfo (source, info, error);

                auto* o = new juce::DynamicObject();
                o->setProperty ("type", "file");
                o->setProperty ("path", source.getFullPathName());
                o->setProperty ("relative", source.getRelativePathFrom (projectFolder).replaceCharacter ('\\', '/'));
                o->setProperty ("channels", info.numChannels);
                o->setProperty ("rate", info.sampleRate);
                o->setProperty ("frames", info.numFrames);
                table.set (i, juce::var (o));
            }
        }

        auto* root = new juce::DynamicObject();
        root->setProperty ("format", formatName);
        root->setProperty ("version", sessionFile::schemaVersion);
        root->setProperty ("app", juce::String (version::string));
        root->setProperty ("session", sessionJson);
        root->setProperty ("sources", sources);

        const auto temp = file.getSiblingFile (file.getFileName() + ".tmp");
        if (! temp.replaceWithText (juce::JSON::toString (juce::var (root), false)) || ! temp.moveFileTo (file))
        {
            temp.deleteFile();
            job.error = TRANS ("Could not write the project file:") + "\n" + file.getFullPathName();
            return false;
        }

        job.progress = 1.0;
        return true;
    }

    bool load (const juce::File& file, BlockStore& store, const juce::File& peakCacheFolder,
               Session& out, juce::StringArray& missingFiles, BackgroundJob& job)
    {
        const auto json = juce::JSON::parse (file);
        if (json["format"].toString() != formatName)
        {
            job.error = TRANS ("This is not an MXJ AUDIO project, or it is damaged.");
            return false;
        }

        const auto version = (int) json["version"];
        if (version > sessionFile::schemaVersion)
        {
            job.error = TRANS ("This project was saved by a newer version of MXJ AUDIO. Please update the program.");
            return false;
        }

        const auto dataFolder = getDataFolder (file);
        const auto projectFolder = file.getParentDirectory();
        const auto* table = json["sources"].getArray();
        const auto total = table != nullptr ? juce::jmax (1, table->size()) : 1;
        int done = 0;

        auto factory = [&] (const juce::var& d, juce::String& error) -> SampleSourcePtr
        {
            job.progress = (double) ++done / (double) total;
            if (! job.shouldContinue())
            {
                error = "cancelled";
                return nullptr;
            }

            const auto type = d["type"].toString();
            const auto channels = juce::jlimit (1, 64, (int) d["channels"]);
            const auto rate = (double) d["rate"] > 0 ? (double) d["rate"] : 48000.0;
            const auto frames = juce::jmax ((juce::int64) 0, (juce::int64) d["frames"]);
            const auto silence = [&] (const juce::String& name) -> SampleSourcePtr
            {
                missingFiles.addIfNotAlreadyThere (name);
                return std::make_shared<SilenceSource> (channels, rate, frames);
            };

            if (type == "data")
            {
                const auto dataFile = dataFolder.getChildFile (d["file"].toString());
                if (dataFile.getSize() != frames * channels * (juce::int64) sizeof (float))
                    return silence (dataFile.getFullPathName());

                auto source = std::make_shared<BlockFileSource> (dataFile, channels, rate, frames, false);
                if (auto peaks = PeakCache::load (peakFileFor (dataFile), channels, frames))
                    source->setPeaks (std::shared_ptr<const PeakCache> (std::move (peaks)));
                else if (! computePeaks (*source, job))
                    return nullptr;

                return source;
            }

            if (type == "file")
            {
                auto path = juce::File (d["path"].toString());
                if (! path.existsAsFile() && d["relative"].toString().isNotEmpty())
                    path = projectFolder.getChildFile (d["relative"].toString());

                juce::String importError;
                auto importer = path.existsAsFile() ? AudioImporter::create (path, store, peakCacheFolder, importError) : nullptr;
                if (importer == nullptr || importer->run ([&job] { return job.shouldContinue(); }) != AudioImporter::Result::ok
                    || importer->getSequence().getBlocks().empty())
                    return silence (juce::File (d["path"].toString()).getFullPathName());

                // WAV and AIFF are read in place as one source; a long file is split into
                // several blocks of that same source (AudioSequence::fromSource).
                const auto& seq = importer->getSequence();
                const auto& blocks = seq.getBlocks();
                const auto whole = blocks.front().source;
                const bool oneSource = blocks.front().sourceStart == 0 && whole->getNumFrames() == seq.getNumFrames()
                                    && std::all_of (blocks.begin(), blocks.end(), [&whole] (const auto& b) { return b.source == whole; });
                if (oneSource)
                    return whole;

                // Should not happen for WAV and AIFF; keep the project opening anyway.
                missingFiles.addIfNotAlreadyThere (path.getFullPathName());
                return std::make_shared<SilenceSource> (seq.getNumChannels(), seq.getSampleRate(), seq.getNumFrames());
            }

            error = "Unknown audio source type " + type;
            return nullptr;
        };

        SequenceReader reader (json["sources"], factory);
        if (reader.failed())
        {
            job.error = job.shouldContinue() ? TRANS ("The project file is damaged.") : juce::String();
            Log::warning ("Project " + file.getFullPathName() + ": " + reader.getError());
            return false;
        }

        juce::String error;
        if (! sessionFile::fromVar (json["session"], version, reader, out, error))
        {
            job.error = TRANS ("The project file is damaged.");
            Log::warning ("Project " + file.getFullPathName() + ": " + error);
            return false;
        }

        return true;
    }
}
