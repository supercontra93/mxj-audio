// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "app/AppContext.h"
#include "app/AppCommands.h"
#include "app/ui/StartPage.h"
#include "app/ui/StatusBar.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** Content of the main window: menu bar, page area, status bar.

        Bridges our CommandRegistry to JUCE's ApplicationCommandManager so menus
        and keyboard shortcuts both run registry commands.
    */
    class MainComponent : public juce::Component,
                          public juce::MenuBarModel,
                          public juce::ApplicationCommandTarget
    {
    public:
        explicit MainComponent (AppContext& context);
        ~MainComponent() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        void refreshTexts();

        juce::ApplicationCommandManager& getCommandManager() noexcept { return commandManager; }

        // MenuBarModel
        juce::StringArray getMenuBarNames() override;
        juce::PopupMenu getMenuForIndex (int menuIndex, const juce::String& menuName) override;
        void menuItemSelected (int, int) override {}

        // ApplicationCommandTarget
        juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
        void getAllCommands (juce::Array<juce::CommandID>&) override;
        void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
        bool perform (const InvocationInfo&) override;

    private:
        /** JUCE needs integer ids; ours are strings. Index + 1 maps between them. */
        juce::CommandID toJuceId (const CommandId& id) const;
        const CommandInfo* fromJuceId (juce::CommandID id) const;
        void runCommand (const CommandId& id);

        AppContext& context;
        juce::ApplicationCommandManager commandManager;
        juce::StringArray commandOrder;

        juce::MenuBarComponent menuBar { this };
        std::unique_ptr<StartPage> startPage;
        StatusBar statusBar;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
    };
}
