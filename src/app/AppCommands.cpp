// SPDX-License-Identifier: AGPL-3.0-only
#include "app/AppCommands.h"
#include "services/i18n/Localisation.h"
#include "services/logging/Log.h"
#include "services/updates/UpdatePolicy.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    juce::StringArray getMenuNames()
    {
        return { "File", "Edit", "View", "Effects", "Multitrack", "Plugins", "Window", "Help" };
    }

    void registerAppCommands (AppContext& ctx, AppActions actions)
    {
        auto& c = ctx.commands;
        auto& history = ctx.history;
        const auto never = [] { return false; }; // arrives in a later phase

        // File
        c.add ({ "file.new",        "New Audio File...",          "File", "Create an empty audio file",         "command + N",         never,   [] {} });
        c.add ({ "file.newSession", "New Multitrack Session...",  "File", "Create a multitrack session",        "command + shift + N", never,   [] {} });
        c.add ({ "file.open",       "Open...",                    "File", "Open an audio file or project",      "command + O",         never,   [] {} });
        c.add ({ "app.quit",        "Exit",                       "File", "Close MXJ AUDIO",                    "command + Q",         nullptr, actions.quit });

        // Edit
        c.add ({ "edit.undo", "Undo", "Edit", "Undo the last action", "command + Z",
                 [&history] { return history.canUndo(); }, [&history] { history.undo(); } });
        c.add ({ "edit.redo", "Redo", "Edit", "Redo the last undone action", "command + shift + Z",
                 [&history] { return history.canRedo(); }, [&history] { history.redo(); } });

        // View
        c.add ({ "view.language.tr", "Turkish", "View", "Show the interface in Turkish", {},
                 nullptr, [setLanguage = actions.setLanguage] { setLanguage ("tr"); },
                 [] { return Localisation::getCurrentLanguage() == "tr"; } });
        c.add ({ "view.language.en", "English", "View", "Show the interface in English", {},
                 nullptr, [setLanguage = actions.setLanguage] { setLanguage ("en"); },
                 [] { return Localisation::getCurrentLanguage() == "en"; } });

        // Help
        c.add ({ "help.checkUpdates", "Check for Updates...", "Help", "Look for a newer version of MXJ AUDIO", {},
                 [] { return updates::enabled; }, [] {} });
        c.add ({ "help.openLogs", "Open Log Folder", "Help", "Show the log files, useful for bug reports", {},
                 nullptr, [&ctx] { ctx.paths.getLogsFolder().startAsProcess(); } });
        c.add ({ "help.website", "MXJ AUDIO on GitHub", "Help", "Open the project page in your browser", {},
                 nullptr, [] { juce::URL ("https://github.com/supercontra93/mxj-audio").launchInDefaultBrowser(); } });
        c.add ({ "help.about", "About MXJ AUDIO", "Help", "Version and licence information", {},
                 nullptr, actions.showAbout });

        Log::info ("Registered " + juce::String (c.size()) + " commands");
    }
}
