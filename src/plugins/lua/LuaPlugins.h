// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/audio/AudioSequence.h"
#include "dsp/process/Render.h"
#include "plugins/lua/LuaScript.h"

#include <deque>
#include <mutex>

namespace mxj::plugins
{
    /** The MXJ (Lua) plugins (master plan, section 7): those installed in the
        plugins folder and those loaded from a developer folder.

        Effect and generator plugins become effect types in dsp::catalogue
        ("lua:<id>", category "MXJ Plugins"), applied like built-in effects.
        Analysis and tool plugins are run with runAnalysis() and runTool().

        Installed plugins live in `<plugins folder>/lua/<id>/`. A package
        (.mxjplugin) is a zip file with plugin.json and the Lua files.

        Message thread, except runAnalysis() and the effect processors.
    */
    class LuaPluginManager
    {
    public:
        struct Plugin
        {
            LuaPluginInfo info;
            juce::File folder;
            bool enabled = true;
            bool developer = false;     ///< loaded from a developer folder (not copied)
            juce::String error;         ///< could not be loaded (info may be incomplete)
            juce::String source;        ///< the main script
            juce::Time changed;         ///< newest file time when loaded (developer reload)
        };

        explicit LuaPluginManager (juce::File installFolder);
        ~LuaPluginManager();

        //==============================================================================
        /** Reads every plugin again and updates the effect types. */
        void reload();

        std::vector<Plugin> getPlugins() const;
        const Plugin* find (const juce::String& id) const;

        /** Switched-off plugins (stored by the app in its settings). */
        void setDisabled (const juce::StringArray& ids);
        juce::StringArray getDisabled() const { return disabled; }
        void setEnabled (const juce::String& id, bool shouldBeOn);

        /** Developer folders (stored by the app in its settings). */
        void setDeveloperFolders (const juce::StringArray& folders);
        juce::StringArray getDeveloperFolders() const { return developerFolders; }

        /** Reloads developer plugins whose files changed. Returns true if any did. */
        bool reloadChangedDeveloperPlugins();

        //==============================================================================
        /** Reads plugin.json from a package without installing it. */
        static bool readPackage (const juce::File& package, LuaPluginInfo& info, juce::String& error);

        /** Installs (or updates) a package. */
        bool install (const juce::File& package, juce::String& error);

        /** Removes an installed plugin (developer folders are only forgotten). */
        bool uninstall (const juce::String& id);

        juce::File getInstallFolder() const { return folder; }

        //==============================================================================
        /** Runs an analysis plugin (a copy from getPlugins()) on `audio`, on a worker
            thread. Returns its report, or false with an error. */
        bool runAnalysis (const Plugin& plugin, const AudioSequence& audio, const std::vector<float>& values,
                          const dsp::WorkControl& control, juce::String& report, juce::String& error) const;

        /** Runs a tool plugin (message thread). `runCommand` runs program commands.
            Returns what the plugin said with mxj.message(), or false with an error. */
        bool runTool (const juce::String& id, const std::vector<float>& values,
                      std::function<bool (const juce::String&)> runCommand,
                      juce::String& messages, juce::String& error) const;

        //==============================================================================
        /** The developer console: what plugins printed and their errors. Thread safe. */
        void log (const juce::String& line) const;
        juce::StringArray getConsole() const;
        void clearConsole();

        /** Called after the list of plugins changed (message thread). */
        std::function<void()> onPluginsChanged;
        /** Called after a line was added to the console (any thread). */
        std::function<void()> onConsoleChanged;

        /** Group of the Effects menu for MXJ plugins. */
        static constexpr const char* menuGroup = "MXJ Plugins";

    private:
        void registerEffects();
        Plugin readPlugin (const juce::File& pluginFolder, bool developer) const;

        juce::File folder;
        std::vector<Plugin> plugins;
        juce::StringArray disabled, developerFolders;

        struct Console;
        std::shared_ptr<Console> console;

        JUCE_DECLARE_NON_COPYABLE (LuaPluginManager)
    };
}
