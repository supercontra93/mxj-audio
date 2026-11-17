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
#include "app/settings/AudioSettingsPanel.h"
#include "app/theme/GrafitLookAndFeel.h"
#include "app/ui/AboutPanel.h"
#include "app/ui/MainComponent.h"
#include "app/ui/MainWindow.h"
#include "core/Version.h"
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

            Localisation::apply (context->settings.getString (keys::language),
                                 translationFor (context->settings.getString (keys::language)));

            const auto deviceError = context->audio.initialise (context->settings.getString (keys::audioDeviceState));
            Log::info ("Audio device: " + context->audio.describeDevice()
                       + (deviceError.isNotEmpty() ? " (" + deviceError + ")" : juce::String()));

            const auto recoverable = context->findRecoverableRun();
            workspace = std::make_unique<Workspace> (*context);

            lookAndFeel = std::make_unique<GrafitLookAndFeel>();
            juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());
            tooltipWindow = std::make_unique<juce::TooltipWindow> (nullptr, 600);

            registerAppCommands (*context, *workspace, {
                [this] { systemRequestedQuit(); },
                [this] { AboutPanel::show (mainWindow.get()); },
                [this] (const juce::String& code) { setLanguage (code); },
                [this] { showAudioSettings(); } });

            mainWindow = std::make_unique<MainWindow> (*context, *workspace, [this] { systemRequestedQuit(); });

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

            if (context != nullptr && ! context->settings.save())
                Log::error ("Could not save settings to " + context->settings.getFile().getFullPathName());

            mainWindow = nullptr;
            workspace = nullptr;

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

            // Unsaved changes: ask first (stops a recording, keeping the take).
            workspace->askToSaveChanges ([] { juce::JUCEApplication::quit(); });
        }

        void anotherInstanceStarted (const juce::String&) override {}

    private:
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

            Log::info ("Smoke test passed");
            quit();
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
        std::unique_ptr<Workspace> workspace;
        std::unique_ptr<GrafitLookAndFeel> lookAndFeel;
        std::unique_ptr<juce::TooltipWindow> tooltipWindow;
        std::unique_ptr<MainWindow> mainWindow;
        juce::uint32 smokeTestStart = 0;
    };
}

START_JUCE_APPLICATION (mxj::MxjApplication)
