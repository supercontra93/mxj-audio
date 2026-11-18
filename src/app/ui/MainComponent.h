// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "app/AppContext.h"
#include "app/AppCommands.h"
#include "app/Workspace.h"
#include "app/editor/EditorPage.h"
#include "app/session/SessionPage.h"
#include "app/session/SessionWorkspace.h"
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
                          public juce::ApplicationCommandTarget,
                          public juce::FileDragAndDropTarget,
                          private juce::ChangeListener
    {
    public:
        MainComponent (AppContext& context, Workspace& workspace, SessionWorkspace& session);
        ~MainComponent() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        void refreshTexts();

        /** Shows a message in the status bar. */
        void setStatusMessage (const juce::String& message);

        /** Called when a file was opened or closed (the window updates its title). */
        std::function<void()> onDocumentChanged;

        // FileDragAndDropTarget: dropping an audio file opens it (in the multitrack
        // view the time line imports it), dropping a project (.mxj) opens the project.
        bool isInterestedInFileDrag (const juce::StringArray& files) override;
        void fileDragEnter (const juce::StringArray&, int, int) override  { dragOver = true; repaint(); }
        void fileDragExit (const juce::StringArray&) override             { dragOver = false; repaint(); }
        void filesDropped (const juce::StringArray& files, int, int) override;
        void paintOverChildren (juce::Graphics&) override;

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
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void showPage();

        /** JUCE needs integer ids; ours are strings. Index + 1 maps between them. */
        juce::CommandID toJuceId (const CommandId& id) const;
        const CommandInfo* fromJuceId (juce::CommandID id) const;
        void runCommand (const CommandId& id);

        AppContext& context;
        Workspace& workspace;
        SessionWorkspace& session;
        juce::ApplicationCommandManager commandManager;
        juce::StringArray commandOrder;

        juce::MenuBarComponent menuBar { this };
        std::unique_ptr<StartPage> startPage;
        std::unique_ptr<EditorPage> editorPage;
        std::unique_ptr<SessionPage> sessionPage;
        const AudioDocument* shownDocument = nullptr;
        bool dragOver = false;
        StatusBar statusBar;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
    };
}
