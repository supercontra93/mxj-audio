// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_core/juce_core.h>

namespace mxj
{
    /** Where MXJ AUDIO keeps its files on this computer.

        Default root on Windows: %APPDATA%\MXJ AUDIO
        (macOS later: ~/Library/Application Support/MXJ AUDIO).
        A different root can be given, for tests and for a future portable mode.
    */
    class AppPaths
    {
    public:
        /** Uses the platform's per-user application data folder. */
        AppPaths();

        /** Uses the given folder as root. */
        explicit AppPaths (juce::File rootFolder);

        juce::File getRoot() const          { return root; }
        juce::File getSettingsFile() const  { return root.getChildFile ("settings.json"); }
        juce::File getLogsFolder() const    { return root.getChildFile ("logs"); }
        juce::File getCacheFolder() const   { return root.getChildFile ("cache"); }
        juce::File getPluginsFolder() const { return root.getChildFile ("plugins"); }
        juce::File getPresetsFolder() const { return root.getChildFile ("presets"); }

        /** Creates the folders above. Returns false if any could not be created. */
        bool createFolders() const;

        static juce::File getDefaultRoot();

    private:
        juce::File root;
    };
}
