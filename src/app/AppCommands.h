// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "app/AppContext.h"
#include "app/Workspace.h"

namespace mxj
{
    /** Menus in display order. Each name is an English source string. */
    juce::StringArray getMenuNames();

    /** Ids of commands after which the menu shows a separator line. */
    juce::StringArray getMenuSeparatorsAfter();

    /** What the UI must provide so that commands can act on it. */
    struct AppActions
    {
        std::function<void()> quit;
        std::function<void()> showAbout;
        std::function<void (const juce::String& languageCode)> setLanguage;
        std::function<void()> showAudioSettings;
    };

    /** Registers the commands of the program. */
    void registerAppCommands (AppContext& context, Workspace& workspace, AppActions actions);
}
