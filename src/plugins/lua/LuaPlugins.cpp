// SPDX-License-Identifier: AGPL-3.0-only
#include "plugins/lua/LuaPlugins.h"
#include "dsp/effects/EffectCatalogue.h"

namespace mxj::plugins
{
    struct LuaPluginManager::Console
    {
        mutable std::mutex lock;
        juce::StringArray lines;
        std::function<void()> changed;

        void add (const juce::String& line)
        {
            {
                const std::lock_guard<std::mutex> l (lock);
                lines.add (juce::Time::getCurrentTime().toString (false, true, true, true) + "  " + line);
                if (lines.size() > 500)
                    lines.removeRange (0, lines.size() - 500);
            }

            if (changed != nullptr)
                changed();
        }
    };

    namespace
    {
        constexpr double secondsPerBlock = 30.0; ///< an effect call longer than this is stopped
        constexpr double secondsForTool = 10.0;

        juce::Time newestFileTime (const juce::File& folder)
        {
            auto newest = folder.getChildFile ("plugin.json").getLastModificationTime();
            for (const auto& entry : juce::RangedDirectoryIterator (folder, true, "*.lua"))
                newest = juce::jmax (newest, entry.getModificationTime());

            return newest;
        }

        /** The processor of an effect or generator plugin: a Lua state per processor. */
        class LuaEffectProcessor final : public dsp::EffectProcessor
        {
        public:
            LuaEffectProcessor (dsp::ParameterValuesPtr v, LuaPluginInfo i, juce::String s,
                                std::shared_ptr<std::function<void (const juce::String&)>> logTo)
                : EffectProcessor (std::move (v)), info (std::move (i)), source (std::move (s)), log (std::move (logTo))
            {
            }

            void prepare (double sampleRate, int, int numChannels) override
            {
                rate = sampleRate;
                position = 0;
                error.clear();

                LuaHost host;
                host.log = [l = log, name = info.name] (const juce::String& text) { if (*l != nullptr) (*l) (name + ": " + text); };
                script = std::make_unique<LuaScript> (info, host);
                script->setTimeLimit (secondsPerBlock);

                if (script->load (source, error) && script->callPrepare (sampleRate, numChannels, params->getAll(), error))
                    return;

                fail();
            }

            void reset() override {}

            void process (float* const* channels, int numChannels, int numFrames) override
            {
                if (script == nullptr || error.isNotEmpty())
                    return;

                LuaScript::Block block { channels, numChannels, numFrames, rate, position, -1 };
                const auto* function = info.type == LuaPluginInfo::Type::generator ? "generate" : "process";

                if (! script->callWithBlock (function, block, params->getAll(), error))
                    fail();

                position += numFrames;
            }

            juce::String getError() const override { return error; }

        private:
            void fail()
            {
                if (*log != nullptr)
                    (*log) (info.name + ": " + error);

                error = error.upToFirstOccurrenceOf ("\n", false, false); // the traceback is in the console
            }

            const LuaPluginInfo info;
            const juce::String source;
            std::shared_ptr<std::function<void (const juce::String&)>> log;
            std::unique_ptr<LuaScript> script;
            juce::String error;
            double rate = 48000.0;
            juce::int64 position = 0;
        };
    }

    //==============================================================================
    LuaPluginManager::LuaPluginManager (juce::File installFolder)
        : folder (std::move (installFolder)), console (std::make_shared<Console>())
    {
        console->changed = [this] { if (onConsoleChanged != nullptr) onConsoleChanged(); };
    }

    LuaPluginManager::~LuaPluginManager()
    {
        console->changed = nullptr;
        dsp::catalogue::removeAllWithPrefix ("lua:");
    }

