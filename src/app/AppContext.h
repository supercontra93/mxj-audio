// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/audio/BlockStore.h"
#include "core/commands/CommandRegistry.h"
#include "core/history/History.h"
#include "engine/devices/AudioEngine.h"
#include "services/paths/AppPaths.h"
#include "services/settings/Settings.h"

namespace mxj
{
    /** The long-lived objects of one program run. Owned by the application
        object, passed by reference to the parts of the UI that need them. */
    struct AppContext
    {
        explicit AppContext (AppPaths p)
            : paths (std::move (p)), settings (paths.getSettingsFile()) {}

        /** Temporary audio of this run. Created on first use; folders left
            behind by an earlier crash are removed first. */
        BlockStore& blocks()
        {
            if (blockStore == nullptr)
            {
                const auto folder = paths.getCacheFolder().getChildFile ("blocks");
                BlockStore::cleanUpAbandonedFolders (folder);
                blockStore = std::make_unique<BlockStore> (folder);
            }

            return *blockStore;
        }

    private:
        // Declared before `audio` so that playback lets go of the audio first.
        std::unique_ptr<BlockStore> blockStore;

    public:
        AppPaths paths;
        Settings settings;
        CommandRegistry commands;
        History history;
        AudioEngine audio;
    };
}
