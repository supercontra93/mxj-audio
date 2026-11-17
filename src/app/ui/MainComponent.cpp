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

    MainComponent::MainComponent (AppContext& ctx, Workspace& w) : context (ctx), workspace (w)
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

        workspace.addChangeListener (this);
        context.audio.getDeviceManager().addChangeListener (this);
        workspace.onStatusMessage = [this] (const juce::String& m) { setStatusMessage (m); };

        refreshTexts();
        setSize (1280, 800);
    }

    MainComponent::~MainComponent()
    {
        workspace.onStatusMessage = nullptr;
        workspace.removeChangeListener (this);
        context.audio.getDeviceManager().removeChangeListener (this);
        context.commands.onCommandPerformed = nullptr;
        commandManager.setFirstCommandTarget (nullptr);
    }

    void MainComponent::refreshTexts()
    {
        startPage = nullptr;
        editorPage = nullptr;
        shownDocument = nullptr;
        showPage();

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

        if (editorPage != nullptr)
            editorPage->setBounds (area);
    }

    void MainComponent::showPage()
    {
        const auto* doc = workspace.getDocument();
        const bool pageExists = doc != nullptr ? editorPage != nullptr : startPage != nullptr;

        if (pageExists && doc == shownDocument)
            return;

        shownDocument = doc;
        auto run = [this] (const CommandId& id) { runCommand (id); };

        if (doc != nullptr)
        {
            startPage = nullptr;
            editorPage = std::make_unique<EditorPage> (workspace, context.commands, run);
            addAndMakeVisible (*editorPage);
        }
        else
        {
            editorPage = nullptr;
            startPage = std::make_unique<StartPage> (context.commands, run,
                                                     [this] { return context.audio.describeDevice(); });
            addAndMakeVisible (*startPage);
        }

        resized();

        if (onDocumentChanged != nullptr)
            onDocumentChanged();
    }

    void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        showPage();
        commandManager.commandStatusChanged();

        if (startPage != nullptr)
            startPage->repaint(); // shows the audio device
    }

    void MainComponent::setStatusMessage (const juce::String& message)
    {
        statusBar.setMessage (message);
    }

    bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
    {
        return files.size() == 1 && audioFormats::canOpen (juce::File (files[0]));
    }

    void MainComponent::filesDropped (const juce::StringArray& files, int, int)
    {
        dragOver = false;
        repaint();

        if (! files.isEmpty())
            workspace.openFile (juce::File (files[0]));
    }

    void MainComponent::paintOverChildren (juce::Graphics& g)
    {
        if (! dragOver)
            return;

        g.setColour (palette::accent.withAlpha (0.12f));
        g.fillRect (getLocalBounds().withTrimmedTop (menuBarHeight));
        g.setColour (palette::accent);
        g.drawRect (getLocalBounds().withTrimmedTop (menuBarHeight), 2);
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
