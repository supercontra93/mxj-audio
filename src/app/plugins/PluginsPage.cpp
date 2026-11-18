// SPDX-License-Identifier: AGPL-3.0-only
#include "app/plugins/PluginsPage.h"
#include "app/plugins/PluginCommands.h"
#include "app/theme/Fonts.h"
#include "app/theme/Palette.h"
#include "app/ui/Dialogs.h"
#include "services/logging/Log.h"

namespace mxj
{
    namespace
    {
        constexpr int gap = 8;
        int buttonHeight() { return fonts::scaled (28); }
        int labelHeight()  { return fonts::scaled (22); }

        using LuaPlugin = plugins::LuaPluginManager::Plugin;
        using LuaType = plugins::LuaPluginInfo::Type;

        juce::String typeText (LuaType t)
        {
            switch (t)
            {
                case LuaType::effect:    return TRANS ("Effect");
                case LuaType::generator: return TRANS ("Generator");
                case LuaType::analysis:  return TRANS ("Analysis");
                case LuaType::tool:      return TRANS ("Tool");
            }

            return {};
        }

        juce::String permissionText (const juce::StringArray& permissions)
        {
            if (permissions.isEmpty())
                return TRANS ("none");

            juce::StringArray texts;
            for (const auto& p : permissions)
                texts.add (p == "commands" ? TRANS ("run program commands") : p);

            return texts.joinIntoString (", ");
        }

        void styleDetails (juce::TextEditor& e)
        {
            e.setMultiLine (true);
            e.setReadOnly (true);
            e.setScrollbarsShown (true);
            e.setCaretVisible (false);
            e.setFont (fonts::sans (13.0f));
        }

        void styleLabel (juce::Label& l, bool dim = true)
        {
            l.setFont (fonts::sans (13.0f));
            l.setColour (juce::Label::textColourId, dim ? palette::textDim : palette::text);
            l.setMinimumHorizontalScale (0.8f);
        }

        //==============================================================================
        /** A list with columns. Rows are lines of text; widths are shares of the width. */
        class TableList : public juce::ListBox,
                          private juce::ListBoxModel
        {
        public:
            TableList (juce::StringArray headings, std::vector<float> widths)
                : heads (std::move (headings)), shares (std::move (widths))
            {
                setModel (this);
                setRowHeight (fonts::scaled (26));
                setColour (juce::ListBox::backgroundColourId, palette::background);
                setColour (juce::ListBox::outlineColourId, palette::line);
                setOutlineThickness (1);
                setHeaderComponent (std::make_unique<Header> (*this));
            }

            ~TableList() override { setModel (nullptr); }

            void setRows (std::vector<juce::StringArray> newRows, std::vector<bool> dimmed = {})
            {
                const auto keep = getSelectedRow();
                rows = std::move (newRows);
                dim = std::move (dimmed);
                updateContent();
                if (keep >= 0 && keep < (int) rows.size())
                    selectRow (keep, true, true);
                else
                    deselectAllRows();
                repaint();
            }

            std::function<void()> onSelect;
            std::function<void (int)> onDoubleClick;

        private:
            class Header : public juce::Component
            {
            public:
                explicit Header (TableList& t) : table (t) { setSize (10, fonts::scaled (24)); }

                void paint (juce::Graphics& g) override
                {
                    g.fillAll (palette::panelRaised);
                    g.setColour (palette::textDim);
                    g.setFont (fonts::sansBold (12.0f));
                    table.drawColumns (g, table.heads, getWidth(), getHeight());
                }

            private:
                TableList& table;
            };

            void drawColumns (juce::Graphics& g, const juce::StringArray& texts, int width, int height) const
            {
                float total = 0.0f;
                for (auto s : shares)
                    total += s;

                float x = 8.0f;
                const auto usable = (float) width - 16.0f;
                for (size_t i = 0; i < shares.size() && (int) i < texts.size(); ++i)
                {
                    const auto w = usable * shares[i] / total;
                    g.drawFittedText (texts[(int) i], juce::Rectangle<float> (x, 0.0f, w - 6.0f, (float) height).toNearestInt(),
                                      juce::Justification::centredLeft, 1, 0.8f);
                    x += w;
                }
            }

            int getNumRows() override { return (int) rows.size(); }

            void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool rowIsSelected) override
            {
                if (row < 0 || row >= (int) rows.size())
                    return;

                if (rowIsSelected)
                    g.fillAll (palette::accent.withAlpha (0.22f));
                else if (row % 2 == 1)
                    g.fillAll (palette::panel.withAlpha (0.5f));

                const bool isDim = row < (int) dim.size() && dim[(size_t) row];
                g.setColour (isDim ? palette::textDim : palette::text);
                g.setFont (fonts::sans (13.0f));
                drawColumns (g, rows[(size_t) row], width, height);
            }

