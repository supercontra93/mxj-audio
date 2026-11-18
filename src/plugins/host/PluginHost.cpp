// SPDX-License-Identifier: AGPL-3.0-only
#include "plugins/host/PluginHost.h"
#include "dsp/effects/EffectCatalogue.h"

namespace mxj::plugins
{
    /** What the effect types keep alive: the formats and the live plugins by name. */
    struct PluginHost::Core
    {
        juce::AudioPluginFormatManager formats;
        std::mutex liveLock;
        std::map<juce::String, std::weak_ptr<HostedPlugin>> live;

        std::unique_ptr<juce::AudioPluginInstance> create (const juce::PluginDescription& d, double rate, int block,
                                                           juce::String& error)
        {
            return formats.createPluginInstance (d, rate, block, error);
        }

        std::shared_ptr<dsp::EffectState> makeState (const std::shared_ptr<Core>& self, const juce::PluginDescription& d,
                                                     const juce::String& instance, const juce::String& saved)
        {
            const std::weak_ptr<Core> weak = self;
            auto factory = [weak] (const juce::PluginDescription& desc, double rate, int block, juce::String& error)
                -> std::unique_ptr<juce::AudioPluginInstance>
            {
                if (auto c = weak.lock())
                    return c->create (desc, rate, block, error);

                error = "The program is closing";
                return nullptr;
            };

            if (instance.isEmpty())
                return std::make_shared<HostedPlugin> (d, factory, saved);

            std::shared_ptr<HostedPlugin> existing;
            {
                const std::lock_guard<std::mutex> l (liveLock);

                for (auto it = live.begin(); it != live.end();)
                    it = it->second.expired() ? live.erase (it) : std::next (it);

                existing = live[instance].lock();
                if (existing == nullptr)
                {
                    auto created = std::make_shared<HostedPlugin> (d, factory, saved);
                    live[instance] = created;
                    return created;
                }
            }

            existing->restore (saved); // nothing if it already has this state
            return existing;
        }
    };

    namespace
    {
        juce::String folderKey (const juce::String& format) { return format.toUpperCase(); }
    }

    //==============================================================================
    PluginHost::PluginHost() : core (std::make_shared<Core>()) {}

    PluginHost::~PluginHost()
    {
        // Effect types stay in the catalogue, but cannot load plugins any more.
        dsp::catalogue::removeAllWithPrefix ("vst3:");
        dsp::catalogue::removeAllWithPrefix ("clap:");
    }

    void PluginHost::addFormat (std::unique_ptr<juce::AudioPluginFormat> format)
    {
        core->formats.addFormat (std::move (format));
    }

    juce::AudioPluginFormat* PluginHost::findFormat (const juce::String& name) const
    {
        for (auto* f : core->formats.getFormats())
            if (f->getName() == name)
                return f;

        return nullptr;
    }

    juce::StringArray PluginHost::getFormatNames() const
    {
        juce::StringArray names;
        for (auto* f : core->formats.getFormats())
            names.add (f->getName());

        return names;
    }

    //==============================================================================
    bool PluginHost::load (const juce::File& file)
    {
        listFile = file;
        const auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr || ! xml->hasTagName ("MXJPLUGINS"))
            return false;

        const std::lock_guard<std::mutex> l (lock);
        plugins.clear();
        blocked.clear();
        scannedFiles.clear();
        userFolders.clear();

        if (auto* known = xml->getChildByName ("KNOWN"))
            for (auto* e : known->getChildIterator())
            {
                juce::PluginDescription d;
                if (d.loadFromXml (*e))
                    plugins.push_back (d);
            }

        if (auto* scanned = xml->getChildByName ("SCANNED"))
            for (auto* e : scanned->getChildIterator())
                scannedFiles[e->getStringAttribute ("path")] = juce::Time (e->getStringAttribute ("time").getLargeIntValue());

        if (auto* b = xml->getChildByName ("BLOCKED"))
            for (auto* e : b->getChildIterator())
                blocked.push_back ({ e->getStringAttribute ("format"), e->getStringAttribute ("path"), e->getStringAttribute ("reason") });