    LuaPluginManager::Plugin LuaPluginManager::readPlugin (const juce::File& pluginFolder, bool developer) const
    {
        Plugin p;
        p.folder = pluginFolder;
        p.developer = developer;
        p.changed = newestFileTime (pluginFolder);

        if (! LuaPluginInfo::readFolder (pluginFolder, p.info, p.error))
        {
            if (p.info.name.isEmpty())
                p.info.name = pluginFolder.getFileName();
            return p;
        }

        p.source = pluginFolder.getChildFile (p.info.mainFile).loadFileAsString();
        p.enabled = ! disabled.contains (p.info.id);

        // Check that the script runs, and has the function its type needs.
        LuaScript script (p.info, {});
        script.setTimeLimit (5.0);
        if (! script.load (p.source, p.error))
            return p;

        const char* needed = nullptr;
        switch (p.info.type)
        {
            case LuaPluginInfo::Type::effect:    needed = "process"; break;
            case LuaPluginInfo::Type::generator: needed = "generate"; break;
            case LuaPluginInfo::Type::analysis:  needed = "analyse"; break;
            case LuaPluginInfo::Type::tool:      needed = "run"; break;
        }

        if (! script.hasFunction (needed))
            p.error = "main.lua has no function " + juce::String (needed) + "()";

        return p;
    }

    void LuaPluginManager::reload()
    {
        plugins.clear();
        juce::StringArray ids;

        const auto add = [this, &ids] (Plugin p)
        {
            if (p.info.id.isNotEmpty() && ids.contains (p.info.id))
            {
                log (p.folder.getFullPathName() + ": a plugin with the id " + p.info.id + " is already loaded");
                return;
            }

            if (p.error.isNotEmpty())
                log (p.info.name + ": " + p.error);

            ids.add (p.info.id);
            plugins.push_back (std::move (p));
        };

        // Developer folders first: they win over an installed copy of the same plugin.
        for (const auto& path : developerFolders)
            add (readPlugin (juce::File (path), true));

        for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*", juce::File::findDirectories))
            add (readPlugin (entry.getFile(), false));

        std::sort (plugins.begin(), plugins.end(), [] (const Plugin& a, const Plugin& b)
                   { return a.info.name.compareIgnoreCase (b.info.name) < 0; });

        registerEffects();

        if (onPluginsChanged != nullptr)
            onPluginsChanged();
    }

    void LuaPluginManager::registerEffects()
    {
        dsp::catalogue::removeAllWithPrefix ("lua:");

        auto logTo = std::make_shared<std::function<void (const juce::String&)>> (
            [weak = std::weak_ptr<Console> (console)] (const juce::String& line)
            {
                if (auto c = weak.lock())
                    c->add (line);
            });

        for (const auto& p : plugins)
        {
            if (! p.enabled || p.error.isNotEmpty()
                || (p.info.type != LuaPluginInfo::Type::effect && p.info.type != LuaPluginInfo::Type::generator))
                continue;

            dsp::EffectType type;
            type.id = "lua:" + p.info.id;
            type.name = p.info.name;
            type.category = menuGroup;
            type.source = "MXJ plugin";
            type.description = p.info.description.isNotEmpty() ? p.info.description : p.info.name;
            type.kind = p.info.type == LuaPluginInfo::Type::generator ? dsp::EffectType::Kind::generator
                                                                       : dsp::EffectType::Kind::script;
            type.parameters = p.info.parameters;
            type.create = [info = p.info, source = p.source, logTo] (dsp::ParameterValuesPtr values) -> std::unique_ptr<dsp::EffectProcessor>
            {
                return std::make_unique<LuaEffectProcessor> (std::move (values), info, source, logTo);
            };

            dsp::catalogue::add (std::move (type));
        }
    }

    std::vector<LuaPluginManager::Plugin> LuaPluginManager::getPlugins() const
    {
        return plugins;
    }

    const LuaPluginManager::Plugin* LuaPluginManager::find (const juce::String& id) const
    {
        for (const auto& p : plugins)
            if (p.info.id == id)
                return &p;

        return nullptr;
    }

    void LuaPluginManager::setDisabled (const juce::StringArray& ids)
    {
        disabled = ids;
        for (auto& p : plugins)
            p.enabled = ! disabled.contains (p.info.id);
    }

