// SPDX-License-Identifier: AGPL-3.0-only
#include "app/ui/MainWindow.h"
#include "app/ui/MainComponent.h"
#include "app/theme/Palette.h"
#include "core/Version.h"
#include "services/settings/SettingKeys.h"

namespace mxj
{
    MainWindow::MainWindow (AppContext& ctx, Workspace& w, std::function<void()> quitFunction)
        : DocumentWindow (version::name, palette::background, DocumentWindow::allButtons),
          context (ctx),
          workspace (w),
          onQuit (std::move (quitFunction))
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);
        setResizeLimits (800, 500, 10000, 10000);

        auto content = std::make_unique<MainComponent> (context, workspace);
        content->onDocumentChanged = [this] { updateTitle(); };
        mainComponent = content.get();
        setContentOwned (content.release(), false);

        const auto state = context.settings.getString (keys::windowState);
        if (state.isEmpty() || ! restoreWindowStateFromString (state))
            centreWithSize (1280, 800);

        setVisible (true);
        addKeyListener (mainComponent->getCommandManager().getKeyMappings());
    }

    MainWindow::~MainWindow()
    {
        if (mainComponent != nullptr)
            removeKeyListener (mainComponent->getCommandManager().getKeyMappings());
    }

    void MainWindow::updateTitle()
    {
        const auto* doc = workspace.getDocument();
        const auto title = doc != nullptr ? (doc->isModified() ? "* " : "") + doc->getDisplayName() + " - " + version::name
                                          : juce::String (version::name);
        if (title != getName())
            setName (title);
    }

    void MainWindow::closeButtonPressed()
    {
        if (onQuit != nullptr)
            onQuit();
    }

    void MainWindow::refreshTexts()
    {
        if (mainComponent != nullptr)
            mainComponent->refreshTexts();
    }

    void MainWindow::storeWindowState()
    {
        context.settings.set (keys::windowState, getWindowStateAsString());
    }
}
