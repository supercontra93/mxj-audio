// SPDX-License-Identifier: AGPL-3.0-only
#include "app/AppCommands.h"
#include "services/i18n/Localisation.h"
#include "services/logging/Log.h"
#include "services/settings/SettingKeys.h"
#include "services/updates/UpdatePolicy.h"
#include "app/theme/Fonts.h"
#include "dsp/effects/EffectCatalogue.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    juce::StringArray getMenuNames()
    {
        return { "File", "Edit", "View", "Transport", "Effects", "Multitrack", "Plugins", "Window", "Help" };
    }

    juce::StringArray getMenuSeparatorsAfter()
    {
        return { "file.close", "file.exportSelection",
                 "edit.redo", "edit.delete", "edit.selectNone", "edit.fadeOut", "markers.previous",
                 "view.zoomSelection", "view.textSize.small",
                 "transport.toEnd",
                 "effects.tone", "effects.applyRack" };
    }

    void registerAppCommands (AppContext& ctx, Workspace& w, AppActions actions)
    {
        auto& c = ctx.commands;
        auto& history = ctx.history;
        const auto never = [] { return false; }; // arrives in a later phase
        const auto hasFile = [&w] { return w.hasDocument() && ! w.isRecording(); };
        const auto editable = [&w] { return w.canEdit(); };
        const auto selected = [&w] { return w.canEdit() && w.hasSelection(); };
        const auto pastable = [&w] { return w.canEdit() && w.hasClipboard(); };
        const auto hasMarker = [&w] { return w.canEdit() && w.getCurrentMarker() >= 0; };
        const auto hasMarkers = [&w] { return w.isReady() && ! w.isRecording() && ! w.getDocument()->getMarkers().empty(); };
        const auto notBusy = [&w] { return ! w.isBusy() && ! w.isRecording(); };

        // File
        c.add ({ "file.new",        "New Audio File...",          "File", "Create an empty audio file",         "command + N",         never,   [] {} });
        c.add ({ "file.newSession", "New Multitrack Session...",  "File", "Create a multitrack session",        "command + shift + N", never,   [] {} });
        c.add ({ "file.open",       "Open...",                    "File", "Open an audio file or project",      "command + O",         notBusy, [&w] { w.showOpenDialog(); } });
        c.add ({ "file.close",      "Close",                      "File", "Close the open file",                "command + W",         [&w] { return w.hasDocument() && ! w.isBusy(); }, [&w] { w.closeFile(); } });
        c.add ({ "file.save",       "Save",                       "File", "Save the changes to the file",       "command + S",         editable, [&w] { w.save(); } });
        c.add ({ "file.saveAs",     "Save As...",                 "File", "Save under a new name or in another format", "command + shift + S", editable, [&w] { w.showSaveAsDialog(); } });
        c.add ({ "file.export",     "Export...",                  "File", "Save a copy in another format; the open file stays as it is", "command + shift + E", editable, [&w] { w.showExportDialog (false); } });
        c.add ({ "file.exportSelection", "Export Selection...",   "File", "Save the selected audio as a new file", {},               selected, [&w] { w.showExportDialog (true); } });
        c.add ({ "app.quit",        "Exit",                       "File", "Close MXJ AUDIO",                    "command + Q",         nullptr, actions.quit });

        // Edit
        c.add ({ "edit.undo",        "Undo",            "Edit", "Undo the last action",                        "command + Z",         [&w, &history] { return w.canEdit() && history.canUndo(); }, [&w] { w.undo(); } });
        c.add ({ "edit.redo",        "Redo",            "Edit", "Redo the last undone action",                 "command + shift + Z", [&w, &history] { return w.canEdit() && history.canRedo(); }, [&w] { w.redo(); } });
        c.add ({ "edit.cut",         "Cut",             "Edit", "Remove the selection and keep it for pasting", "command + X",        selected, [&w] { w.cut(); } });
        c.add ({ "edit.copy",        "Copy",            "Edit", "Keep a copy of the selection for pasting",     "command + C",        selected, [&w] { w.copy(); } });
        c.add ({ "edit.paste",       "Paste",           "Edit", "Insert the copied audio at the cursor or over the selection", "command + V", pastable, [&w] { w.paste(); } });
        c.add ({ "edit.mixPaste",    "Mix Paste",       "Edit", "Mix the copied audio into the audio at the cursor", "command + shift + V", pastable, [&w] { w.mixPaste(); } });
        c.add ({ "edit.delete",      "Delete",          "Edit", "Remove the selection",                        "delete",              selected, [&w] { w.deleteSelection(); } });
        c.add ({ "edit.selectAll",   "Select All",      "Edit", "Select the whole file",                       "command + A",         editable, [&w] { w.selectAll(); } });
        c.add ({ "edit.selectNone",  "Select None",     "Edit", "Remove the selection, keep the cursor",       "escape",              selected, [&w] { w.selectNone(); } });
        c.add ({ "edit.trim",        "Trim to Selection", "Edit", "Keep only the selection, remove the rest",  "command + T",         selected, [&w] { w.trimToSelection(); } });
        c.add ({ "edit.silence",     "Silence",         "Edit", "Replace the selection with silence",          "command + L",         selected, [&w] { w.silenceSelection(); } });
        c.add ({ "edit.insertSilence", "Insert Silence...", "Edit", "Insert silence of a chosen length at the cursor", {},           editable, [&w] { w.showInsertSilenceDialog(); } });
        c.add ({ "edit.fadeIn",      "Fade In",         "Edit", "Let the selection rise smoothly from silence", {},                   selected, [&w] { w.fadeIn(); } });
        c.add ({ "edit.fadeOut",     "Fade Out",        "Edit", "Let the selection fall smoothly to silence",   {},                   selected, [&w] { w.fadeOut(); } });
        c.add ({ "markers.add",      "Add Marker",      "Edit", "Add a marker at the playhead (also while playing)", "M",              editable, [&w] { w.addMarker(); } });
        c.add ({ "markers.rename",   "Rename Marker...", "Edit", "Rename the selected marker",                 {},                    hasMarker, [&w] { w.showRenameMarkerDialog(); } });
        c.add ({ "markers.delete",   "Delete Marker",   "Edit", "Delete the selected marker",                  {},                    hasMarker, [&w] { w.deleteMarker(); } });
        c.add ({ "markers.next",     "Next Marker",     "Edit", "Jump to the next marker",                     "command + cursor right", hasMarkers, [&w] { w.goToNextMarker(); } });
        c.add ({ "markers.previous", "Previous Marker", "Edit", "Jump to the previous marker",                 "command + cursor left",  hasMarkers, [&w] { w.goToPreviousMarker(); } });
        c.add ({ "settings.audioDevices", "Audio Device Settings...", "Edit", "Choose the sound card, the recording input and their settings", {},
                 [&w] { return ! w.isRecording(); }, actions.showAudioSettings });

        // View
        c.add ({ "view.zoomIn",   "Zoom In",            "View", "Show the waveform in more detail",       "command + 1", hasFile, [&w] { w.zoomIn(); } });
        c.add ({ "view.zoomOut",  "Zoom Out",           "View", "Show more of the waveform",              "command + 3", hasFile, [&w] { w.zoomOut(); } });
        c.add ({ "view.zoomFull", "Zoom to Whole File", "View", "Fit the whole file into the window",     "command + F", hasFile, [&w] { w.zoomToFit(); } });
        c.add ({ "view.zoomSelection", "Zoom to Selection", "View", "Fit the selection into the window",  "command + E", [&w] { return w.hasSelection() && ! w.isRecording(); }, [&w] { w.zoomToSelection(); } });
        c.add ({ "view.language.tr", "Turkish", "View", "Show the interface in Turkish", {},
                 nullptr, [setLanguage = actions.setLanguage] { setLanguage ("tr"); },
                 [] { return Localisation::getCurrentLanguage() == "tr"; } });
        c.add ({ "view.language.en", "English", "View", "Show the interface in English", {},
                 nullptr, [setLanguage = actions.setLanguage] { setLanguage ("en"); },
                 [] { return Localisation::getCurrentLanguage() == "en"; } });

        // Text size (phase 3: Baybars found the text too small)
        const auto textSize = [setTextScale = actions.setTextScale] (float scale)
        {
            return [setTextScale, scale] { setTextScale (scale); };
        };
        const auto textSizeIs = [] (float scale) { return [scale] { return juce::approximatelyEqual (fonts::getTextScale(), scale); }; };
        c.add ({ "view.textSize.small",  "Small Text",   "View", "Show the interface text at its smallest size", {}, nullptr, textSize (fonts::textScales[0]), textSizeIs (fonts::textScales[0]), "Text Size" });
        c.add ({ "view.textSize.normal", "Normal Text",  "View", "Show the interface text at its normal size",   {}, nullptr, textSize (fonts::textScales[1]), textSizeIs (fonts::textScales[1]), "Text Size" });
        c.add ({ "view.textSize.large",  "Large Text",   "View", "Show the interface text larger",               {}, nullptr, textSize (fonts::textScales[2]), textSizeIs (fonts::textScales[2]), "Text Size" });
        c.add ({ "view.textSize.larger", "Largest Text", "View", "Show the interface text at its largest size",  {}, nullptr, textSize (fonts::textScales[3]), textSizeIs (fonts::textScales[3]), "Text Size" });

        // Transport
        c.add ({ "transport.playStop", "Play / Stop",  "Transport", "Play the selection, or from the cursor; stop", "spacebar",
                 [&w] { return (w.isReady() && ! w.isBusy()) || w.isRecording(); },
                 [&w] { if (w.isRecording()) w.toggleRecording(); else w.togglePlayback(); } });
        c.add ({ "transport.record",   "Record",       "Transport", "Record from the microphone into the file at the cursor, or into a new file", "R",
                 [&w] { return ! w.isBusy() && (! w.hasDocument() || w.isReady()); }, [&w] { w.toggleRecording(); },
                 [&w] { return w.isRecording(); } });
        c.add ({ "transport.toStart",  "Go to Start",  "Transport", "Move the cursor to the start of the file", "home",   hasFile, [&w] { w.goToStart(); } });
        c.add ({ "transport.toEnd",    "Go to End",    "Transport", "Move the cursor to the end of the file",   "end",    hasFile, [&w] { w.goToEnd(); } });

        // Effects (phase 3): one command per effect, grouped into submenus.
        for (const auto& type : dsp::catalogue::all())
        {
            const auto id = "effects." + type.id;
            c.add ({ id, type.name + "...", "Effects", type.description, {}, editable,
                     [showEffect = actions.showEffect, typeId = type.id] { showEffect (typeId); }, nullptr, type.category });

            if (type.id == "amplify")
                c.add ({ "effects.invert", "Invert", "Effects", "Turn the waveform upside down (polarity)", {}, editable,
                         [&w] { w.invertAudio(); }, nullptr, type.category });
            if (type.id == "speed")
                c.add ({ "effects.reverse", "Reverse", "Effects", "Play the selection backwards", {}, editable,
                         [&w] { w.reverseAudio(); }, nullptr, type.category });
        }

        c.add ({ "effects.applyRack", "Apply Effect Rack", "Effects", "Apply the effects of the rack to the selection, or to the whole file", {},
                 [&w] { return w.canEdit() && w.getRack().hasActiveEffects(); }, [&w] { w.applyRack(); } });
        c.add ({ "analysis.loudness", "Loudness and Levels...", "Effects", "Measure loudness (LUFS), peaks and levels of the selection or the file", {},
                 [&w] { return w.canEdit(); }, actions.showLoudnessAndLevels });

        // Window: panels of the editor
        const auto togglePanel = [&ctx, &w] (const char* key)
        {
            return [&ctx, &w, key]
            {
                ctx.settings.set (key, ! ctx.settings.getBool (key));
                w.sendChangeMessage(); // the editor shows or hides the panel
            };
        };
        const auto panelShown = [&ctx] (const char* key) { return [&ctx, key] { return ctx.settings.getBool (key); }; };
        c.add ({ "window.effectRack", "Effect Rack",       "Window", "Show or hide the effect rack",       "command + R", nullptr, togglePanel (keys::showRack),     panelShown (keys::showRack) });
        c.add ({ "window.meters",     "Level Meters",      "Window", "Show or hide the level meters",      {},            nullptr, togglePanel (keys::showMeters),   panelShown (keys::showMeters) });
        c.add ({ "window.spectrum",   "Spectrum Analyzer", "Window", "Show or hide the spectrum analyzer", {},            nullptr, togglePanel (keys::showSpectrum), panelShown (keys::showSpectrum) });

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