            void selectedRowsChanged (int) override
            {
                if (onSelect != nullptr)
                    onSelect();
            }

            void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
            {
                if (onDoubleClick != nullptr)
                    onDoubleClick (row);
            }

            juce::StringArray heads;
            std::vector<float> shares;
            std::vector<juce::StringArray> rows;
            std::vector<bool> dim;
        };

        //==============================================================================
        class Tab : public juce::Component
        {
        public:
            explicit Tab (PluginSystem& p) : ps (p) {}
            virtual void refresh() = 0;
            void paint (juce::Graphics& g) override { g.fillAll (palette::panel); }

        protected:
            PluginSystem& ps;
        };

        //==============================================================================
        class InstalledTab : public Tab
        {
        public:
            InstalledTab (PluginSystem& p, std::function<void (const juce::String&)> u) : Tab (p), use (std::move (u))
            {
                addAndMakeVisible (list);
                list.onSelect = [this] { updateDetails(); };
                list.onDoubleClick = [this] (int) { openSelected(); };

                styleDetails (details);
                addAndMakeVisible (details);

                onOff.onClick = [this]
                {
                    if (const auto* sel = selected())
                        ps.setEnabled (sel->info.id, ! sel->enabled);
                };
                open.setButtonText (TRANS ("Open..."));
                open.setTooltip (TRANS ("Open the plugin: the effect window, or run an analysis or tool"));
                open.onClick = [this] { openSelected(); };
                update.onClick = [this] { updateSelected(); };
                remove.setButtonText (TRANS ("Remove"));
                remove.onClick = [this] { removeSelected(); };
                installFile.setButtonText (TRANS ("Install from File..."));
                installFile.onClick = [this] { installPluginFromFile (ps); };
                folder.setButtonText (TRANS ("Show Folder"));
                folder.setTooltip (TRANS ("Show the folder where the plugin's files are"));
                folder.onClick = [this]
                {
                    if (const auto* sel = selected())
                        sel->folder.revealToUser();
                };

                for (auto* b : { &onOff, &open, &update, &remove, &installFile, &folder })
                    addAndMakeVisible (b);

                refresh();
            }

            void refresh() override
            {
                plugins = ps.getLua().getPlugins();
                std::vector<juce::StringArray> rows;
                std::vector<bool> dim;
                for (const auto& p : plugins)
                {
                    const auto state = p.error.isNotEmpty() ? TRANS ("Error")
                                     : ! p.enabled         ? TRANS ("Off")
                                     : p.developer         ? TRANS ("Developer")
                                                           : TRANS ("On");
                    rows.push_back ({ p.info.name.isNotEmpty() ? p.info.name : p.folder.getFileName(), p.info.version,
                                      typeText (p.info.type), state });
                    dim.push_back (! p.enabled || p.error.isNotEmpty());
                }

                list.setRows (std::move (rows), std::move (dim));
                updateDetails();
            }

            void resized() override
            {
                auto area = getLocalBounds().reduced (gap);
                auto buttons = area.removeFromBottom (buttonHeight());
                area.removeFromBottom (gap);
                list.setBounds (area.removeFromLeft (area.getWidth() * 3 / 5));
                area.removeFromLeft (gap);
                details.setBounds (area);

                const auto w = fonts::scaled (128);
                for (auto* b : { &onOff, &open, &update, &remove })
                {
                    b->setBounds (buttons.removeFromLeft (w));
                    buttons.removeFromLeft (gap);
                }
                installFile.setBounds (buttons.removeFromRight (fonts::scaled (170)));
                buttons.removeFromRight (gap);
                folder.setBounds (buttons.removeFromRight (w));
            }

        private:
            const LuaPlugin* selected() const
            {
                const auto row = list.getSelectedRow();
                return row >= 0 && row < (int) plugins.size() ? &plugins[(size_t) row] : nullptr;
            }