    void LuaPluginManager::setEnabled (const juce::String& id, bool shouldBeOn)
    {
        if (shouldBeOn)
            disabled.removeString (id);
        else
            disabled.addIfNotAlreadyThere (id);

        setDisabled (disabled);
        registerEffects();

        if (onPluginsChanged != nullptr)
            onPluginsChanged();
    }

    void LuaPluginManager::setDeveloperFolders (const juce::StringArray& folders)
    {
        developerFolders = folders;
    }

    bool LuaPluginManager::reloadChangedDeveloperPlugins()
    {
        for (const auto& p : plugins)
            if (p.developer && newestFileTime (p.folder) != p.changed)
            {
                log (p.info.name + ": files changed, reloading");
                reload();
                return true;
            }

        return false;
    }

    //==============================================================================
    namespace
    {
        /** The folder of an unpacked package that holds plugin.json (the root or its only subfolder). */
        juce::File findPluginRoot (const juce::File& unpacked)
        {
            if (unpacked.getChildFile ("plugin.json").existsAsFile())
                return unpacked;

            const auto folders = unpacked.findChildFiles (juce::File::findDirectories, false);
            if (folders.size() == 1 && folders[0].getChildFile ("plugin.json").existsAsFile())
                return folders[0];

            return {};
        }

        bool unpack (const juce::File& package, const juce::File& into, juce::String& error)
        {
            juce::ZipFile zip (package);
            if (zip.getNumEntries() == 0)
            {
                error = "Not a plugin package (not a zip file): " + package.getFileName();
                return false;
            }

            // No absolute paths or ".." in the package.
            for (int i = 0; i < zip.getNumEntries(); ++i)
            {
                const auto name = zip.getEntry (i)->filename.replaceCharacter ('\\', '/');
                if (name.startsWith ("/") || name.contains ("../") || name.contains (":"))
                {
                    error = "The package holds a file outside its folder: " + name;
                    return false;
                }
            }

            const auto result = zip.uncompressTo (into, true);
            if (result.failed())
            {
                error = result.getErrorMessage();
                return false;
            }

            return true;
        }
    }

    bool LuaPluginManager::readPackage (const juce::File& package, LuaPluginInfo& info, juce::String& error)
    {
        juce::TemporaryFile temp;
        const auto dir = temp.getFile();
        dir.createDirectory();

        const auto ok = unpack (package, dir, error)
                     && [&]
                        {
                            const auto root = findPluginRoot (dir);
                            if (root == juce::File())
                            {
                                error = "The package has no plugin.json";
                                return false;
                            }

                            return LuaPluginInfo::readFolder (root, info, error);
                        }();

        dir.deleteRecursively();
        return ok;
    }

    bool LuaPluginManager::install (const juce::File& package, juce::String& error)
    {
        juce::TemporaryFile temp;
        const auto dir = temp.getFile();
        dir.createDirectory();

        LuaPluginInfo info;
        auto ok = unpack (package, dir, error);
        auto root = ok ? findPluginRoot (dir) : juce::File();

        if (ok && root == juce::File())
        {
            error = "The package has no plugin.json";
            ok = false;
        }

        if (ok)
            ok = LuaPluginInfo::readFolder (root, info, error);

        if (ok)
        {
            const auto target = folder.getChildFile (info.id);
            folder.createDirectory();
            target.deleteRecursively();

            if (! root.copyDirectoryTo (target))
            {
                error = "Could not copy the plugin to " + target.getFullPathName();
                ok = false;
            }
            else
            {
                log (info.name + " " + info.version + " installed");
            }
        }

        dir.deleteRecursively();

        if (ok)
            reload();

        return ok;
    }

    bool LuaPluginManager::uninstall (const juce::String& id)
    {
        const auto* p = find (id);
        if (p == nullptr)
            return false;

        if (p->developer)
        {
            developerFolders.removeString (p->folder.getFullPathName());
        }
        else if (! p->folder.isAChildOf (folder) || ! p->folder.deleteRecursively())
        {
            return false;
        }

        log (p->info.name + " removed");
        reload();
        return true;
    }

