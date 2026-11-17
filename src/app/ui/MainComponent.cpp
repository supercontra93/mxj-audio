// SPDX-License-Identifier: AGPL-3.0-only
#include "app/ui/MainComponent.h"
#include "app/theme/Palette.h"

namespace mxj
{
    namespace
    {
        constexpr int menuBarHeight = 26;
        constexpr int statusBarHeight = 24;
    }

    MainComponent::MainComponent (AppContext& ctx) : context (ctx)
    {
        for (const auto* c : context.commands.getAll())
            commandOrder.add (c->id);

        commandManager.setFirstCommandTarget (this);

        addAndMakeVisible (menuBar);
        addAndMakeVisible (statusBar);

        // Keep menus and buttons in step with what can run right now.
        context.commands.onCommandPerformed = [this] (const CommandId&)
        {
            commandManager.commandStatusChanged();
            menuItemsChanged();
        };

        refreshTexts();
        setSize (1280, 800);
    }

    MainComponent::~MainComponent()
    {
        context.commands.onCommandPerformed = nullptr;
        commandManager.setFirstCommandTarget (nullptr);
    }

    void MainComponent::refreshTexts()
    {
        startPage = std::make_unique<StartPage> (context.commands, [this] (const CommandId& id) { runCommand (id); });
        addAndMakeVisible (*startPage);

        statusBar.refreshTexts();

        // Command names are translated when JUCE asks for them, so re-register.
        commandManager.clearCommands();
        commandManager.registerAllCommandsForTarget (this);
        menuItemsChanged();
        resized();
    }

    void MainComponent::paint (juce::Graphics& g)
    {
        g.fillAll (palette::background);
    }

    void MainComponent::resized()
    {
        auto area = getLocalBounds();
        menuBar.setBounds (area.removeFromTop (menuBarHeight));
        statusBar.setBounds (area.removeFromBottom (statusBarHeight));

        if (startPage != nullptr)
            startPage->setBounds (area);
    }

    juce::StringArray MainComponent::getMenuBarNames()
    {
        juce::StringArray names;
        for (const auto& n : getMenuNames())
            names.add (TRANS (n));

        return names;
    }

    juce::PopupMenu MainComponent::getMenuForIndex (int menuIndex, const juce::String&)
    {
        juce::PopupMenu menu;
        const auto category = getMenuNames()[menuIndex];

        for (const auto* c : context.commands.getByCategory (category))
            menu.addCommandItem (&commandManager, toJuceId (c->id));

        if (menu.getNumItems() == 0)
            menu.addItem (juce::PopupMenu::Item (TRANS ("Coming in a later version")).setEnabled (false));

        return menu;
    }

    void MainComponent::getAllCommands (juce::Array<juce::CommandID>& ids)
    {
        for (int i = 0; i < commandOrder.size(); ++i)
            ids.add (i + 1);
    }

    void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& result)
    {
        const auto* c = fromJuceId (id);
        if (c == nullptr)
            return;

        result.setInfo (TRANS (c->name), TRANS (c->description), TRANS (c->category), 0);
        result.setActive (context.commands.isEnabled (c->id));
        result.setTicked (c->isTicked != nullptr && c->isTicked());

        if (c->defaultShortcut.isNotEmpty())
        {
            const auto key = juce::KeyPress::createFromDescription (c->defaultShortcut);
            result.addDefaultKeypress (key.getKeyCode(), key.getModifiers());
        }
    }

    bool MainComponent::perform (const InvocationInfo& info)
    {
        if (const auto* c = fromJuceId (info.commandID))
            return context.commands.perform (c->id);

        return false;
    }

    void MainComponent::runCommand (const CommandId& id)
    {
        commandManager.invokeDirectly (toJuceId (id), true);
    }

    juce::CommandID MainComponent::toJuceId (const CommandId& id) const
    {
        return commandOrder.indexOf (id) + 1;
    }

    const CommandInfo* MainComponent::fromJuceId (juce::CommandID id) const
    {
        return context.commands.find (commandOrder[id - 1]);
    }
}
