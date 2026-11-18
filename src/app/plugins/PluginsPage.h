// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "app/plugins/PluginSystem.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** The plugin page (master plan, section 7), a window with four tabs:

          Installed   MXJ plugins: switch on or off, open, update, remove, install from a file
          Discover    the catalogue: search, category, description, install with one click
          VST & CLAP  the plugins found, the folders to scan, scanning, the block list
          Developer   load a plugin from a folder (reloaded on save), the error console
    */
    class PluginsPage : public juce::DocumentWindow
    {
    public:
        /** Opens the page, or brings it to the front. `use` opens an MXJ plugin
            by id (the effect window, or runs an analysis or tool plugin). */
        static void show (PluginSystem& plugins, std::function<void (const juce::String& luaId)> use);

        /** Closes the page (on quit). */
        static void closeAll();

        PluginsPage (PluginSystem& plugins, std::function<void (const juce::String&)> use);
        ~PluginsPage() override;

        void closeButtonPressed() override;

    private:
        class Content;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginsPage)
    };
}
