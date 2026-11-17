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
        return { "File", "Edit", "View", "Transport", "Effects", "Multitrack", "Plugins", "Window", "Help" };
    }

    void registerAppCommands (AppContext& ctx, Workspace& w, AppActions actions)
    {
        auto& c = ctx.commands;
        auto& history = ctx.history;
        const auto never = [] { return false; }; // arrives in a later phase
        const auto hasFile = [&w] { return w.hasDocument(); };
        const auto ready = [&w] { return w.isReady(); };

        // File
        c.add ({ "file.new",        "New Audio File...",          "File", "Create an empty audio file",         "command + N",         never,   [] {} });
        c.add ({ "file.newSession", "New Multitrack Session...",  "File", "Create a multitrack session",        "command + shift + N", never,   [] {} });
        c.add ({ "file.open",       "Open...",                    "File", "Open an audio file or project",      "command + O",         nullptr, [&w] { w.showOpenDialog(); } });
        c.add ({ "file.close",      "Close",                      "File", "Close the open file",                "command + W",         hasFile, [&w] { w.closeFile(); } });
        c.add ({ "app.quit",        "Exit",                       "File", "Close MXJ AUDIO",                    "command + Q",         nullptr, actions.quit });

        // Edit
        c.add ({ "edit.undo", "Undo", "Edit", "Undo the last action", "command + Z",
                 [&history] { return history.canUndo(); }, [&history] { history.undo(); } });
        c.add ({ "edit.redo", "Redo", "Edit", "Redo the last undone action", "command + shift + Z",
                 [&history] { return history.canRedo(); }, [&history] { history.redo(); } });
        c.add ({ "settings.audioDevices", "Audio Device Settings...", "Edit", "Choose the sound card and its sample rate and buffer size", {},
                 nullptr, actions.showAudioSettings });

        // View
        c.add ({ "view.zoomIn",   "Zoom In",            "View", "Show the waveform in more detail",       "command + 1", hasFile, [&w] { w.zoomIn(); } });
        c.add ({ "view.zoomOut",  "Zoom Out",           "View", "Show more of the waveform",              "command + 3", hasFile, [&w] { w.zoomOut(); } });
        c.add ({ "view.zoomFull", "Zoom to Whole File", "View", "Fit the whole file into the window",     "command + F", hasFile, [&w] { w.zoomToFit(); } });
        c.add ({ "view.language.tr", "Turkish", "View", "Show the interface in Turkish", {},
                 nullptr, [setLanguage = actions.setLanguage] { setLanguage ("tr"); },
                 [] { return Localisation::getCurrentLanguage() == "tr"; } });
        c.add ({ "view.language.en", "English", "View", "Show the interface in English", {},
                 nullptr, [setLanguage = actions.setLanguage] { setLanguage ("en"); },
                 [] { return Localisation::getCurrentLanguage() == "en"; } });

        // Transport
        c.add ({ "transport.playStop", "Play / Stop",  "Transport", "Start or stop playback from the cursor", "spacebar", ready, [&w] { w.togglePlayback(); } });
        c.add ({ "transport.toStart",  "Go to Start",  "Transport", "Move the cursor to the start of the file", "home",   hasFile, [&w] { w.goToStart(); } });
        c.add ({ "transport.toEnd",    "Go to End",    "Transport", "Move the cursor to the end of the file",   "end",    hasFile, [&w] { w.goToEnd(); } });

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
