// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "app/AppContext.h"
#include "app/Workspace.h"
#include "app/session/SessionWorkspace.h"

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
        std::function<void (const juce::String& effectId)> showEffect;
        std::function<void()> showLoudnessAndLevels;
        std::function<void (float scale)> setTextScale;
    };

    /** Registers the commands of the program. Commands shared by the waveform
        editor and the multitrack view (save, undo, play, zoom, ...) act on the
        view that is shown. */
    void registerAppCommands (AppContext& context, Workspace& workspace, SessionWorkspace& session, AppActions actions);
}