        if (auto* folders = xml->getChildByName ("FOLDERS"))
            for (auto* e : folders->getChildIterator())
                userFolders[folderKey (e->getStringAttribute ("format"))].add (e->getStringAttribute ("path"));

        return true;
    }

    bool PluginHost::save() const
    {
        if (listFile == juce::File())
            return false;

        juce::XmlElement root ("MXJPLUGINS");
        root.setAttribute ("version", 1);
        {
            const std::lock_guard<std::mutex> l (lock);

            auto* known = root.createNewChildElement ("KNOWN");
            for (const auto& d : plugins)
                known->addChildElement (d.createXml().release());

            auto* scanned = root.createNewChildElement ("SCANNED");
            for (const auto& [path, time] : scannedFiles)
            {
                auto* e = scanned->createNewChildElement ("FILE");
                e->setAttribute ("path", path);
                e->setAttribute ("time", juce::String (time.toMilliseconds()));
            }

            auto* b = root.createNewChildElement ("BLOCKED");
            for (const auto& x : blocked)
            {
                auto* e = b->createNewChildElement ("FILE");
                e->setAttribute ("format", x.format);
                e->setAttribute ("path", x.file);
                e->setAttribute ("reason", x.reason);
            }

            auto* folders = root.createNewChildElement ("FOLDERS");
            for (const auto& [format, list] : userFolders)
                for (const auto& path : list)
                {
                    auto* e = folders->createNewChildElement ("FOLDER");
                    e->setAttribute ("format", format);
                    e->setAttribute ("path", path);
                }
        }

        listFile.getParentDirectory().createDirectory();
        return root.writeTo (listFile);
    }

    std::vector<juce::PluginDescription> PluginHost::getPlugins() const
    {
        const std::lock_guard<std::mutex> l (lock);
        return plugins;
    }

    std::vector<PluginHost::BlockedFile> PluginHost::getBlocked() const
    {
        const std::lock_guard<std::mutex> l (lock);
        return blocked;
    }

    juce::StringArray PluginHost::getStandardFolders (const juce::String& format) const
    {
        juce::StringArray result;
        if (auto* f = findFormat (format))
        {
            const auto paths = f->getDefaultLocationsToSearch();
            for (int i = 0; i < paths.getNumPaths(); ++i)
                result.add (paths.getRawString (i));
        }

        return result;
    }

    juce::StringArray PluginHost::getUserFolders (const juce::String& format) const
    {
        const std::lock_guard<std::mutex> l (lock);
        const auto it = userFolders.find (folderKey (format));
        return it != userFolders.end() ? it->second : juce::StringArray();
    }

    void PluginHost::setUserFolders (const juce::String& format, const juce::StringArray& folders)
    {
        {
            const std::lock_guard<std::mutex> l (lock);
            userFolders[folderKey (format)] = folders;
        }

        save();
        if (onListChanged != nullptr)
            onListChanged();
    }

    void PluginHost::unblock (const juce::String& file)
    {
        {
            const std::lock_guard<std::mutex> l (lock);
            blocked.erase (std::remove_if (blocked.begin(), blocked.end(), [&] (const BlockedFile& b) { return b.file == file; }),
                           blocked.end());
            scannedFiles.erase (file);
        }

        save();
        if (onListChanged != nullptr)
            onListChanged();
    }

    void PluginHost::clearList()
    {
        {
            const std::lock_guard<std::mutex> l (lock);
            plugins.clear();
            scannedFiles.clear();
        }

        save();
    }

    std::vector<std::pair<juce::String, juce::String>> PluginHost::findFilesToScan (bool everything) const
    {
        std::vector<std::pair<juce::String, juce::String>> result;

        for (auto* format : core->formats.getFormats())
        {
            juce::FileSearchPath paths;
            for (const auto& folder : getStandardFolders (format->getName()))
                paths.addIfNotAlreadyThere (juce::File (folder));
            for (const auto& folder : getUserFolders (format->getName()))
                paths.addIfNotAlreadyThere (juce::File (folder));

            for (const auto& file : format->searchPathsForPlugins (paths, true, false))
            {
                const std::lock_guard<std::mutex> l (lock);
                const bool isBlocked = std::any_of (blocked.begin(), blocked.end(), [&] (const BlockedFile& b) { return b.file == file; });
                if (isBlocked)
                    continue;

                const auto it = scannedFiles.find (file);
                const bool unchanged = it != scannedFiles.end() && it->second == juce::File (file).getLastModificationTime();
                if (everything || ! unchanged)
                    result.emplace_back (format->getName(), file);
            }
        }

        return result;
    }

    void PluginHost::addScanResult (const juce::String& format, const juce::String& file,
                                    const juce::OwnedArray<juce::PluginDescription>& found)
    {
        const std::lock_guard<std::mutex> l (lock);
        plugins.erase (std::remove_if (plugins.begin(), plugins.end(), [&] (const juce::PluginDescription& d)
                       { return d.pluginFormatName == format && d.fileOrIdentifier == file; }),
                       plugins.end());

        for (const auto* d : found)
            plugins.push_back (*d);

        std::sort (plugins.begin(), plugins.end(), [] (const auto& a, const auto& b)
                   { return a.name.compareIgnoreCase (b.name) < 0; });

        scannedFiles[file] = juce::File (file).getLastModificationTime();
    }

    void PluginHost::addBlocked (const juce::String& format, const juce::String& file, const juce::String& reason)
    {
        const std::lock_guard<std::mutex> l (lock);
        blocked.erase (std::remove_if (blocked.begin(), blocked.end(), [&] (const BlockedFile& b) { return b.file == file; }),
                       blocked.end());
        blocked.push_back ({ format, file, reason });
    }

    void PluginHost::removeMissing()
    {
        const std::lock_guard<std::mutex> l (lock);
        plugins.erase (std::remove_if (plugins.begin(), plugins.end(), [] (const juce::PluginDescription& d)
                       { return ! juce::File (d.fileOrIdentifier).exists(); }),
                       plugins.end());
    }

    //==============================================================================
    juce::String PluginHost::effectIdFor (const juce::PluginDescription& d)
    {
        return d.pluginFormatName.toLowerCase() + ":" + juce::String::toHexString (d.uniqueId) + "-"
             + juce::String::toHexString ((d.manufacturerName + "/" + d.name).hashCode());
    }

    bool PluginHost::isEffect (const juce::PluginDescription& d)
    {
        // VST3 plugins listed from their module information carry no channel
        // counts (0 and 0): those count as effects unless they are instruments.
        if (d.isInstrument || d.category.containsIgnoreCase ("Instrument"))
            return false;

        return (d.numInputChannels > 0 && d.numOutputChannels > 0) || (d.numInputChannels == 0 && d.numOutputChannels == 0);
    }

    void PluginHost::registerEffects()
    {
        dsp::catalogue::removeAllWithPrefix ("vst3:");
        dsp::catalogue::removeAllWithPrefix ("clap:");

        for (const auto& d : getPlugins())
        {
            if (! isEffect (d) || findFormat (d.pluginFormatName) == nullptr)
                continue;

            dsp::EffectType type;
            type.id = effectIdFor (d);
            type.name = d.name;
            type.category = d.pluginFormatName;
            type.source = d.pluginFormatName;
            type.description = (d.manufacturerName.isNotEmpty() ? d.manufacturerName + " - " : juce::String())
                             + d.pluginFormatName + " " + d.version;
            type.kind = dsp::EffectType::Kind::realtime;

            std::weak_ptr<Core> weak = core;
            type.createState = [weak, d] (const juce::String& instance, const juce::String& saved) -> std::shared_ptr<dsp::EffectState>
            {
                if (auto c = weak.lock())
                    return c->makeState (c, d, instance, saved);

                return std::make_shared<HostedPlugin> (d, nullptr, saved);
            };

            type.create = [] (dsp::ParameterValuesPtr values) -> std::unique_ptr<dsp::EffectProcessor>
            {
                auto plugin = std::dynamic_pointer_cast<HostedPlugin> (values->state);
                return std::make_unique<HostedPluginProcessor> (std::move (values), std::move (plugin));
            };

            dsp::catalogue::add (std::move (type));
        }
    }

    std::unique_ptr<juce::AudioPluginInstance> PluginHost::createInstance (const juce::PluginDescription& d, double rate,
                                                                           int block, juce::String& error) const
    {
        return core->create (d, rate, block, error);
    }
}
