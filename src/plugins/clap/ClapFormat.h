// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <functional>
#include <utility>

namespace mxj::plugins
{
    /** Hosts CLAP plugins (https://cleveraudio.org, MIT licensed headers).

        JUCE hosts VST3 but not CLAP, so this is our own small host: it finds
        .clap files, lists the plugins inside, and wraps one as a
        juce::AudioPluginInstance, so the rest of the program treats CLAP and
        VST3 the same way. Audio effects only (stereo or mono main ports); note
        input and output are ignored. See ADR 0010.

        The plugin's own window is not a JUCE editor: the app layer embeds it
        through ClapPluginInstance::Gui (this layer may not use UI modules).
    */
    class ClapPluginFormat final : public juce::AudioPluginFormat
    {
    public:
        ClapPluginFormat() = default;

        static juce::String getFormatName() { return "CLAP"; }
        juce::String getName() const override { return getFormatName(); }

        void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>&, const juce::String& file) override;
        bool fileMightContainThisPluginType (const juce::String& file) override;
        juce::String getNameOfPluginFromIdentifier (const juce::String& file) override;
        bool pluginNeedsRescanning (const juce::PluginDescription&) override;
        bool doesPluginStillExist (const juce::PluginDescription&) override;
        bool canScanForPlugins() const override { return true; }
        bool isTrivialToScan() const override   { return false; }
        juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool recursive, bool) override;
        juce::FileSearchPath getDefaultLocationsToSearch() override;

    private:
        void createPluginInstance (const juce::PluginDescription&, double initialSampleRate,
                                   int initialBufferSize, PluginCreationCallback) override;
        bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override { return false; }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClapPluginFormat)
    };

    /** One loaded CLAP plugin. Created by ClapPluginFormat. */
    class ClapPluginInstance : public juce::AudioPluginInstance
    {
    public:
        /** The plugin's own window, embedded by the app layer into a native
            window it owns. Message thread only. */
        class Gui
        {
        public:
            virtual ~Gui() = default;

            /** "win32", "x11" or "cocoa" for this platform, if the plugin has a window for it. */
            virtual bool isSupported() const = 0;

            /** Creates the window inside `nativeParent` (HWND, X11 Window or NSView).
                Returns false if the plugin refuses. `width` and `height` get its size. */
            virtual bool open (void* nativeParent, double scale, int& width, int& height) = 0;
            virtual void close() = 0;
            virtual bool canResize() const = 0;

            /** Asks the plugin to take this size; it may adjust it. Returns the size it took. */
            virtual std::pair<int, int> resize (int width, int height) = 0;

            /** The plugin asks for a new size (called on the message thread). */
            std::function<void (int width, int height)> onResizeRequest;
            /** The plugin closed its window itself. */
            std::function<void()> onClosedByPlugin;
        };

        using juce::AudioPluginInstance::AudioPluginInstance;

        /** nullptr if the plugin has no window. */
        virtual Gui* getGui() = 0;
    };
}
