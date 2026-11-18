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
        return { "file.close", "file.exportMixdown",
                 "edit.redo", "edit.delete", "edit.selectNone", "edit.fadeOut", "markers.previous",
                 "view.zoomSelection", "view.multitrack", "view.spectral", "view.spectral.mel", "view.spectral.res8192", "view.textSize.small",
                 "transport.toEnd",
                 "multitrack.addBus", "multitrack.moveDown", "multitrack.snap", "multitrack.renameClip",
                 "window.trackEffects", "plugins.installFile",
                 "effects.tone", "effects.applyRack", "effects.clipRepair" };
    }

    void registerAppCommands (AppContext& ctx, Workspace& w, SessionWorkspace& s, AppActions actions)
    {
        auto& c = ctx.commands;
        auto& history = ctx.history;

        // Waveform editor conditions: false while the multitrack view is shown.
        const auto waveformShown = [&s] { return ! s.isActive(); };
        const auto hasFile = [&w, &s] { return ! s.isActive() && w.hasDocument() && ! w.isRecording(); };
        const auto editable = [&w, &s] { return ! s.isActive() && w.canEdit(); };
        const auto selected = [&w, &s] { return ! s.isActive() && w.canEdit() && w.hasSelection(); };
        const auto pastable = [&w, &s] { return ! s.isActive() && w.canEdit() && w.hasClipboard(); };
        const auto hasMarker = [&w, &s] { return ! s.isActive() && w.canEdit() && w.getCurrentMarker() >= 0; };
        const auto hasMarkers = [&w, &s] { return ! s.isActive() && w.isReady() && ! w.isRecording() && ! w.getDocument()->getMarkers().empty(); };
        const auto notBusy = [&w, &s] { return ! w.isBusy() && ! w.isRecording() && ! s.isBusy() && ! s.isRecording(); };

        // Multitrack conditions
        const auto sessionEditable = [&s] { return s.isActive() && s.canEdit(); };
        const auto sessionShown = [&s] { return s.isActive() && ! s.isRecording(); };
        const auto clipsSelected = [&s] { return s.isActive() && s.canEdit() && ! s.getSelectedClips().empty(); };
        const auto trackSelected = [&s] { return s.isActive() && s.canEdit() && s.getSession().findTrack (s.getSelectedTrack()) != nullptr
                                                 && s.getSelectedTrack() != 0; };

        /** The same command in both views: `inSession` while the multitrack view is shown. */
        using Condition = std::function<bool()>;
        using Action = std::function<void()>;
        const auto either = [&s] (Condition inSession, Condition inWaveform)
        {
            return [&s, inSession, inWaveform] { return s.isActive() ? inSession() : inWaveform(); };
        };
        const auto route = [&s] (Action inSession, Action inWaveform)
        {
            return [&s, inSession, inWaveform] { if (s.isActive()) inSession(); else inWaveform(); };
        };

        // File
        c.add ({ "file.new",        "New Audio File...",          "File", "Create an empty audio file",         "command + N",         notBusy, [&w] { w.showNewFileDialog(); } });
        c.add ({ "file.newSession", "New Multitrack Session...",  "File", "Create a multitrack session: tracks, clips, mixer and mixdown", "command + shift + N",
                 notBusy, [&s] { s.showNewSessionDialog(); } });
        c.add ({ "file.open",       "Open...",                    "File", "Open an audio file or project",      "command + O",         notBusy, [&w] { w.showOpenDialog(); } });
        c.add ({ "file.close",      "Close",                      "File", "Close the open file or session",     "command + W",
                 either ([&s] { return ! s.isBusy(); }, [&w] { return w.hasDocument() && ! w.isBusy(); }),
                 route ([&s] { s.closeSession(); }, [&w] { w.closeFile(); }) });
        c.add ({ "file.save",       "Save",                       "File", "Save the changes to the file or project", "command + S",
                 either ([&s] { return s.canEdit(); }, [&w] { return w.canEdit(); }), route ([&s] { s.save(); }, [&w] { w.save(); }) });
        c.add ({ "file.saveAs",     "Save As...",                 "File", "Save under a new name or in another format", "command + shift + S",
                 either ([&s] { return s.canEdit(); }, [&w] { return w.canEdit(); }), route ([&s] { s.showSaveAsDialog(); }, [&w] { w.showSaveAsDialog(); }) });
        c.add ({ "file.export",     "Export...",                  "File", "Save a copy in another format; the open file stays as it is", "command + shift + E",
                 either (sessionEditable, editable), route ([&s] { s.showExportMixdownDialog(); }, [&w] { w.showExportDialog (false); }) });
        c.add ({ "file.exportSelection", "Export Selection...",   "File", "Save the selected audio as a new file", {},               selected, [&w] { w.showExportDialog (true); } });
        c.add ({ "file.exportMixdown", "Export Multitrack Mixdown...", "File", "Mix all tracks of the session (or the time selection) into one audio file", {},
                 [&s] { return s.canEdit(); }, [&s] { s.setActive (true); s.showExportMixdownDialog(); } });
        c.add ({ "app.quit",        "Exit",                       "File", "Close MXJ AUDIO",                    "command + Q",         nullptr, actions.quit });

        // Edit
        c.add ({ "edit.undo",        "Undo",            "Edit", "Undo the last action",                        "command + Z",
                 either ([&s] { return s.canUndo(); }, [&w, &history] { return w.canEdit() && history.canUndo(); }),
                 route ([&s] { s.undo(); }, [&w] { w.undo(); }) });
        c.add ({ "edit.redo",        "Redo",            "Edit", "Redo the last undone action",                 "command + shift + Z",
                 either ([&s] { return s.canRedo(); }, [&w, &history] { return w.canEdit() && history.canRedo(); }),
                 route ([&s] { s.redo(); }, [&w] { w.redo(); }) });
        c.add ({ "edit.cut",         "Cut",             "Edit", "Remove the selection and keep it for pasting", "command + X",
                 either (clipsSelected, selected), route ([&s] { s.copyClips(); s.deleteSelection (false); }, [&w] { w.cut(); }) });
        c.add ({ "edit.copy",        "Copy",            "Edit", "Keep a copy of the selection for pasting",     "command + C",
                 either (clipsSelected, selected), route ([&s] { s.copyClips(); }, [&w] { w.copy(); }) });
        c.add ({ "edit.paste",       "Paste",           "Edit", "Insert the copied audio at the cursor or over the selection", "command + V",
                 either ([&s] { return s.canEdit() && s.hasClipboard(); }, pastable), route ([&s] { s.pasteClips(); }, [&w] { w.paste(); }) });
        c.add ({ "edit.mixPaste",    "Mix Paste",       "Edit", "Mix the copied audio into the audio at the cursor", "command + shift + V", pastable, [&w] { w.mixPaste(); } });
        c.add ({ "edit.delete",      "Delete",          "Edit", "Remove the selection",                        "delete",
                 either ([&s] { return s.canEdit() && s.hasSelection(); }, selected), route ([&s] { s.deleteSelection (false); }, [&w] { w.deleteSelection(); }) });
        c.add ({ "edit.selectAll",   "Select All",      "Edit", "Select the whole file",                       "command + A",
                 either (sessionEditable, editable),
                 route ([&s] { s.setTimeSelection ({ 0, juce::jmax ((juce::int64) 1, s.getSession().getLength()) }); }, [&w] { w.selectAll(); }) });
        c.add ({ "edit.selectNone",  "Select None",     "Edit", "Remove the selection, keep the cursor",       "escape",
                 either ([&s] { return s.hasSession() && s.hasSelection(); }, selected), route ([&s] { s.selectNone(); }, [&w] { w.selectNone(); }) });
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
                 [&w, &s] { return ! w.isRecording() && ! s.isRecording(); }, actions.showAudioSettings });

        // View
        c.add ({ "view.zoomIn",   "Zoom In",            "View", "Show the waveform in more detail",       "command + 1",
                 either (sessionShown, hasFile), route ([&s] { s.zoomIn(); }, [&w] { w.zoomIn(); }) });
        c.add ({ "view.zoomOut",  "Zoom Out",           "View", "Show more of the waveform",              "command + 3",
                 either (sessionShown, hasFile), route ([&s] { s.zoomOut(); }, [&w] { w.zoomOut(); }) });
        c.add ({ "view.zoomFull", "Zoom to Whole File", "View", "Fit the whole file into the window",     "command + F",
                 either (sessionShown, hasFile), route ([&s] { s.zoomToFit(); }, [&w] { w.zoomToFit(); }) });
        c.add ({ "view.zoomSelection", "Zoom to Selection", "View", "Fit the selection into the window",  "command + E",
                 [&w, &s] { return ! s.isActive() && w.hasSelection() && ! w.isRecording(); }, [&w] { w.zoomToSelection(); } });
        c.add ({ "view.waveform",   "Waveform Editor",  "View", "Show the waveform editor (one audio file)", "9",
                 [&s] { return ! s.isRecording() && ! s.isBusy(); }, [&s] { s.setActive (false); }, [&s] { return ! s.isActive(); } });
        c.add ({ "view.multitrack", "Multitrack Session", "View", "Show the multitrack session (opens a new one if none is open)", "0",
                 [&w, &s] { return ! w.isRecording() && ! w.isBusy() && ! s.isBusy(); },
                 [&s] { if (s.hasSession()) s.setActive (true); else s.showNewSessionDialog(); }, [&s] { return s.isActive(); } });
        c.add ({ "view.language.tr", "Turkish", "View", "Show the interface in Turkish", {},
                 nullptr, [setLanguage = actions.setLanguage] { setLanguage ("tr"); },
                 [] { return Localisation::getCurrentLanguage() == "tr"; } });
        c.add ({ "view.language.en", "English", "View", "Show the interface in English", {},
                 nullptr, [setLanguage = actions.setLanguage] { setLanguage ("en"); },
                 [] { return Localisation::getCurrentLanguage() == "en"; } });

        // Spectral display (phase 4)
        const auto setChoice = [&ctx, &w] (const char* key, juce::var value)
        {
            return [&ctx, &w, key, value]
            {
                ctx.settings.set (key, value);
                if (! ctx.settings.getBool (keys::showSpectral))
                    ctx.settings.set (keys::showSpectral, true);
                w.sendChangeMessage();
            };
        };
        const auto choiceIs = [&ctx] (const char* key, juce::var value) { return [&ctx, key, value] { return ctx.settings.get (key) == value; }; };
        const auto spectralGroup = "Spectrogram";
        c.add ({ "view.spectral", "Spectral Frequency Display", "View", "Show the frequencies (spectrogram) under the waveform", "shift + D",
                 nullptr, [&ctx, &w] { ctx.settings.set (keys::showSpectral, ! ctx.settings.getBool (keys::showSpectral)); w.sendChangeMessage(); },
                 [&ctx] { return ctx.settings.getBool (keys::showSpectral); } });
        c.add ({ "view.spectral.linear", "Linear Frequency Scale", "View", "Spectrogram: every kHz takes the same height", {}, nullptr,
                 setChoice (keys::spectralScale, "linear"), choiceIs (keys::spectralScale, "linear"), spectralGroup });
        c.add ({ "view.spectral.log", "Logarithmic Frequency Scale", "View", "Spectrogram: every octave takes the same height (like hearing)", {}, nullptr,
                 setChoice (keys::spectralScale, "log"), choiceIs (keys::spectralScale, "log"), spectralGroup });
        c.add ({ "view.spectral.mel", "Mel Frequency Scale", "View", "Spectrogram: the mel scale, between linear and logarithmic", {}, nullptr,
                 setChoice (keys::spectralScale, "mel"), choiceIs (keys::spectralScale, "mel"), spectralGroup });
        c.add ({ "view.spectral.res512", "Resolution: Fast Changes (512)", "View", "Spectrogram: sharp in time, blurred in frequency", {}, nullptr,
                 setChoice (keys::spectralResolution, 512), choiceIs (keys::spectralResolution, 512), spectralGroup });
        c.add ({ "view.spectral.res2048", "Resolution: Balanced (2048)", "View", "Spectrogram: balanced between time and frequency", {}, nullptr,
                 setChoice (keys::spectralResolution, 2048), choiceIs (keys::spectralResolution, 2048), spectralGroup });
        c.add ({ "view.spectral.res8192", "Resolution: Fine Frequencies (8192)", "View", "Spectrogram: sharp in frequency, blurred in time", {}, nullptr,
                 setChoice (keys::spectralResolution, 8192), choiceIs (keys::spectralResolution, 8192), spectralGroup });
        c.add ({ "view.spectral.amber", "Amber Colours", "View", "Spectrogram colours: dark to amber to white", {}, nullptr,
                 setChoice (keys::spectralColours, "amber"), choiceIs (keys::spectralColours, "amber"), spectralGroup });
        c.add ({ "view.spectral.gray", "Gray Colours", "View", "Spectrogram colours: black to white", {}, nullptr,
                 setChoice (keys::spectralColours, "gray"), choiceIs (keys::spectralColours, "gray"), spectralGroup });
        c.add ({ "view.spectral.rainbow", "Rainbow Colours", "View", "Spectrogram colours: blue to red", {}, nullptr,
                 setChoice (keys::spectralColours, "rainbow"), choiceIs (keys::spectralColours, "rainbow"), spectralGroup });

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
                 either ([&s] { return (s.hasSession() && ! s.isBusy()) || s.isRecording(); },
                         [&w] { return (w.isReady() && ! w.isBusy()) || w.isRecording(); }),
                 route ([&s] { if (s.isRecording()) s.toggleRecording(); else s.togglePlayback(); },
                        [&w] { if (w.isRecording()) w.toggleRecording(); else w.togglePlayback(); }) });
        c.add ({ "transport.record",   "Record",       "Transport", "Record from the microphone into the file at the cursor, or into a new file; in the session onto the armed track", "R",
                 either ([&s] { return s.hasSession() && ! s.isBusy(); }, [&w] { return ! w.isBusy() && (! w.hasDocument() || w.isReady()); }),
                 route ([&s] { s.toggleRecording(); }, [&w] { w.toggleRecording(); }),
                 [&w, &s] { return s.isActive() ? s.isRecording() : w.isRecording(); } });
        c.add ({ "transport.toStart",  "Go to Start",  "Transport", "Move the cursor to the start of the file", "home",
                 either (sessionShown, hasFile), route ([&s] { s.goToStart(); }, [&w] { w.goToStart(); }) });
        c.add ({ "transport.toEnd",    "Go to End",    "Transport", "Move the cursor to the end of the file",   "end",
                 either (sessionShown, hasFile), route ([&s] { s.goToEnd(); }, [&w] { w.goToEnd(); }) });

        // Multitrack (phase 5)
        c.add ({ "multitrack.import",      "Import Files...",     "Multitrack", "Add audio files as clips at the cursor, on the selected track or on new tracks", "command + I",
                 sessionEditable, [&s] { s.showImportDialog(); } });
        c.add ({ "multitrack.addTrack",    "Add Audio Track",     "Multitrack", "Add an audio track under the selected track", "shift + T",
                 sessionEditable, [&s] { s.addTrack(); } });
        c.add ({ "multitrack.addBus",      "Add Bus",             "Multitrack", "Add a bus: tracks can be routed or sent to it, for shared effects", "shift + B",
                 sessionEditable, [&s] { s.addBus(); } });
        c.add ({ "multitrack.removeTrack", "Remove Track",        "Multitrack", "Remove the selected track or bus with its clips", {},
                 trackSelected, [&s] { s.removeSelectedTrack(); } });
        c.add ({ "multitrack.moveUp",      "Move Track Up",       "Multitrack", "Move the selected track one place up", {},
                 trackSelected, [&s] { s.moveSelectedTrack (-1); } });
        c.add ({ "multitrack.moveDown",    "Move Track Down",     "Multitrack", "Move the selected track one place down", {},
                 trackSelected, [&s] { s.moveSelectedTrack (1); } });
        c.add ({ "multitrack.split",       "Split at Cursor",     "Multitrack", "Cut the clips under the cursor in two (the selected clips, or all tracks)", "command + K",
                 sessionEditable, [&s] { s.splitAtCursor(); } });
        c.add ({ "multitrack.rippleDelete", "Ripple Delete",      "Multitrack", "Delete the selection and move what follows to the left to close the gap", "shift + delete",
                 [&s] { return s.isActive() && s.canEdit() && s.hasSelection(); }, [&s] { s.deleteSelection (true); } });
        c.add ({ "multitrack.clipVolume",  "Clip Volume...",      "Multitrack", "Set the volume of the selected clips in dB", {},
                 clipsSelected, [&s] { s.showClipGainDialog(); } });
        c.add ({ "multitrack.clipMute",    "Mute Clip",           "Multitrack", "Silence the selected clips or bring them back", {},
                 clipsSelected, [&s] { s.toggleClipMute(); } });
        c.add ({ "multitrack.renameClip",  "Rename Clip...",      "Multitrack", "Give the selected clip a new name", {},
                 [&s] { return s.isActive() && s.canEdit() && s.getSelectedClips().size() == 1; }, [&s] { s.showRenameClipDialog(); } });
        c.add ({ "multitrack.snap",        "Snap",                "Multitrack", "Clips and the cursor snap to clip edges, markers and the cursor", "shift + S",
                 [&s] { return s.isActive(); }, [&s] { s.toggleSnapping(); }, [&s] { return s.isSnapping(); } });
        c.add ({ "multitrack.mixdown",     "Mixdown to New File", "Multitrack", "Mix the session (or the time selection) and open the result in the waveform editor", {},
                 sessionEditable, [&s] { s.mixdownToNewFile(); } });

        // Effects (phase 3): one command per effect, grouped into submenus.
        for (const auto* entry : dsp::catalogue::all())
        {
            const auto& type = *entry;
            if (! type.source.isEmpty())
                continue; // plugin effects are added by registerPluginCommands()

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

        // Noise reduction and spectral repair (phase 4), in the same submenu as the repair effects.
        const auto repairGroup = "Noise Reduction and Repair";
        const auto spectralSelected = [&w, &s] { return ! s.isActive() && w.canEdit() && w.hasSpectralSelection(); };
        c.add ({ "repair.captureNoisePrint", "Capture Noise Print", "Effects", "Measure the background noise in the selection, for Noise Reduction", "shift + P",
                 selected, [&w] { w.captureNoisePrint(); }, nullptr, repairGroup });
        c.add ({ "spectral.heal", "Heal Selection", "Effects", "Heal the selection from the sound around it: removes coughs, clicks and other short noises", "command + U",
                 selected, [&w] { w.healSelection(); }, nullptr, repairGroup });
        c.add ({ "spectral.changeLevel", "Change Level of Selection...", "Effects", "Raise or lower the selection or the selected frequencies by a number of dB", {},
                 selected, [&w] { w.showChangeLevelDialog(); }, nullptr, repairGroup });
        c.add ({ "spectral.deleteBand", "Delete Selected Frequencies", "Effects", "Remove only the frequencies selected in the spectrogram (also the Delete key)", {},
                 spectralSelected, [&w] { w.deleteSelectedFrequencies(); }, nullptr, repairGroup });

        c.add ({ "effects.applyRack", "Apply Effect Rack", "Effects", "Apply the effects of the rack to the selection, or to the whole file", {},
                 [&w, &s] { return ! s.isActive() && w.canEdit() && w.getRack().hasActiveEffects(); }, [&w] { w.applyRack(); } });
        c.add ({ "analysis.loudness", "Loudness and Levels...", "Effects", "Measure loudness (LUFS), peaks and levels of the selection or the file", {},
                 editable, actions.showLoudnessAndLevels });

        // Window: panels of the editor
        const auto togglePanel = [&ctx, &w, &s] (const char* key)
        {
            return [&ctx, &w, &s, key]
            {
                ctx.settings.set (key, ! ctx.settings.getBool (key));
                w.sendChangeMessage(); // the editor shows or hides the panel
                s.sendChangeMessage();
            };
        };
        const auto panelShown = [&ctx] (const char* key) { return [&ctx, key] { return ctx.settings.getBool (key); }; };
        c.add ({ "window.effectRack", "Effect Rack",       "Window", "Show or hide the effect rack",       "command + R", waveformShown, togglePanel (keys::showRack),     panelShown (keys::showRack) });
        c.add ({ "window.meters",     "Level Meters",      "Window", "Show or hide the level meters",      {},            nullptr, togglePanel (keys::showMeters),   panelShown (keys::showMeters) });
        c.add ({ "window.spectrum",   "Spectrum Analyzer", "Window", "Show or hide the spectrum analyzer", {},            waveformShown, togglePanel (keys::showSpectrum), panelShown (keys::showSpectrum) });

        const auto toggleSessionPanel = [&ctx, &s] (const char* key)
        {
            return [&ctx, &s, key]
            {
                ctx.settings.set (key, ! ctx.settings.getBool (key));
                s.sendChangeMessage(); // the session page shows or hides the panel
            };
        };
        c.add ({ "window.mixer",        "Mixer",         "Window", "Show or hide the mixer under the tracks", "shift + X",
                 [&s] { return s.isActive(); }, toggleSessionPanel (keys::showMixer), panelShown (keys::showMixer) });
        c.add ({ "window.trackEffects", "Track Effects", "Window", "Show or hide the effects of the selected track", {},
                 [&s] { return s.isActive(); }, toggleSessionPanel (keys::showTrackEffects), panelShown (keys::showTrackEffects) });

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
