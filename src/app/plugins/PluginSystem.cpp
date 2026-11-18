// SPDX-License-Identifier: AGPL-3.0-only
#include "app/plugins/PluginSystem.h"
#include "plugins/clap/ClapFormat.h"
#include "services/logging/Log.h"
#include "services/settings/SettingKeys.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace mxj
{
    namespace
    {
        juce::File programFolder()
        {
            return juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
        }

        juce::File scannerProgram()
        {
           #if JUCE_WINDOWS
            return programFolder().getChildFile ("mxj-plugin-scanner.exe");
           #else
            return programFolder().getChildFile ("mxj-plugin-scanner");
           #endif
        }

        juce::StringArray lines (const juce::String& text)
        {
            auto result = juce::StringArray::fromLines (text);
            result.trim();
            result.removeEmptyStrings();
            return result;
        }
    }

    PluginSystem::PluginSystem (AppContext& ctx)
        : context (ctx), lua (ctx.paths.getPluginsFolder().getChildFile ("lua"))
    {
       #if JUCE_PLUGINHOST_VST3
        host.addFormat (std::make_unique<juce::VST3PluginFormat>());
       #endif
        host.addFormat (std::make_unique<plugins::ClapPluginFormat>());

        host.load (ctx.paths.getPluginsFolder().getChildFile ("known-plugins.xml"));
        host.registerEffects();
        host.onListChanged = [this] { host.registerEffects(); listsChanged(); };

        scanner = std::make_unique<plugins::PluginScanner> (host, scannerProgram());
        scanner->onFinished = [this] (int found, int blocked)
        {
            Log::info ("Plugin scan finished: " + juce::String (found) + " plugins, " + juce::String (blocked) + " blocked");
            if (onScanFinished != nullptr)
                onScanFinished (found, blocked);
        };

        lua.setDisabled (lines (ctx.settings.getString (keys::pluginsDisabled)));
        lua.setDeveloperFolders (lines (ctx.settings.getString (keys::pluginsDevFolders)));
        lua.reload();
        lua.onPluginsChanged = [this] { listsChanged(); };
        lua.onConsoleChanged = [this]
        {
            // Any thread: tell the page on the message thread.
            juce::MessageManager::callAsync ([this, token = alive]
            {
                if (*token)
                    sendChangeMessage();
            });
        };

        // The local test catalogue next to the program (the online one comes with phase 8).
        if (! catalogue.load (programFolder().getChildFile ("catalog").getChildFile ("index.json"), catalogueError))
            Log::info ("Plugin catalogue: " + catalogueError);

        Log::info ("Plugins: " + juce::String ((int) host.getPlugins().size()) + " VST3/CLAP known, "
                   + juce::String ((int) lua.getPlugins().size()) + " MXJ plugins, catalogue with "
                   + juce::String ((int) catalogue.getEntries().size()) + " entries");

        // Developer plugins are reloaded when their files change.
        startTimer (1000);
    }

    PluginSystem::~PluginSystem()
    {
        stopTimer();
        scanner->cancel();
        scanner = nullptr;
        host.onListChanged = nullptr;
        lua.onPluginsChanged = nullptr;
        lua.onConsoleChanged = nullptr;
        *alive = false;
    }

    void PluginSystem::listsChanged()
    {
        if (onPluginsChanged != nullptr)
            onPluginsChanged();

        sendChangeMessage();
    }

    void PluginSystem::timerCallback()
    {
        if (! lua.getDeveloperFolders().isEmpty() && lua.reloadChangedDeveloperPlugins())
            Log::info ("Developer plugins reloaded");
    }

    bool PluginSystem::installFromCatalogue (const juce::String& id, juce::String& error)
    {
        const auto* entry = catalogue.find (id);
        if (entry == nullptr)
        {
            error = TRANS ("The plugin is not in the catalogue.");
            return false;
        }

        if (! entry->isCompatible())
        {
            error = TRANS ("This plugin needs a newer version of MXJ AUDIO.");
            return false;
        }

        const auto package = catalogue.getVerifiedPackage (*entry, error);
        if (package == juce::File())
        {
            error = TRANS (error);
            return false;
        }

        if (! lua.install (package, error))
        {
            error = TRANS (error);
            return false;
        }

        Log::info ("Plugin installed from the catalogue: " + id + " " + entry->version);
        return true;
    }

    juce::String PluginSystem::getInstalledVersion (const juce::String& id) const
    {
        if (const auto* p = lua.find (id); p != nullptr && ! p->developer)
            return p->info.version;

        return {};
    }

    void PluginSystem::setEnabled (const juce::String& luaId, bool shouldBeOn)
    {
        lua.setEnabled (luaId, shouldBeOn);
        storeSettings();
    }

    void PluginSystem::setDeveloperFolders (const juce::StringArray& folders)
    {
        lua.setDeveloperFolders (folders);
        lua.reload();
        storeSettings();
    }

    void PluginSystem::scan (bool everything)
    {
        if (scanner->isScanning())
            return;

        Log::info (everything ? "Plugin scan: all files" : "Plugin scan: new and changed files");
        scanner->start (everything);
        sendChangeMessage();
    }

    void PluginSystem::storeSettings()
    {
        context.settings.set (keys::pluginsDisabled, lua.getDisabled().joinIntoString ("\n"));
        context.settings.set (keys::pluginsDevFolders, lua.getDeveloperFolders().joinIntoString ("\n"));
    }
}
