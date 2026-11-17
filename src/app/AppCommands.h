// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "app/AppContext.h"

namespace mxj
{
    /** Menus in display order. Each name is an English source string. */
    juce::StringArray getMenuNames();

    /** What the UI must provide so that commands can act on it. */
    struct AppActions
    {
        std::function<void()> quit;
        std::function<void()> showAbout;
        std::function<void (const juce::String& languageCode)> setLanguage;
    };

    /** Registers the commands that exist in phase 0. */
    void registerAppCommands (AppContext& context, AppActions actions);
}
