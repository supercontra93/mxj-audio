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
#include "app/theme/GrafitLookAndFeel.h"
#include "app/ui/AboutPanel.h"
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
    }

    /** The application object: starts and stops everything.

        Command line options:
          --data-dir <folder>  keep settings and logs in this folder instead of %APPDATA%
          --smoke-test         start up fully, then quit after a moment with exit code 0
                               (used by CI to prove the program starts)
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

            lookAndFeel = std::make_unique<GrafitLookAndFeel>();
            juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());
            tooltipWindow = std::make_unique<juce::TooltipWindow> (nullptr, 600);

            registerAppCommands (*context, {
                [this] { systemRequestedQuit(); },
                [this] { AboutPanel::show (mainWindow.get()); },
                [this] (const juce::String& code) { setLanguage (code); } });

            mainWindow = std::make_unique<MainWindow> (*context, [this] { systemRequestedQuit(); });

            Log::info ("Main window open");

            if (args.contains ("--smoke-test"))
            {
                Log::info ("Smoke test: quitting in 2 seconds");
                juce::Timer::callAfterDelay (2000, [this]
                {
                    Log::info ("Smoke test passed");
                    systemRequestedQuit();
                });
            }
        }

        void shutdown() override
        {
            if (mainWindow != nullptr)
                mainWindow->storeWindowState();

            if (context != nullptr && ! context->settings.save())
                Log::error ("Could not save settings to " + context->settings.getFile().getFullPathName());

            mainWindow = nullptr;
            tooltipWindow = nullptr;
            juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
            lookAndFeel = nullptr;

            Log::info ("Stopped");
            Log::stop();
            context = nullptr;
        }

        void systemRequestedQuit() override
        {
            quit();
        }

        void anotherInstanceStarted (const juce::String&) override {}

    private:
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
        std::unique_ptr<GrafitLookAndFeel> lookAndFeel;
        std::unique_ptr<juce::TooltipWindow> tooltipWindow;
        std::unique_ptr<MainWindow> mainWindow;
    };
}

START_JUCE_APPLICATION (mxj::MxjApplication)