            const plugins::PluginCatalogue::Entry* newerInCatalogue (const LuaPlugin& p) const
            {
                const auto* e = ps.getCatalogue().find (p.info.id);
                return e != nullptr && ! p.developer && e->isCompatible() && plugins::compareVersions (e->version, p.info.version) > 0 ? e : nullptr;
            }

            void updateDetails()
            {
                const auto* p = selected();
                for (auto* b : { &onOff, &open, &update, &remove, &folder })
                    b->setEnabled (p != nullptr);

                onOff.setButtonText (p != nullptr && ! p->enabled ? TRANS ("Turn On") : TRANS ("Turn Off"));
                if (p == nullptr)
                {
                    details.setText (plugins.empty() ? TRANS ("No MXJ plugins are installed yet. Install one from the Discover tab, "
                                                              "from a file, or load one from a folder on the Developer tab.")
                                                     : juce::String(), false);
                    update.setButtonText (TRANS ("Update"));
                    return;
                }

                const auto* newer = newerInCatalogue (*p);
                update.setEnabled (newer != nullptr);
                update.setButtonText (newer != nullptr ? TRANS ("Update to") + " " + newer->version : TRANS ("Update"));
                open.setEnabled (p->enabled && p->error.isEmpty());

                juce::String t;
                t << p->info.name << " " << p->info.version << "\n"
                  << typeText (p->info.type) << (p->info.category.isNotEmpty() ? ", " + p->info.category : juce::String()) << "\n\n"
                  << p->info.description << "\n\n"
                  << TRANS ("Author") << ": " << p->info.author << "\n"
                  << TRANS ("Licence") << ": " << p->info.license << "\n"
                  << TRANS ("Permissions") << ": " << permissionText (p->info.permissions) << "\n"
                  << TRANS ("Id") << ": " << p->info.id << "\n"
                  << TRANS ("Folder") << ": " << p->folder.getFullPathName() << "\n";
                if (p->info.homepage.isNotEmpty())
                    t << TRANS ("Web page") << ": " << p->info.homepage << "\n";
                if (p->developer)
                    t << "\n" << TRANS ("Loaded from a developer folder: reloaded when you save a file.") << "\n";
                if (p->error.isNotEmpty())
                    t << "\n" << TRANS ("Error") << ": " << p->error << "\n";

                details.setText (t, false);
            }

            void openSelected()
            {
                if (const auto* p = selected(); p != nullptr && p->enabled && p->error.isEmpty() && use != nullptr)
                    use (p->info.id);
            }

            void updateSelected()
            {
                const auto* p = selected();
                if (p == nullptr || newerInCatalogue (*p) == nullptr)
                    return;

                juce::String error;
                if (! ps.installFromCatalogue (p->info.id, error))
                    dialogs::showWarning (TRANS ("Update"), error);
            }

            void removeSelected()
            {
                const auto* p = selected();
                if (p == nullptr)
                    return;

                const auto id = p->info.id;
                const auto developer = p->developer;
                dialogs::ask (TRANS ("Remove"),
                              developer ? TRANS ("Forget this developer plugin? Its folder is not deleted.")
                                        : TRANS ("Remove this plugin and delete its files?") + "\n\n" + p->info.name,
                              { TRANS ("Remove"), TRANS ("Cancel") },
                              [safe = juce::Component::SafePointer<InstalledTab> (this), id] (int result)
                {
                    if (safe == nullptr || result != 1)
                        return;

                    safe->ps.getLua().uninstall (id);
                    safe->ps.storeSettings();
                    Log::info ("Plugin removed: " + id);
                });
            }

            std::function<void (const juce::String&)> use;
            std::vector<LuaPlugin> plugins;
            TableList list { { TRANS ("Name"), TRANS ("Version"), TRANS ("Type"), TRANS ("State") }, { 4.0f, 1.3f, 1.6f, 1.4f } };
            juce::TextEditor details;
            juce::TextButton onOff, open, update, remove, installFile, folder;
        };

