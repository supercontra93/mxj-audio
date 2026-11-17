// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "app/AppContext.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    class MainComponent;

    /** The main program window. Uses the native Windows title bar; the menu bar
        is drawn inside the window by MainComponent so it follows the theme. */
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (AppContext& context, std::function<void()> onQuit);
        ~MainWindow() override;

        void closeButtonPressed() override;

        /** Rebuilds all visible text after a language change. */
        void refreshTexts();

        /** Saves the window position and size to the settings. */
        void storeWindowState();

    private:
        AppContext& context;
        std::function<void()> onQuit;
        MainComponent* mainComponent = nullptr;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };
}
