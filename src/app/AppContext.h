// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/commands/CommandRegistry.h"
#include "core/history/History.h"
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

        AppPaths paths;
        Settings settings;
        CommandRegistry commands;
        History history;
    };
}