    //==============================================================================
    bool LuaPluginManager::runAnalysis (const Plugin& plugin, const AudioSequence& audio, const std::vector<float>& values,
                                        const dsp::WorkControl& control, juce::String& report, juce::String& error) const
    {
        const auto* p = &plugin;
        if (p->info.type != LuaPluginInfo::Type::analysis || p->error.isNotEmpty())
        {
            error = "The plugin is not available";
            return false;
        }

        LuaHost host;
        host.log = [c = console, name = p->info.name] (const juce::String& t) { c->add (name + ": " + t); };
        host.shouldStop = [&control] { return control.cancelled(); };

        LuaScript script (p->info, host);
        script.setTimeLimit (secondsPerBlock);

        const auto channels = audio.getNumChannels();
        const auto length = audio.getNumFrames();
        const auto fail = [&]
        {
            console->add (p->info.name + ": " + error);
            error = error.upToFirstOccurrenceOf ("\n", false, false);
            return false;
        };

        if (! script.load (p->source, error) || ! script.callPrepare (audio.getSampleRate(), channels, values, error))
            return fail();

        constexpr int chunk = 1 << 14;
        juce::AudioBuffer<float> buffer (juce::jmax (1, channels), chunk);

        for (juce::int64 pos = 0; pos < length; pos += chunk)
        {
            if (control.cancelled())
                return false;

            const auto n = (int) juce::jmin ((juce::int64) chunk, length - pos);
            audio.read (buffer.getArrayOfWritePointers(), channels, pos, n);

            LuaScript::Block block { buffer.getArrayOfWritePointers(), channels, n, audio.getSampleRate(), pos, length };
            if (! script.callWithBlock ("analyse", block, values, error))
                return fail();

            control.report ((double) (pos + n) / (double) juce::jmax ((juce::int64) 1, length));
        }

        if (script.hasFunction ("finish") && ! script.call ("finish", values, report, error))
            return fail();

        return true;
    }

    bool LuaPluginManager::runTool (const juce::String& id, const std::vector<float>& values,
                                    std::function<bool (const juce::String&)> runCommand,
                                    juce::String& messages, juce::String& error) const
    {
        const auto* p = find (id);
        if (p == nullptr || p->info.type != LuaPluginInfo::Type::tool || p->error.isNotEmpty())
        {
            error = "The plugin is not available";
            return false;
        }

        LuaHost host;
        host.log = [c = console, name = p->info.name] (const juce::String& t) { c->add (name + ": " + t); };
        host.message = [&messages] (const juce::String& t) { messages << (messages.isEmpty() ? "" : "\n") << t; };
        host.runCommand = [c = console, name = p->info.name, runCommand] (const juce::String& command)
        {
            const auto ok = runCommand != nullptr && runCommand (command);
            c->add (name + ": command " + command + (ok ? " ran" : " could not run"));
            return ok;
        };

        LuaScript script (p->info, host);
        script.setTimeLimit (secondsForTool);

        juce::String result;
        if (! script.load (p->source, error) || ! script.call ("run", values, result, error))
        {
            console->add (p->info.name + ": " + error);
            error = error.upToFirstOccurrenceOf ("\n", false, false);
            return false;
        }

        if (result.isNotEmpty())
            messages << (messages.isEmpty() ? "" : "\n") << result;

        return true;
    }

    //==============================================================================
    void LuaPluginManager::log (const juce::String& line) const
    {
        console->add (line);
    }

    juce::StringArray LuaPluginManager::getConsole() const
    {
        const std::lock_guard<std::mutex> l (console->lock);
        return console->lines;
    }

    void LuaPluginManager::clearConsole()
    {
        {
            const std::lock_guard<std::mutex> l (console->lock);
            console->lines.clear();
        }

        if (onConsoleChanged != nullptr)
            onConsoleChanged();
    }
}
