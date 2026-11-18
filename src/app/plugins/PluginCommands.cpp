// SPDX-License-Identifier: AGPL-3.0-only
#include "app/plugins/PluginCommands.h"
#include "app/effects/ParameterPanel.h"
#include "app/theme/Fonts.h"
#include "app/theme/Palette.h"
#include "app/ui/Dialogs.h"
#include "dsp/effects/EffectCatalogue.h"
#include "services/logging/Log.h"

#include <map>

namespace mxj
{
    namespace
    {
        using Plugin = plugins::LuaPluginManager::Plugin;
        using Type = plugins::LuaPluginInfo::Type;

        /** Commands added for plugins, removed again when the plugins change. */
        juce::StringArray& pluginCommandIds()
        {
            static juce::StringArray ids;
            return ids;
        }

        std::map<juce::String, std::vector<float>>& lastValues()
        {
            static std::map<juce::String, std::vector<float>> values;
            return values;
        }

        void runNow (AppContext& ctx, Workspace& w, PluginSystem& ps, const Plugin& plugin, const std::vector<float>& values,
                     juce::Component* parent)
        {
            const auto name = plugin.info.name;
            const juce::Component::SafePointer<juce::Component> safeParent (parent);

            if (plugin.info.type == Type::analysis)
            {
                auto report = std::make_shared<juce::String>();
                w.analyseWith (TRANS ("Analysing"),
                               [&lua = ps.getLua(), plugin, values, report] (const AudioSequence& audio, BackgroundJob& j)
                {
                    juce::String error;
                    const auto ok = lua.runAnalysis (plugin, audio, values, { [&j] { return j.shouldContinue(); }, &j.progress },
                                                     *report, error);
                    j.error = error;
                    return ok;
                },
                [name, report, safeParent] (bool ok, const juce::String& error)
                {
                    if (ok)
                        dialogs::showText (name, report->isNotEmpty() ? *report : TRANS ("The plugin reported nothing."), safeParent.getComponent());
                    else if (error.isNotEmpty())
                        dialogs::showWarning (name, TRANS ("The plugin stopped with an error:") + "\n" + error);
                });
                return;
            }

            juce::String messages, error;
            const auto ok = ps.getLua().runTool (plugin.info.id, values, [&ctx] (const juce::String& command)
            {
                // A plugin may not run plugins (no loops) or quit the program.
                if (command.startsWith ("plugins.") || command == "app.quit")
                    return false;

                return ctx.commands.perform (command);
            }, messages, error);

            if (! ok)
                dialogs::showWarning (name, TRANS ("The plugin stopped with an error:") + "\n" + error);
            else if (messages.isNotEmpty())
                dialogs::showInfo (name, messages);
        }

        /** The settings of an analysis or tool plugin, then Run. */
        class LuaRunDialog : public juce::Component
        {
        public:
            LuaRunDialog (AppContext& c, Workspace& w, PluginSystem& p, const Plugin& plugin)
                : ctx (c), workspace (w), ps (p), pluginCopy (plugin), type (makeType (plugin)), values (type.createValues()),
                  panel (type, values, 48000.0)
            {
                if (const auto it = lastValues().find (plugin.info.id); it != lastValues().end())
                    values->setAll (it->second);
                panel.refreshFromValues();
                addAndMakeVisible (panel);

                run.setButtonText (TRANS ("Run"));
                run.setColour (juce::TextButton::buttonColourId, palette::accent);
                run.setColour (juce::TextButton::textColourOffId, palette::onAccent);
                run.onClick = [this]
                {
                    lastValues()[pluginCopy.info.id] = values->getAll();
                    auto* parent = getTopLevelComponent();
                    const auto all = values->getAll();
                    close();
                    runNow (ctx, workspace, ps, pluginCopy, all, parent);
                };
                addAndMakeVisible (run);

                cancel.setButtonText (TRANS ("Close"));
                cancel.onClick = [this] { close(); };
                addAndMakeVisible (cancel);

                setSize (fonts::scaled (520), panel.getIdealHeight() + fonts::scaled (90));
            }

            void paint (juce::Graphics& g) override
            {
                g.fillAll (palette::panel);
                g.setColour (palette::text);
                g.setFont (fonts::sans (13.0f));
                g.drawFittedText (pluginCopy.info.description, getLocalBounds().reduced (16).removeFromTop (fonts::scaled (36)),
                                  juce::Justification::centredLeft, 2);
            }

            void resized() override
            {
                auto area = getLocalBounds().reduced (16);
                area.removeFromTop (fonts::scaled (40));
                auto buttons = area.removeFromBottom (fonts::scaled (28));
                panel.setBounds (area.removeFromTop (panel.getIdealHeight()));
                run.setBounds (buttons.removeFromRight (fonts::scaled (104)));
                buttons.removeFromRight (8);
                cancel.setBounds (buttons.removeFromRight (fonts::scaled (104)));
            }

        private:
            static dsp::EffectType makeType (const Plugin& plugin)
            {
                dsp::EffectType t;
                t.id = "lua:" + plugin.info.id;
                t.name = plugin.info.name;
                t.description = plugin.info.description;
                t.parameters = plugin.info.parameters;
                return t;
            }

            void close()
            {
                if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
                    window->exitModalState (0);
            }

            AppContext& ctx;
            Workspace& workspace;
            PluginSystem& ps;
            const Plugin pluginCopy;
            const dsp::EffectType type;
            dsp::ParameterValuesPtr values;
            ParameterPanel panel;
            juce::TextButton run, cancel;
        };
    }

