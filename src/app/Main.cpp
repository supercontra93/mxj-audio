// SPDX-License-Identifier: AGPL-3.0-only
//
// MXJ AUDIO - free and open source audio editor
// Copyright (C) 2026 MXJ AUDIO contributors
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU Affero General Public License version 3 as published by
// the Free Software Foundation. See the LICENSE file for details.

#include "app/AppCommands.h"
#include "app/AppContext.h"
#include "app/Workspace.h"
#include "app/effects/EffectDialog.h"
#include "app/effects/StatsDialog.h"
#include "app/plugins/PluginCommands.h"
#include "app/plugins/PluginSystem.h"
#include "app/plugins/PluginWindow.h"
#include "app/plugins/PluginsPage.h"
#include "app/session/SessionWorkspace.h"
#include "app/settings/AudioSettingsPanel.h"
#include "app/theme/Fonts.h"
#include "app/theme/GrafitLookAndFeel.h"
#include "app/ui/AboutPanel.h"
#include "app/ui/Dialogs.h"
#include "app/ui/MainComponent.h"
#include "app/ui/MainWindow.h"
#include "core/Version.h"
#include "core/audio/DerivedSources.h"
#include "services/i18n/Localisation.h"
#include "services/logging/Log.h"
#include "services/settings/SettingKeys.h"
#include "services/updates/UpdatePolicy.h"

