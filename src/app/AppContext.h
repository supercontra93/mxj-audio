// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/audio/BlockStore.h"
#include "core/commands/CommandRegistry.h"
#include "core/history/History.h"
#include "engine/devices/AudioEngine.h"
#include "services/paths/AppPaths.h"
#include "services/presets/PresetStore.h"
#include "services/settings/Settings.h"

namespace mxj
{
    /** The long-lived objects of one program run. Owned by the application
        object, passed by reference to the parts of the UI that need them. */
    struct AppContext
    {
        explicit AppContext (AppPaths p)
            : paths (std::move (p)), settings (paths.getSettingsFile()), presets (paths.getPresetsFolder()) {}

        /** Temporary audio of this run. Created on first use; folders left
            behind by an earlier crash are removed first, except one whose work
            can still be restored (see findRecoverableRun()). */
        BlockStore& blocks()
        {
            if (blockStore == nullptr)
            {
                const auto folder = getBlocksFolder();
                BlockStore::cleanUpAbandonedFolders (folder, recoverableRun);
                blockStore = std::make_unique<BlockStore> (folder);
            }

            return *blockStore;
        }

        juce::File getBlocksFolder() const { return paths.getCacheFolder().getChildFile ("blocks"); }

        /** Looks for unsaved work of a run that crashed. Call before blocks(). */
        juce::File findRecoverableRun()
        {
            recoverableRun = BlockStore::findRecoverableFolder (getBlocksFolder());
            return recoverableRun;
        }

    private:
        // Declared before `audio` so that playback lets go of the audio first.
        std::unique_ptr<BlockStore> blockStore;
        juce::File recoverableRun;

    public:
        AppPaths paths;
        Settings settings;
        PresetStore presets;
        CommandRegistry commands;
        History history;
        AudioEngine audio;
    };
}