    void runLuaPlugin (AppContext& ctx, Workspace& w, PluginSystem& ps, const juce::String& id, juce::Component* parent)
    {
        const auto* found = ps.getLua().find (id);
        if (found == nullptr || ! found->enabled || found->error.isNotEmpty())
            return;

        const auto plugin = *found;
        if (plugin.info.type == Type::analysis && ! w.canEdit())
        {
            dialogs::showInfo (plugin.info.name, TRANS ("Open an audio file first: this plugin analyses the selection or the whole file."));
            return;
        }

        if (plugin.info.parameters.empty())
        {
            runNow (ctx, w, ps, plugin, {}, parent);
            return;
        }

        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (new LuaRunDialog (ctx, w, ps, plugin));
        options.dialogTitle = plugin.info.name;
        options.dialogBackgroundColour = palette::panel;
        options.componentToCentreAround = parent;
        options.useNativeTitleBar = true;
        options.resizable = false;
        options.escapeKeyTriggersCloseButton = true;
        options.launchAsync();
    }

    void installPluginFromFile (PluginSystem& ps, std::function<void (bool)> onDone)
    {
        static std::unique_ptr<juce::FileChooser> chooser; // kept until the next one
        chooser = std::make_unique<juce::FileChooser> (TRANS ("Install Plugin from File"), juce::File(), "*.mxjplugin");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [&ps, onDone] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File())
                return;

            plugins::LuaPluginInfo info;
            juce::String error;
            if (! plugins::LuaPluginManager::readPackage (file, info, error))
            {
                dialogs::showWarning (TRANS ("Install Plugin from File"), TRANS ("This is not a valid MXJ plugin:") + "\n" + TRANS (error));
                if (onDone != nullptr)
                    onDone (false);
                return;
            }

            const auto install = [&ps, file, info, onDone]
            {
                juce::String installError;
                const auto ok = ps.getLua().install (file, installError);
                if (ok)
                {
                    Log::info ("Plugin installed from a file: " + info.id + " " + info.version);
                    dialogs::showInfo (TRANS ("Install Plugin from File"), info.name + " " + info.version + ": " + TRANS ("installed."));
                }
                else
                {
                    dialogs::showWarning (TRANS ("Install Plugin from File"), TRANS (installError));
                }

                if (onDone != nullptr)
                    onDone (ok);
            };

            // Plugins that may run program commands are shown first.
            if (info.needsCommands())
                dialogs::ask (info.name, TRANS ("This plugin asks for permission to run program commands (for example undo, select or zoom). "
                                                "Install it only if you trust its author.") + "\n\n" + TRANS ("Author") + ": " + info.author,
                              { TRANS ("Install"), TRANS ("Cancel") }, [install] (int result) { if (result == 1) install(); });
            else
                install();
        });
    }

    void registerPluginCommands (AppContext& ctx, Workspace& w, SessionWorkspace& s, PluginSystem& ps, PluginActions actions)
    {
        auto& c = ctx.commands;
        const auto notBusy = [&w, &s] { return ! w.isBusy() && ! w.isRecording() && ! s.isBusy() && ! s.isRecording(); };
        const auto editable = [&w, &s] { return ! s.isActive() && w.canEdit(); };

        if (! c.contains ("plugins.manager"))
        {
            c.add ({ "plugins.manager", "Plugins...", "Plugins", "Installed plugins, the catalogue, VST3 and CLAP folders, and developer tools", {},
                     nullptr, actions.showPluginsPage });
            c.add ({ "plugins.scan", "Scan for New VST3 and CLAP Plugins", "Plugins", "Look for new or changed plugins in the plugin folders", {},
                     [&ps] { return ! ps.getScanner().isScanning(); }, [&ps] { ps.scan (false); } });
            c.add ({ "plugins.installFile", "Install Plugin from File...", "Plugins", "Install an MXJ plugin package (.mxjplugin)", {},
                     nullptr, [&ps] { installPluginFromFile (ps); } });
        }

        for (const auto& id : pluginCommandIds())
            c.remove (id);
        pluginCommandIds().clear();

        // Plugin effects: like built-in effects, in submenus by format.
        for (const auto* type : dsp::catalogue::all())
        {
            if (type->source.isEmpty())
                continue;

            const auto id = "effects." + type->id;
            if (c.add ({ id, type->name + "...", "Effects", type->description, {}, editable,
                         [showEffect = actions.showEffect, typeId = type->id] { showEffect (typeId); }, nullptr, type->category }))
                pluginCommandIds().add (id);
        }

        // Analysis and tool plugins: in the Plugins menu.
        for (const auto& p : ps.getLua().getPlugins())
        {
            if (! p.enabled || p.error.isNotEmpty() || (p.info.type != Type::analysis && p.info.type != Type::tool))
                continue;

            const auto id = "plugins.run." + p.info.id;
            const auto enabled = p.info.type == Type::analysis ? std::function<bool()> (editable) : std::function<bool()> (notBusy);
            if (c.add ({ id, p.info.name + (p.info.parameters.empty() ? "" : "..."), "Plugins", p.info.description, {}, enabled,
                         [&ctx, &w, &ps, pluginId = p.info.id, getWindow = actions.getMainWindow]
                         { runLuaPlugin (ctx, w, ps, pluginId, getWindow != nullptr ? getWindow() : nullptr); },
                         nullptr, p.info.type == Type::analysis ? "Analysis" : "Tools" }))
                pluginCommandIds().add (id);
        }
    }
}