        //==============================================================================
        class DiscoverTab : public Tab
        {
        public:
            explicit DiscoverTab (PluginSystem& p) : Tab (p)
            {
                styleLabel (note);
                addAndMakeVisible (note);

                search.setTextToShowWhenEmpty (TRANS ("Search plugins"), palette::textDim);
                search.onTextChange = [this] { refresh(); };
                addAndMakeVisible (search);

                category.onChange = [this] { refresh(); };
                addAndMakeVisible (category);

                addAndMakeVisible (list);
                list.onSelect = [this] { updateDetails(); };
                list.onDoubleClick = [this] (int) { installSelected(); };

                styleDetails (details);
                addAndMakeVisible (details);

                install.setColour (juce::TextButton::buttonColourId, palette::accent);
                install.setColour (juce::TextButton::textColourOffId, palette::onAccent);
                install.onClick = [this] { installSelected(); };
                addAndMakeVisible (install);

                const auto& catalogue = ps.getCatalogue();
                category.addItem (TRANS ("All categories"), 1);
                const auto categories = catalogue.getCategories();
                for (int i = 0; i < categories.size(); ++i)
                    category.addItem (TRANS (categories[i]), i + 2);
                category.setSelectedId (1, juce::dontSendNotification);

                note.setText (ps.getCatalogueError().isNotEmpty()
                                  ? TRANS ("The catalogue could not be read:") + " " + TRANS (ps.getCatalogueError())
                                  : TRANS ("Local test catalogue (comes with the program). The online community catalogue opens with version 0.9; "
                                           "every package is checked with SHA-256 before it is installed."),
                              juce::dontSendNotification);
                refresh();
            }

            void refresh() override
            {
                const auto categories = ps.getCatalogue().getCategories();
                const auto chosen = category.getSelectedId() >= 2 ? categories[category.getSelectedId() - 2] : juce::String();
                shown = ps.getCatalogue().search (search.getText(), chosen);

                std::vector<juce::StringArray> rows;
                for (const auto* e : shown)
                {
                    const auto installed = ps.getInstalledVersion (e->id);
                    rows.push_back ({ e->name, TRANS (e->category), e->version, e->author,
                                      installed.isEmpty() ? juce::String() : TRANS ("Installed") + " " + installed });
                }

                list.setRows (std::move (rows));
                updateDetails();
            }

            void resized() override
            {
                auto area = getLocalBounds().reduced (gap);
                note.setBounds (area.removeFromTop (labelHeight() * 2));
                area.removeFromTop (gap);
                auto top = area.removeFromTop (buttonHeight());
                category.setBounds (top.removeFromRight (fonts::scaled (200)));
                top.removeFromRight (gap);
                search.setBounds (top);
                area.removeFromTop (gap);
                list.setBounds (area.removeFromLeft (area.getWidth() * 3 / 5));
                area.removeFromLeft (gap);
                install.setBounds (area.removeFromBottom (buttonHeight()));
                area.removeFromBottom (gap);
                details.setBounds (area);
            }

        private:
            const plugins::PluginCatalogue::Entry* selected() const
            {
                const auto row = list.getSelectedRow();
                return row >= 0 && row < (int) shown.size() ? shown[(size_t) row] : nullptr;
            }

            void updateDetails()
            {
                const auto* e = selected();
                if (e == nullptr)
                {
                    install.setButtonText (TRANS ("Install"));
                    install.setEnabled (false);
                    details.setText (shown.empty() ? TRANS ("No plugin matches the search.") : TRANS ("Choose a plugin to see what it does."), false);
                    return;
                }

                const auto installed = ps.getInstalledVersion (e->id);
                const auto newer = installed.isNotEmpty() && plugins::compareVersions (e->version, installed) > 0;

                if (! e->isCompatible())
                {
                    install.setButtonText (TRANS ("Needs a newer MXJ AUDIO"));
                    install.setEnabled (false);
                }
                else if (installed.isEmpty())
                {
                    install.setButtonText (TRANS ("Install"));
                    install.setEnabled (true);
                }
                else
                {
                    install.setButtonText (newer ? TRANS ("Update to") + " " + e->version : TRANS ("Installed"));
                    install.setEnabled (newer);
                }

                juce::String t;
                t << e->name << " " << e->version << "\n"
                  << TRANS (e->category) << ", " << typeText (e->type == "generator" ? LuaType::generator
                                                            : e->type == "analysis"  ? LuaType::analysis
                                                            : e->type == "tool"      ? LuaType::tool : LuaType::effect) << "\n\n"
                  << e->description << "\n\n"
                  << TRANS ("Author") << ": " << e->author << "\n"
                  << TRANS ("Licence") << ": " << e->license << "\n"
                  << TRANS ("Permissions") << ": " << permissionText (e->permissions) << "\n"
                  << TRANS ("Size") << ": " << juce::File::descriptionOfSizeInBytes (e->size) << "\n"
                  << "SHA-256: " << e->sha256.substring (0, 16) << "...\n";
                if (e->minMxjVersion.isNotEmpty())
                    t << TRANS ("Needs MXJ AUDIO") << " " << e->minMxjVersion << "+\n";
                if (e->homepage.isNotEmpty())
                    t << TRANS ("Web page") << ": " << e->homepage << "\n";
                details.setText (t, false);
            }

