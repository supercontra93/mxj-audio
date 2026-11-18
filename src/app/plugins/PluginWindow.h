// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "plugins/host/HostedPlugin.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** The own window of a VST3 or CLAP plugin. One window per live plugin:
        showing it again brings it to the front.

        VST3 plugins show their JUCE editor. CLAP plugins draw into the native
        window (ClapPluginInstance::Gui). A plugin without a window gets a
        message instead.
    */
    class PluginWindow : public juce::DocumentWindow
    {
    public:
        /** Opens (or brings to the front) the window of the plugin in `state`.
            Does nothing if `state` is not a hosted plugin. */
        static void show (const std::shared_ptr<dsp::EffectState>& state, const juce::String& title);

        /** True if `state` is a hosted plugin (it can have a window). */
        static bool canShow (const std::shared_ptr<dsp::EffectState>& state);

        /** Closes the window of the plugin in `state`, if open. */
        static void closeFor (const std::shared_ptr<dsp::EffectState>& state);

        /** Closes every plugin window (before the plugins are unloaded). */
        static void closeAll();

        PluginWindow (std::shared_ptr<plugins::HostedPlugin> plugin, const juce::String& title);
        ~PluginWindow() override;

        void closeButtonPressed() override;

    private:
        class ClapView;
        class Message;

        std::shared_ptr<plugins::HostedPlugin> plugin;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginWindow)
    };
}
