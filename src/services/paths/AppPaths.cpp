// SPDX-License-Identifier: AGPL-3.0-only
#include "services/paths/AppPaths.h"
#include "core/Version.h"

namespace mxj
{
    AppPaths::AppPaths() : AppPaths (getDefaultRoot()) {}

    AppPaths::AppPaths (juce::File rootFolder) : root (std::move (rootFolder)) {}

    juce::File AppPaths::getDefaultRoot()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile (version::name);
    }

    bool AppPaths::createFolders() const
    {
        bool ok = true;

        for (const auto& f : { root, getLogsFolder(), getCacheFolder(), getPluginsFolder(), getPresetsFolder() })
            ok = f.createDirectory().wasOk() && ok;

        return ok;
    }
}