            void installSelected()
            {
                const auto* e = selected();
                if (e == nullptr || ! install.isEnabled())
                    return;

                const auto id = e->id;
                const auto name = e->name;
                const auto doInstall = [safe = juce::Component::SafePointer<DiscoverTab> (this), id, name]
                {
                    if (safe == nullptr)
                        return;

                    juce::String error;
                    if (safe->ps.installFromCatalogue (id, error))
                        safe->ps.storeSettings();
                    else
                        dialogs::showWarning (name, error);
                };

                if (e->permissions.contains ("commands"))
                    dialogs::ask (name, TRANS ("This plugin asks for permission to run program commands (for example undo, select or zoom). "
                                               "Install it only if you trust its author.") + "\n\n" + TRANS ("Author") + ": " + e->author,
                                  { TRANS ("Install"), TRANS ("Cancel") }, [doInstall] (int result) { if (result == 1) doInstall(); });
                else
                    doInstall();
            }

            juce::Label note;
            juce::TextEditor search;
            juce::ComboBox category;
            std::vector<const plugins::PluginCatalogue::Entry*> shown;
            TableList list { { TRANS ("Name"), TRANS ("Category"), TRANS ("Version"), TRANS ("Author"), TRANS ("State") },
                             { 3.0f, 2.0f, 1.0f, 2.0f, 1.8f } };
            juce::TextEditor details;
            juce::TextButton install;
        };