#include <MxjResources.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    namespace
    {
        juce::String translationFor (const juce::String& languageCode)
        {
            if (languageCode == "tr")
                return juce::String::fromUTF8 (mxjres::tr_txt, mxjres::tr_txtSize);

            return {};
        }

        /** Reads "--name value" from the command line. */
        juce::String getArgumentValue (const juce::StringArray& args, const juce::String& name)
        {
            const auto i = args.indexOf (name);
            return i >= 0 ? args[i + 1].unquoted() : juce::String();
        }

        /** The first argument that is an existing file (for "Open with" and drag onto the program). */
        juce::File getFileArgument (const juce::StringArray& args)
        {
            for (int i = 0; i < args.size(); ++i)
            {
                if (args[i] == "--data-dir")
                {
                    ++i;
                    continue;
                }

                const auto arg = args[i].unquoted();
                if (arg.isEmpty() || arg.startsWith ("--"))
                    continue;

                const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (arg);
                if (file.existsAsFile())
                    return file;
            }

            return {};
        }
    }

    /** The application object: starts and stops everything.

        Command line options:
          --data-dir <folder>  keep settings and logs in this folder instead of %APPDATA%
          --smoke-test         start up fully, then quit after a moment with exit code 0
                               (used by CI to prove the program starts). With a file
                               argument it also opens the file and waits until it has loaded.
          <file>               open this audio file
    */
    class MxjApplication : public juce::JUCEApplication
    {
    public:
        const juce::String getApplicationName() override    { return version::name; }
        const juce::String getApplicationVersion() override { return version::string; }
        bool moreThanOneInstanceAllowed() override          { return true; }

        void initialise (const juce::String& commandLine) override
        {
            const auto args = juce::StringArray::fromTokens (commandLine, true);
            const auto dataDir = getArgumentValue (args, "--data-dir");

            context = std::make_unique<AppContext> (dataDir.isNotEmpty() ? AppPaths (juce::File::getCurrentWorkingDirectory().getChildFile (dataDir))
                                                                           : AppPaths());
            context->paths.createFolders();

            Log::start (context->paths.getLogsFolder());
            Log::info ("Starting. Data folder: " + context->paths.getRoot().getFullPathName());
            Log::info (juce::String ("Update check: ") + (updates::enabled ? "built in" : "disabled in this build"));

            if (! context->settings.load())
                Log::info (context->settings.wasLoadedFromDamagedFile()
                               ? "Settings file was damaged; a copy was kept and defaults are used"
                               : "No settings file yet; using defaults");

            fonts::setTextScale ((float) context->settings.getDouble (keys::textSize));

            Localisation::apply (context->settings.getString (keys::language),
                                 translationFor (context->settings.getString (keys::language)));

            const auto deviceError = context->audio.initialise (context->settings.getString (keys::audioDeviceState));
            Log::info ("Audio device: " + context->audio.describeDevice()
                       + (deviceError.isNotEmpty() ? " (" + deviceError + ")" : juce::String()));

            // Before the workspace: its effect rack may hold plugin effects.
            pluginSystem = std::make_unique<PluginSystem> (*context);

            const auto recoverable = context->findRecoverableRun();
            workspace = std::make_unique<Workspace> (*context);
            session = std::make_unique<SessionWorkspace> (*context, *workspace);
            workspace->openProject = [this] (const juce::File& f) { session->openProject (f); };
            workspace->onDocumentSet = [this] { session->setActive (false); };

            lookAndFeel = std::make_unique<GrafitLookAndFeel>();
            juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());
            tooltipWindow = std::make_unique<juce::TooltipWindow> (nullptr, 600);

            registerAppCommands (*context, *workspace, *session, {
                [this] { systemRequestedQuit(); },
                [this] { AboutPanel::show (mainWindow.get()); },
                [this] (const juce::String& code) { setLanguage (code); },
                [this] { showAudioSettings(); },
                [this] (const juce::String& effectId) { showEffect (effectId); },
                [this] { showLoudnessAndLevels(); },
                [this] (float scale) { setTextScale (scale); } });

            registerPlugins();
            pluginSystem->onPluginsChanged = [this]
            {
                registerPlugins();
                if (mainWindow != nullptr && mainWindow->getMainComponent() != nullptr)
                    mainWindow->getMainComponent()->commandsChanged();
            };
            pluginSystem->onScanFinished = [this] (int found, int blocked)
            {
                if (mainWindow != nullptr && mainWindow->getMainComponent() != nullptr)
                    mainWindow->getMainComponent()->setStatusMessage (TRANS ("Plugin scan finished") + ": " + juce::String (found) + " "
                                                                      + TRANS ("plugins found") + ", " + juce::String (blocked) + " "
                                                                      + TRANS ("blocked"));
            };

            mainWindow = std::make_unique<MainWindow> (*context, *workspace, *session, [this] { systemRequestedQuit(); });

            Log::info ("Main window open");

            const auto file = getFileArgument (args);
            if (file != juce::File())
                workspace->openFile (file);

            if (args.contains ("--smoke-test"))
                runSmokeTest (file != juce::File());
            else if (recoverable != juce::File())
                workspace->offerRecovery (recoverable);
        }

        void shutdown() override
        {
            if (mainWindow != nullptr)
                mainWindow->storeWindowState();

            if (context != nullptr)
                context->settings.set (keys::audioDeviceState, context->audio.getState());

            if (workspace != nullptr)
                workspace->storeSettings();

            if (pluginSystem != nullptr)
                pluginSystem->storeSettings();

            if (context != nullptr && ! context->settings.save())
                Log::error ("Could not save settings to " + context->settings.getFile().getFullPathName());

            PluginsPage::closeAll();
            PluginWindow::closeAll();
            mainWindow = nullptr;
            session = nullptr;
            workspace = nullptr;
            pluginSystem = nullptr; // after everything that may hold plugin effects

            if (context != nullptr)
                context->audio.shutdown();

            tooltipWindow = nullptr;
            juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
            lookAndFeel = nullptr;

            Log::info ("Stopped");
            Log::stop();
            context = nullptr;
        }

        void systemRequestedQuit() override
        {
            if (workspace == nullptr || smokeTestStart != 0)
            {
                quit();
                return;
            }

            // Unsaved changes: ask first (stops a recording, keeping the take),
            // for the multitrack session and then for the audio file.
            session->askToSaveChanges ([this]
            {
                if (workspace != nullptr)
                    workspace->askToSaveChanges ([] { juce::JUCEApplication::quit(); });
            });
        }

        void anotherInstanceStarted (const juce::String&) override {}

    private:
        void registerPlugins()
        {
            registerPluginCommands (*context, *workspace, *session, *pluginSystem, {
                [this] (const juce::String& effectId) { showEffect (effectId); },
                [this] { showPluginsPage(); },
                [this] { return static_cast<juce::Component*> (mainWindow.get()); } });
        }

        void showPluginsPage()
        {
            PluginsPage::show (*pluginSystem, [this] (const juce::String& id) { usePlugin (id); });
        }

        /** Opens an MXJ plugin from the plugin page. */
        void usePlugin (const juce::String& id)
        {
            const auto* p = pluginSystem->getLua().find (id);
            if (p == nullptr)
                return;

            const auto type = p->info.type;
            if (type == plugins::LuaPluginInfo::Type::effect || type == plugins::LuaPluginInfo::Type::generator)
            {
                if (session->isActive() || ! workspace->canEdit())
                {
                    dialogs::showInfo (p->info.name, TRANS ("Open an audio file in the waveform editor first: this plugin changes the selection or the whole file."));
                    return;
                }

                showEffect ("lua:" + id);
                return;
            }

            runLuaPlugin (*context, *workspace, *pluginSystem, id, mainWindow.get());
        }

        void showAudioSettings()
        {
            AudioSettingsPanel::show (context->audio, context->settings, mainWindow.get(), [this]
            {
                if (context == nullptr)
                    return;

                context->settings.set (keys::audioDeviceState, context->audio.getState());
                Log::info ("Audio device: " + context->audio.describeDevice());
            });
        }

        void showEffect (const juce::String& effectId)
        {
            if (effectId == "noiseReduction" && ! workspace->hasNoisePrint())
            {
                dialogs::showInfo (TRANS ("Noise Reduction (Noise Print)"),
                                   TRANS ("First tell MXJ AUDIO what the noise sounds like: select a part that holds only the background noise "
                                          "(a pause, at least half a second) and choose Effects > Noise Reduction and Repair > Capture Noise Print (Shift+P). "
                                          "Then select the part to clean, or nothing for the whole file, and open Noise Reduction again."));
                return;
            }

            if (const auto* type = dsp::catalogue::find (effectId))
                EffectDialog::show (*workspace, context->presets, *type, mainWindow.get());
        }

        void showLoudnessAndLevels()
        {
            const auto what = workspace->hasSelection() ? TRANS ("Selection") : TRANS ("Whole file");
            workspace->analyse ([this, what] (const dsp::AudioStats& stats)
            {
                if (mainWindow != nullptr)
                    StatsDialog::show (stats, what, mainWindow.get());
            });
        }

        void setTextScale (float scale)
        {
            fonts::setTextScale (scale);
            context->settings.set (keys::textSize, (double) scale);
            Log::info ("Text size set to " + juce::String (scale, 2));

            juce::MessageManager::callAsync ([this]
            {
                if (mainWindow != nullptr)
                    mainWindow->refreshTexts();
            });
        }

        /** Proves in CI that the program starts, and (with a file) that it opens
            audio: waits until the file has loaded, then quits with exit code 0.
            The test fails by timeout or by a missing "Smoke test passed" line. */
        void runSmokeTest (bool withFile)
        {
            Log::info (withFile ? "Smoke test: waiting for the file to load" : "Smoke test: quitting in 2 seconds");
            smokeTestStart = juce::Time::getMillisecondCounter();
            smokeTestPoll (withFile);
        }

        void smokeTestPoll (bool withFile)
        {
            const auto elapsed = juce::Time::getMillisecondCounter() - smokeTestStart;
            const auto* doc = workspace != nullptr ? workspace->getDocument() : nullptr;

            if (withFile && doc != nullptr && doc->isLoading() && elapsed < 50000)
            {
                juce::Timer::callAfterDelay (100, [this, withFile] { smokeTestPoll (withFile); });
                return;
            }

            if (! withFile && elapsed < 2000)
            {
                juce::Timer::callAfterDelay (2000 - (int) elapsed, [this] { smokeTestPoll (false); });
                return;
            }

            if (withFile)
            {
                if (doc == nullptr || ! doc->isReady())
                {
                    Log::error ("Smoke test failed: the file did not load");
                    setApplicationReturnValue (1);
                    quit();
                    return;
                }

                Log::info ("Smoke test: file loaded, " + juce::String (doc->getAudio().getNumFrames()) + " frames in "
                           + juce::String (doc->getLoadSeconds(), 2) + " s");
                smokeTestEditAndExport();
                return;
            }

            // Phase 4: File > New Audio File without the dialog.
            workspace->newFile (44100.0, 1);
            const auto* created = workspace->getDocument();
            if (created == nullptr || ! created->isReady() || created->getNumChannels() != 1
                || ! juce::approximatelyEqual (created->getSampleRate(), 44100.0) || created->isModified())
            {
                Log::error ("Smoke test failed: New Audio File");
                setApplicationReturnValue (1);
                quit();
                return;
            }

            Log::info ("Smoke test: new file OK");
            smokeTestMultitrack();
        }

        void smokeTestFail (const juce::String& what)
        {
            Log::error ("Smoke test failed: " + what);
            setApplicationReturnValue (1);
            quit();
        }

        /** Calls `next` once the session has no background job. */
        void smokeTestWhenSessionIdle (std::function<void()> next)
        {
            if (session->isBusy() && juce::Time::getMillisecondCounter() - smokeTestStart < 80000)
            {
                juce::Timer::callAfterDelay (100, [this, next] { smokeTestWhenSessionIdle (next); });
                return;
            }

            next();
        }

        /** Phase 5: a new session, two files imported (one at another sample
            rate), a bus, the mixdown exported and checked, the project saved,
            closed and opened again. */
        void smokeTestMultitrack()
        {
            session->newSession (48000.0);
            if (! session->isActive() || session->getSession().getNumAudioTracks() != 4)
                return smokeTestFail ("New Multitrack Session");

            const auto writeTone = [] (const juce::File& file, double rate, int seconds)
            {
                juce::AudioBuffer<float> audio (2, seconds * (int) rate);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < audio.getNumSamples(); ++i)
                        audio.setSample (c, i, 0.25f * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / rate));

                file.deleteFile();
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
                auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                               .withSampleRate (rate)
                                                               .withNumChannels (2)
                                                               .withBitsPerSample (16));
                return writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
            };

            // Three seconds at 44.1 kHz (converted to 48 kHz on import, so copied into the
            // project), and 25 seconds at 48 kHz (kept in place; longer than one block).
            const auto tone = context->paths.getRoot().getChildFile ("smoke-tone.wav");
            const auto longTone = context->paths.getRoot().getChildFile ("smoke-long.wav");
            if (! writeTone (tone, 44100.0, 3) || ! writeTone (longTone, 48000.0, 25))
                return smokeTestFail ("could not write the test tones");

            const auto first = session->getSession().tracks[0].id;
            const auto second = session->getSession().tracks[1].id;
            session->importFiles ({ tone }, first, 0);

            smokeTestWhenSessionIdle ([this, longTone, second]
            {
                session->importFiles ({ longTone }, second, 48000);

                smokeTestWhenSessionIdle ([this, second]
                {
                    session->addBus();
                    const auto busId = session->getSelectedTrack();
                    session->setTrackOutput (second, busId);

                    const auto& s = session->getSession();
                    int clips = 0;
                    for (const auto& t : s.tracks)
                        clips += (int) t.clips.size();

                    // The second clip starts after 1 s and lasts 25 s: 26 s in all.
                    const auto expected = (juce::int64) 26 * 48000;
                    if (clips != 2 || s.tracks.size() != 5 || std::abs (s.getLength() - expected) > 2)
                        return smokeTestFail ("import (" + juce::String (clips) + " clips, " + juce::String (s.getLength()) + " frames)");

                    ExportSettings settings;
                    settings.file = context->paths.getRoot().getChildFile ("smoke-mixdown.wav");
                    settings.format = ExportFormat::wav;
                    settings.bitsPerSample = 24;
                    session->exportMixdown (settings);

                    smokeTestWhenSessionIdle ([this, settings, expected]
                    {
                        AudioFileInfo info;
                        juce::String error;
                        if (! audioFormats::readInfo (settings.file, info, error) || std::abs (info.numFrames - expected) > 2)
                            return smokeTestFail ("the mixdown is wrong " + error);

                        const auto project = context->paths.getRoot().getChildFile ("smoke-session.mxj");
                        session->saveAs (project, [this, project]
                        {
                            if (session->isModified())
                                return smokeTestFail ("the project was not saved");

                            session->closeSession();
                            if (session->hasSession())
                                return smokeTestFail ("Close session");

                            session->openProject (project);

                            smokeTestWhenSessionIdle ([this]
                            {
                                if (! session->hasSession())
                                    return smokeTestFail ("the project did not open");

                                const auto& opened = session->getSession();
                                int openedClips = 0;
                                for (const auto& t : opened.tracks)
                                    openedClips += (int) t.clips.size();

                                if (opened.tracks.size() != 5 || openedClips != 2 || opened.tracks[1].outputId != opened.tracks[4].id)
                                    return smokeTestFail ("the opened project is wrong");

                                // No clip may have come back as silence (a file reported missing).
                                for (const auto& t : opened.tracks)
                                    for (const auto& clip : t.clips)
                                        for (const auto& block : clip.audio.getBlocks())
                                            if (dynamic_cast<const SilenceSource*> (block.source.get()) != nullptr)
                                                return smokeTestFail ("audio of clip " + clip.name + " is missing after opening");

                                Log::info ("Smoke test: multitrack OK");
                                Log::info ("Smoke test passed");
                                quit();
                            });
                        });
                    });
                });
            });
        }

        /** Phase 2 part of the smoke test: cut, undo and export through the
            commands, as a user would, then check the exported file. */
        void smokeTestEditAndExport()
        {
            auto* doc = workspace->getDocument();
            const auto length = doc->getAudio().getNumFrames();
            const auto second = (juce::int64) doc->getSampleRate();

            workspace->setSelection ({ 0, second });
            context->commands.perform ("edit.cut");
            const bool cutOk = doc->getAudio().getNumFrames() == length - second && doc->isModified();
            context->commands.perform ("edit.undo");
            const bool undoOk = doc->getAudio().getNumFrames() == length && ! doc->isModified();

            if (! cutOk || ! undoOk)
            {
                Log::error ("Smoke test failed: cut " + juce::String (cutOk ? "ok" : "wrong") + ", undo " + juce::String (undoOk ? "ok" : "wrong"));
                setApplicationReturnValue (1);
                quit();
                return;
            }

            smokeTestEffects (length);
        }

        /** Phase 3 part of the smoke test: a compressor on the first two seconds,
            then Match Loudness (-16 LUFS) on the first 30 seconds, both in the
            background as from the dialogs. The length must stay the same, and two
            undo steps must bring back the original. Then the export follows. */
        void smokeTestEffects (juce::int64 length)
        {
            auto* doc = workspace->getDocument();
            const auto second = (juce::int64) doc->getSampleRate();

            workspace->setSelection ({ 0, juce::jmin (length, 2 * second) });
            workspace->applyEffect (dsp::EffectSettings::defaultsFor ("compressor"));

            smokeTestWhenIdle ([this, length, second]
            {
                auto* d = workspace->getDocument();
                const bool compOk = d->getAudio().getNumFrames() == length && d->isModified();

                auto loudness = dsp::EffectSettings::defaultsFor ("matchLoudness");
                workspace->setSelection ({ 0, juce::jmin (length, 30 * second) });
                workspace->applyEffect (loudness);

                smokeTestWhenIdle ([this, length, compOk]
                {
                    auto* d2 = workspace->getDocument();
                    const bool loudOk = d2->getAudio().getNumFrames() == length;
                    context->commands.perform ("edit.undo");
                    context->commands.perform ("edit.undo");
                    const bool undoOk = d2->getAudio().getNumFrames() == length && ! d2->isModified();

                    if (! compOk || ! loudOk || ! undoOk)
                    {
                        Log::error ("Smoke test failed: compressor " + juce::String (compOk ? "ok" : "wrong")
                                    + ", loudness " + juce::String (loudOk ? "ok" : "wrong")
                                    + ", undo " + juce::String (undoOk ? "ok" : "wrong"));
                        setApplicationReturnValue (1);
                        quit();
                        return;
                    }

                    Log::info ("Smoke test: effects OK");
                    smokeTestRepair (length);
                });
            });
        }

        /** Phase 4: spectral healing of a band, noise print and noise reduction,
            click removal, each undone again. */
        void smokeTestRepair (juce::int64 length)
        {
            const auto second = (juce::int64) workspace->getDocument()->getSampleRate();
            workspace->setSpectralSelection ({ second, juce::jmin (length, 2 * second) }, { 1000.0, 4000.0 });
            const bool bandOk = workspace->hasSpectralSelection();
            workspace->healSelection();

            smokeTestWhenIdle ([this, length, second, bandOk]
            {
                auto* d = workspace->getDocument();
                const bool healOk = bandOk && d->getAudio().getNumFrames() == length && d->isModified()
                                 && workspace->hasSpectralSelection(); // the spectral selection stays

                workspace->setSelection ({ 0, juce::jmin (length, second / 2) });
                workspace->captureNoisePrint();

                smokeTestWhenIdle ([this, length, second, healOk]
                {
                    const bool printOk = workspace->hasNoisePrint();
                    workspace->setSelection ({ second, juce::jmin (length, 3 * second) });
                    workspace->applyEffect (dsp::EffectSettings::defaultsFor ("noiseReduction"));

                    smokeTestWhenIdle ([this, length, healOk, printOk]
                    {
                        workspace->applyEffect (dsp::EffectSettings::defaultsFor ("clickRemoval"));

                        smokeTestWhenIdle ([this, length, healOk, printOk]
                        {
                            auto* d2 = workspace->getDocument();
                            const bool lengthOk = d2->getAudio().getNumFrames() == length;
                            for (int i = 0; i < 3; ++i)
                                context->commands.perform ("edit.undo");
                            const bool undoOk = ! d2->isModified();

                            if (! healOk || ! printOk || ! lengthOk || ! undoOk)
                            {
                                Log::error ("Smoke test failed: heal " + juce::String (healOk ? "ok" : "wrong")
                                            + ", noise print " + juce::String (printOk ? "ok" : "wrong")
                                            + ", length " + juce::String (lengthOk ? "ok" : "wrong")
                                            + ", undo " + juce::String (undoOk ? "ok" : "wrong"));
                                setApplicationReturnValue (1);
                                quit();
                                return;
                            }

                            Log::info ("Smoke test: repair OK");
                            smokeTestPlugins (length);
                        });
                    });
                });
            });
        }

        /** Phase 6: an example MXJ plugin is installed from the local catalogue
            (checked with SHA-256), shows up in the Effects menu, and is applied
            to two seconds and undone. */
        void smokeTestPlugins (juce::int64 length)
        {
            const juce::String id ("mxj.examples.tremolo");
            juce::String error;
            if (! pluginSystem->installFromCatalogue (id, error))
                return smokeTestFail ("installing a plugin from the catalogue: " + error);

            if (dsp::catalogue::find ("lua:" + id) == nullptr || ! context->commands.contains ("effects.lua:" + id))
                return smokeTestFail ("the installed plugin is not in the Effects menu");

            const auto second = (juce::int64) workspace->getDocument()->getSampleRate();
            workspace->setSelection ({ 0, juce::jmin (length, 2 * second) });
            workspace->applyEffect (dsp::EffectSettings::defaultsFor ("lua:" + id));

            smokeTestWhenIdle ([this, length]
            {
                auto* d = workspace->getDocument();
                const bool applied = d->getAudio().getNumFrames() == length && d->isModified();
                context->commands.perform ("edit.undo");

                if (! applied || d->isModified())
                    return smokeTestFail ("applying the MXJ plugin " + juce::String (applied ? "(undo)" : ""));

                Log::info ("Smoke test: plugins OK");
                smokeTestExport (length);
            });
        }

        /** Calls `next` once the background job has finished. */
        void smokeTestWhenIdle (std::function<void()> next)
        {
            if (workspace->isBusy() && juce::Time::getMillisecondCounter() - smokeTestStart < 80000)
            {
                juce::Timer::callAfterDelay (100, [this, next] { smokeTestWhenIdle (next); });
                return;
            }

            next();
        }

        void smokeTestExport (juce::int64 length)
        {
            workspace->setSelection ({});
            ExportSettings settings;
            settings.file = context->paths.getRoot().getChildFile ("smoke-export.flac");
            settings.format = ExportFormat::flac;
            settings.bitsPerSample = 16;
            workspace->exportFile (settings);
            smokeTestWaitForExport (settings.file, length);
        }

        void smokeTestWaitForExport (const juce::File& file, juce::int64 expectedFrames)
        {
            if (workspace->isBusy() && juce::Time::getMillisecondCounter() - smokeTestStart < 80000)
            {
                juce::Timer::callAfterDelay (100, [this, file, expectedFrames] { smokeTestWaitForExport (file, expectedFrames); });
                return;
            }

            AudioFileInfo info;
            juce::String error;
            if (! audioFormats::readInfo (file, info, error) || info.numFrames != expectedFrames)
            {
                Log::error ("Smoke test failed: the exported file is wrong " + error);
                setApplicationReturnValue (1);
                quit();
                return;
            }

            Log::info ("Smoke test: edit and export OK");
            Log::info ("Smoke test passed");
            quit();
        }

        void setLanguage (const juce::String& code)
        {
            context->settings.set (keys::language, code);
            Localisation::apply (code, translationFor (code));
            Log::info ("Language set to " + code);

            // Rebuild the UI after the current command has finished.
            juce::MessageManager::callAsync ([this]
            {
                if (mainWindow != nullptr)
                    mainWindow->refreshTexts();
            });
        }

        std::unique_ptr<AppContext> context;
        std::unique_ptr<PluginSystem> pluginSystem;
        std::unique_ptr<Workspace> workspace;
        std::unique_ptr<SessionWorkspace> session;
        std::unique_ptr<GrafitLookAndFeel> lookAndFeel;
        std::unique_ptr<juce::TooltipWindow> tooltipWindow;
        std::unique_ptr<MainWindow> mainWindow;
        juce::uint32 smokeTestStart = 0;
    };
}

START_JUCE_APPLICATION (mxj::MxjApplication)
