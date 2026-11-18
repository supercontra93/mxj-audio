// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "plugins/host/HostedPlugin.h"

#include <map>
#include <mutex>

namespace mxj::plugins
{
    /** VST3 and CLAP plugins (master plan, section 7): the formats, the list of
        plugins found by scanning, the files that failed (blocked), the folders
        to scan, and the effect types they become.

        Every effect plugin becomes an effect type in dsp::catalogue (category
        "VST3" or "CLAP"), so it can be used everywhere a built-in effect can:
        the Effects menu, the effect rack, track effects. Instruments are listed
        but not used (MIDI comes after v1.0).

        The list is kept in an XML file in the plugins folder, so the program
        starts without scanning. Message thread, except where noted.
    */
    class PluginHost
    {
    public:
        struct BlockedFile
        {
            juce::String format, file, reason;
        };

        PluginHost();
        ~PluginHost();

        /** Adds a format. The app adds JUCE's VST3 format with windows and our
            CLAP format; tests and the scanner the windowless ones. */
        void addFormat (std::unique_ptr<juce::AudioPluginFormat> format);
        juce::AudioPluginFormat* findFormat (const juce::String& name) const;
        juce::StringArray getFormatNames() const;

        //==============================================================================
        /** Reads the list file. Returns false if it is missing or damaged (the list stays empty). */
        bool load (const juce::File& listFile);
        bool save() const;

        std::vector<juce::PluginDescription> getPlugins() const;
        std::vector<BlockedFile> getBlocked() const;

        /** Folders scanned for a format: its standard folders, then the user's. */
        juce::StringArray getStandardFolders (const juce::String& format) const;
        juce::StringArray getUserFolders (const juce::String& format) const;
        void setUserFolders (const juce::String& format, const juce::StringArray& folders);

        /** Lets a blocked file be scanned again. */
        void unblock (const juce::String& file);

        /** Forgets everything that was scanned (the folders stay). */
        void clearList();

        /** Files to scan: those in the folders that are new or changed since the
            last scan, and not blocked. `everything`: all of them. Any thread. */
        std::vector<std::pair<juce::String, juce::String>> findFilesToScan (bool everything) const;

        /** Records the result of scanning one file. */
        void addScanResult (const juce::String& format, const juce::String& file,
                            const juce::OwnedArray<juce::PluginDescription>& found);
        void addBlocked (const juce::String& format, const juce::String& file, const juce::String& reason);

        /** Removes plugins whose file no longer exists. */
        void removeMissing();

        //==============================================================================
        /** Makes the effect types of the listed effect plugins (and removes old ones). */
        void registerEffects();

        /** The effect type id of a plugin: "vst3:..." or "clap:...". Stable while the plugin keeps its id and name. */
        static juce::String effectIdFor (const juce::PluginDescription&);

        /** True for plugins usable as effects (audio in and out, or not known, and not an instrument). */
        static bool isEffect (const juce::PluginDescription&);

        /** Loads a plugin. Any thread (waits for the message thread when needed). */
        std::unique_ptr<juce::AudioPluginInstance> createInstance (const juce::PluginDescription&, double sampleRate,
                                                                   int blockSize, juce::String& error) const;

        /** Called after the list changed (scan finished, folder or block list changed). */
        std::function<void()> onListChanged;

    private:
        struct Core;
        std::shared_ptr<Core> core;

        juce::File listFile;
        std::vector<juce::PluginDescription> plugins;
        std::vector<BlockedFile> blocked;
        std::map<juce::String, juce::Time> scannedFiles; ///< file -> modification time when scanned
        std::map<juce::String, juce::StringArray> userFolders;
        mutable std::mutex lock; ///< plugins, blocked, scannedFiles, userFolders (read by the scan thread)

        JUCE_DECLARE_NON_COPYABLE (PluginHost)
    };
}
