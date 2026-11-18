// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "app/AppContext.h"
#include "plugins/catalogue/PluginCatalogue.h"
#include "plugins/host/PluginHost.h"
#include "plugins/host/PluginScanner.h"
#include "plugins/lua/LuaPlugins.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** The plugins of one program run (phase 6): VST3 and CLAP plugins with
        their scanner, the MXJ (Lua) plugins, and the local test catalogue.

        Made before the workspace, because effect racks stored in the settings
        may use plugin effects. Sends a change message whenever a list changed
        (the plugin page and the menus update). Message thread.
    */
    class PluginSystem : public juce::ChangeBroadcaster,
                         private juce::Timer
    {
    public:
        explicit PluginSystem (AppContext& context);
        ~PluginSystem() override;

        plugins::PluginHost& getHost() noexcept               { return host; }
        plugins::PluginScanner& getScanner() noexcept         { return *scanner; }
        plugins::LuaPluginManager& getLua() noexcept          { return lua; }
        const plugins::PluginCatalogue& getCatalogue() const  { return catalogue; }

        /** Why the catalogue could not be read, or empty. */
        juce::String getCatalogueError() const { return catalogueError; }

        /** Installs (or updates) a plugin of the catalogue, after checking its SHA-256. */
        bool installFromCatalogue (const juce::String& id, juce::String& error);

        /** Version of an installed MXJ plugin, or empty. */
        juce::String getInstalledVersion (const juce::String& id) const;

        void setEnabled (const juce::String& luaId, bool shouldBeOn);
        void setDeveloperFolders (const juce::StringArray& folders);

        /** Starts a scan of the VST3 and CLAP folders (new and changed files, or all). */
        void scan (bool everything);

        /** Writes the plugin settings (on quit). */
        void storeSettings();

        /** Called after effect types or plugins were added or removed, so the
            commands can be registered again. */
        std::function<void()> onPluginsChanged;

        /** Called when a scan ends (found, blocked). */
        std::function<void (int, int)> onScanFinished;

    private:
        void timerCallback() override;
        void listsChanged();

        AppContext& context;
        plugins::PluginHost host;
        std::unique_ptr<plugins::PluginScanner> scanner;
        plugins::LuaPluginManager lua;
        plugins::PluginCatalogue catalogue;
        juce::String catalogueError;
        const std::shared_ptr<bool> alive = std::make_shared<bool> (true); ///< read on the message thread only

        JUCE_DECLARE_NON_COPYABLE (PluginSystem)
    };
}