        //==============================================================================
        class HostTab : public Tab,
                        private juce::Timer
        {
        public:
            explicit HostTab (PluginSystem& p) : Tab (p)
            {
                scanNew.setButtonText (TRANS ("Scan for New Plugins"));
                scanNew.setTooltip (TRANS ("Look for new or changed plugin files in the folders"));
                scanNew.onClick = [this] { ps.scan (false); refresh(); };
                rescan.setButtonText (TRANS ("Rescan All"));
                rescan.setTooltip (TRANS ("Scan every plugin file again"));
                rescan.onClick = [this] { ps.scan (true); refresh(); };
                cancel.setButtonText (TRANS ("Stop"));
                cancel.onClick = [this] { ps.getScanner().cancel(); refresh(); };
                styleLabel (status);

                for (auto* c : std::initializer_list<juce::Component*> { &scanNew, &rescan, &cancel, &status, &list, &folderTitle,
                                                                         &format, &folders, &addFolder, &removeFolder, &blockedTitle,
                                                                         &blocked, &unblock })
                    addAndMakeVisible (c);

                folderTitle.setText (TRANS ("Folders to scan"), juce::dontSendNotification);
                blockedTitle.setText (TRANS ("Blocked files (crashed, hung or failed while being scanned)"), juce::dontSendNotification);
                styleLabel (folderTitle, false);
                styleLabel (blockedTitle, false);

                const auto names = ps.getHost().getFormatNames();
                for (int i = 0; i < names.size(); ++i)
                    format.addItem (names[i], i + 1);
                format.setSelectedId (1, juce::dontSendNotification);
                format.onChange = [this] { refresh(); };

                addFolder.setButtonText (TRANS ("Add Folder..."));
                addFolder.onClick = [this] { chooseFolder(); };
                removeFolder.setButtonText (TRANS ("Remove Folder"));
                removeFolder.onClick = [this]
                {
                    const auto row = folders.getSelectedRow() - standardCount;
                    auto user = ps.getHost().getUserFolders (format.getText());
                    if (row >= 0 && row < user.size())
                    {
                        user.remove (row);
                        ps.getHost().setUserFolders (format.getText(), user);
                    }
                };
                folders.onSelect = [this] { removeFolder.setEnabled (folders.getSelectedRow() >= standardCount); };

                unblock.setButtonText (TRANS ("Try Again"));
                unblock.setTooltip (TRANS ("Take the file off the block list; it is scanned with the next scan"));
                unblock.onClick = [this]
                {
                    const auto files = ps.getHost().getBlocked();
                    const auto row = blocked.getSelectedRow();
                    if (row >= 0 && row < (int) files.size())
                        ps.getHost().unblock (files[(size_t) row].file);
                };
                blocked.onSelect = [this] { unblock.setEnabled (blocked.getSelectedRow() >= 0); };

                refresh();
                startTimerHz (4);
            }

            void refresh() override
            {
                std::vector<juce::StringArray> rows;
                std::vector<bool> dim;
                for (const auto& d : ps.getHost().getPlugins())
                {
                    const auto effect = plugins::PluginHost::isEffect (d);
                    rows.push_back ({ d.name, d.pluginFormatName, d.manufacturerName,
                                      effect ? TRANS ("Effect") : TRANS ("Instrument (not used yet)") });
                    dim.push_back (! effect);
                }
                list.setRows (std::move (rows), std::move (dim));

                const auto standard = ps.getHost().getStandardFolders (format.getText());
                const auto user = ps.getHost().getUserFolders (format.getText());
                standardCount = standard.size();
                std::vector<juce::StringArray> folderRows;
                for (const auto& f : standard)
                    folderRows.push_back ({ f + "  (" + TRANS ("standard") + ")" });
                for (const auto& f : user)
                    folderRows.push_back ({ f });
                folders.setRows (std::move (folderRows));
                removeFolder.setEnabled (folders.getSelectedRow() >= standardCount);

                std::vector<juce::StringArray> blockedRows;
                for (const auto& b : ps.getHost().getBlocked())
                    blockedRows.push_back ({ juce::File (b.file).getFileName(), TRANS (b.reason) });
                blocked.setRows (std::move (blockedRows));
                unblock.setEnabled (blocked.getSelectedRow() >= 0);

                updateStatus();
            }

            void resized() override
            {
                auto area = getLocalBounds().reduced (gap);
                auto top = area.removeFromTop (buttonHeight());
                scanNew.setBounds (top.removeFromLeft (fonts::scaled (200)));
                top.removeFromLeft (gap);
                rescan.setBounds (top.removeFromLeft (fonts::scaled (130)));
                top.removeFromLeft (gap);
                cancel.setBounds (top.removeFromLeft (fonts::scaled (80)));
                top.removeFromLeft (gap * 2);
                status.setBounds (top);
                area.removeFromTop (gap);

                list.setBounds (area.removeFromLeft (area.getWidth() / 2));
                area.removeFromLeft (gap);

                auto upper = area.removeFromTop (area.getHeight() / 2);
                auto titleRow = upper.removeFromTop (buttonHeight());
                format.setBounds (titleRow.removeFromRight (fonts::scaled (90)));
                folderTitle.setBounds (titleRow);
                upper.removeFromTop (4);
                auto folderButtons = upper.removeFromBottom (buttonHeight());
                upper.removeFromBottom (4);
                folders.setBounds (upper);
                addFolder.setBounds (folderButtons.removeFromLeft (fonts::scaled (130)));
                folderButtons.removeFromLeft (gap);
                removeFolder.setBounds (folderButtons.removeFromLeft (fonts::scaled (130)));

                area.removeFromTop (gap);
                blockedTitle.setBounds (area.removeFromTop (labelHeight()));
                area.removeFromTop (4);
                unblock.setBounds (area.removeFromBottom (buttonHeight()).removeFromLeft (fonts::scaled (130)));
                area.removeFromBottom (4);
                blocked.setBounds (area);
            }

        private:
            void timerCallback() override { updateStatus(); }

            void updateStatus()
            {
                auto& scanner = ps.getScanner();
                const auto scanning = scanner.isScanning();
                scanNew.setEnabled (! scanning);
                rescan.setEnabled (! scanning);
                cancel.setEnabled (scanning);

                juce::String text;
                if (scanning)
                    text = TRANS ("Scanning") + " " + juce::String (juce::roundToInt (scanner.getProgress() * 100.0)) + "%: "
                         + juce::File (scanner.getCurrentFile()).getFileName();
                else
                    text = juce::String ((int) ps.getHost().getPlugins().size()) + " " + TRANS ("plugins found") + ". "
                         + TRANS ("Each file is opened by a separate scanner program, so a broken plugin cannot crash MXJ AUDIO.");

                status.setText (text, juce::dontSendNotification);
            }

            void chooseFolder()
            {
                chooser = std::make_unique<juce::FileChooser> (TRANS ("Add Folder..."), juce::File());
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                      [safe = juce::Component::SafePointer<HostTab> (this)] (const juce::FileChooser& fc)
                {
                    const auto dir = fc.getResult();
                    if (safe == nullptr || ! dir.isDirectory())
                        return;

                    auto user = safe->ps.getHost().getUserFolders (safe->format.getText());
                    user.addIfNotAlreadyThere (dir.getFullPathName());
                    safe->ps.getHost().setUserFolders (safe->format.getText(), user);
                });
            }

