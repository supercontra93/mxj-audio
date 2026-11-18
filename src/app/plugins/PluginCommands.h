// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "app/AppContext.h"
#include "app/Workspace.h"
#include "app/plugins/PluginSystem.h"
#include "app/session/SessionWorkspace.h"

namespace mxj
{
    struct PluginActions
    {
        std::function<void (const juce::String& effectId)> showEffect;
        std::function<void()> showPluginsPage;
        std::function<juce::Component*()> getMainWindow;
    };

    /** Registers the commands of the plugins (phase 6):
          plugins.manager, plugins.scan, plugins.installFile    (Plugins menu)
          effects.<type id>       one per plugin effect (Effects menu, groups VST3, CLAP, MXJ Plugins)
          plugins.run.<id>        one per MXJ analysis or tool plugin (Plugins menu)
        Call again after the plugins changed: the commands of plugins are replaced. */
    void registerPluginCommands (AppContext& context, Workspace& workspace, SessionWorkspace& session,
                                 PluginSystem& plugins, PluginActions actions);

    /** Runs an MXJ analysis or tool plugin; asks for its settings first if it has any. */
    void runLuaPlugin (AppContext& context, Workspace& workspace, PluginSystem& plugins,
                       const juce::String& id, juce::Component* parent);

    /** Asks for a .mxjplugin file and installs it. */
    void installPluginFromFile (PluginSystem& plugins, std::function<void (bool ok)> onDone = nullptr);
}