            juce::TextButton scanNew, rescan, cancel, addFolder, removeFolder, unblock;
            juce::Label status, folderTitle, blockedTitle;
            juce::ComboBox format;
            TableList list { { TRANS ("Name"), TRANS ("Format"), TRANS ("Maker"), TRANS ("Kind") }, { 3.0f, 1.0f, 2.2f, 2.2f } };
            TableList folders { { TRANS ("Folder") }, { 1.0f } };
            TableList blocked { { TRANS ("File"), TRANS ("Reason") }, { 1.5f, 2.0f } };
            int standardCount = 0;
            std::unique_ptr<juce::FileChooser> chooser;
        };

        //==============================================================================
        class DeveloperTab : public Tab
        {
        public:
            explicit DeveloperTab (PluginSystem& p) : Tab (p)
            {
                styleLabel (hint);
                hint.setText (TRANS ("Load a plugin you are writing straight from its folder (plugin.json and main.lua). "
                                     "It is not copied, and it is reloaded every time you save one of its files. "
                                     "Errors and print() output appear in the console below."),
                              juce::dontSendNotification);

                load.setButtonText (TRANS ("Load from Folder..."));
                load.onClick = [this] { chooseFolder(); };
                forget.setButtonText (TRANS ("Remove"));
                forget.setTooltip (TRANS ("Forget this folder (nothing is deleted)"));
                forget.onClick = [this]
                {
                    auto list = ps.getLua().getDeveloperFolders();
                    const auto row = folders.getSelectedRow();
                    if (row >= 0 && row < list.size())
                    {
                        list.remove (row);
                        ps.setDeveloperFolders (list);
                    }
                };
                reload.setButtonText (TRANS ("Reload Now"));
                reload.onClick = [this] { ps.getLua().reload(); };
                clear.setButtonText (TRANS ("Clear Console"));
                clear.onClick = [this] { ps.getLua().clearConsole(); refresh(); };

                console.setMultiLine (true);
                console.setReadOnly (true);
                console.setScrollbarsShown (true);
                console.setCaretVisible (false);
                console.setFont (fonts::mono (12.0f));
                consoleTitle.setText (TRANS ("Console"), juce::dontSendNotification);
                styleLabel (consoleTitle, false);

                folders.onSelect = [this] { forget.setEnabled (folders.getSelectedRow() >= 0); };

                for (auto* c : std::initializer_list<juce::Component*> { &hint, &load, &forget, &reload, &folders, &consoleTitle, &clear, &console })
                    addAndMakeVisible (c);

                refresh();
            }

            void refresh() override
            {
                std::vector<juce::StringArray> rows;
                for (const auto& f : ps.getLua().getDeveloperFolders())
                {
                    juce::String state = TRANS ("not loaded");
                    for (const auto& p : ps.getLua().getPlugins())
                        if (p.developer && p.folder == juce::File (f))
                            state = p.error.isNotEmpty() ? TRANS ("Error") : p.info.name + " " + p.info.version;

                    rows.push_back ({ f, state });
                }
                folders.setRows (std::move (rows));
                forget.setEnabled (folders.getSelectedRow() >= 0);

                const auto text = ps.getLua().getConsole().joinIntoString ("\n");
                if (text != console.getText())
                {
                    console.setText (text, false);
                    console.moveCaretToEnd();
                }
            }

            void resized() override
            {
                auto area = getLocalBounds().reduced (gap);
                hint.setBounds (area.removeFromTop (labelHeight() * 3));
                area.removeFromTop (gap);
                auto buttons = area.removeFromTop (buttonHeight());
                load.setBounds (buttons.removeFromLeft (fonts::scaled (170)));
                buttons.removeFromLeft (gap);
                forget.setBounds (buttons.removeFromLeft (fonts::scaled (110)));
                buttons.removeFromLeft (gap);
                reload.setBounds (buttons.removeFromLeft (fonts::scaled (130)));
                area.removeFromTop (gap);
                folders.setBounds (area.removeFromTop (fonts::scaled (130)));
                area.removeFromTop (gap);
                auto titleRow = area.removeFromTop (buttonHeight());
                clear.setBounds (titleRow.removeFromRight (fonts::scaled (140)));
                consoleTitle.setBounds (titleRow);
                area.removeFromTop (4);
                console.setBounds (area);
            }

        private:
            void chooseFolder()
            {
                chooser = std::make_unique<juce::FileChooser> (TRANS ("Load from Folder..."), juce::File());
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                      [safe = juce::Component::SafePointer<DeveloperTab> (this)] (const juce::FileChooser& fc)
                {
                    const auto dir = fc.getResult();
                    if (safe == nullptr || ! dir.isDirectory())
                        return;

                    if (! dir.getChildFile ("plugin.json").existsAsFile())
                    {
                        dialogs::showWarning (TRANS ("Load from Folder..."), TRANS ("This folder has no plugin.json file."));
                        return;
                    }

                    auto list = safe->ps.getLua().getDeveloperFolders();
                    list.addIfNotAlreadyThere (dir.getFullPathName());
                    safe->ps.setDeveloperFolders (list);
                    Log::info ("Developer plugin folder added: " + dir.getFullPathName());
                });
            }

            juce::Label hint, consoleTitle;
            juce::TextButton load, forget, reload, clear;
            TableList folders { { TRANS ("Folder"), TRANS ("Plugin") }, { 3.0f, 1.5f } };
            juce::TextEditor console;
            std::unique_ptr<juce::FileChooser> chooser;
        };

        juce::Component::SafePointer<PluginsPage>& openPage()
        {
            static juce::Component::SafePointer<PluginsPage> page;
            return page;
        }
    }

    //==============================================================================
    class PluginsPage::Content : public juce::Component,
                                 private juce::ChangeListener
    {
    public:
        Content (PluginSystem& p, std::function<void (const juce::String&)> use) : ps (p)
        {
            tabs.setTabBarDepth (fonts::scaled (32));
            tabs.setOutline (0);
            tabs.setColour (juce::TabbedComponent::backgroundColourId, palette::panel);

            installed = new InstalledTab (ps, std::move (use));
            discover = new DiscoverTab (ps);
            host = new HostTab (ps);
            developer = new DeveloperTab (ps);
            tabs.addTab (TRANS ("Installed"), palette::panel, installed, true);
            tabs.addTab (TRANS ("Discover"), palette::panel, discover, true);
            tabs.addTab (TRANS ("VST and CLAP"), palette::panel, host, true);
            tabs.addTab (TRANS ("Developer"), palette::panel, developer, true);
            addAndMakeVisible (tabs);

            ps.addChangeListener (this);
            setSize (fonts::scaled (900), fonts::scaled (580));
        }

        ~Content() override { ps.removeChangeListener (this); }

        void paint (juce::Graphics& g) override { g.fillAll (palette::panel); }
        void resized() override { tabs.setBounds (getLocalBounds()); }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            for (auto* t : { installed, discover, host, developer })
                t->refresh();
        }

        PluginSystem& ps;
        juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
        Tab* installed = nullptr;
        Tab* discover = nullptr;
        Tab* host = nullptr;
        Tab* developer = nullptr;
    };

    //==============================================================================
    void PluginsPage::show (PluginSystem& plugins, std::function<void (const juce::String&)> use)
    {
        if (auto* page = openPage().getComponent())
        {
            page->toFront (true);
            return;
        }

        openPage() = new PluginsPage (plugins, std::move (use));
    }

    void PluginsPage::closeAll()
    {
        delete openPage().getComponent();
    }

    PluginsPage::PluginsPage (PluginSystem& plugins, std::function<void (const juce::String&)> use)
        : juce::DocumentWindow (TRANS ("Plugins"), palette::panel, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new Content (plugins, std::move (use)), true);
        setResizable (true, false);
        setResizeLimits (fonts::scaled (720), fonts::scaled (460), 4000, 3000);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    PluginsPage::~PluginsPage() = default;

    void PluginsPage::closeButtonPressed()
    {
        delete this;
    }
}
