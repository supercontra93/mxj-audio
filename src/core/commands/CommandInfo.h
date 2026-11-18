// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_core/juce_core.h>
#include <functional>

namespace mxj
{
    /** Stable, human readable command identifier such as "file.open".
        Ids are part of the public surface: menus, shortcuts, the command palette,
        macros and Lua plugins all refer to commands by this id, so never rename one. */
    using CommandId = juce::String;

    /** Everything the program knows about one user action.

        Every user action in MXJ AUDIO is a command. Menus, keyboard shortcuts,
        the command palette, macros and plugins all call the same commands
        through the CommandRegistry, so behaviour never forks between them.
    */
    struct CommandInfo
    {
        CommandId id;                 ///< e.g. "edit.undo"
        juce::String name;            ///< English source text, translated for display with TRANS()
        juce::String category;        ///< menu the command belongs to, e.g. "File"
        juce::String description;     ///< one line, shown in the command palette
        juce::String defaultShortcut; ///< JUCE KeyPress description, e.g. "ctrl + O"; empty for none

        /** Returns false while the command cannot run. Missing means always enabled. */
        std::function<bool()> isEnabled;

        /** Runs the command. Called on the message thread only. */
        std::function<void()> perform;

        /** For on/off commands: returns true while the option is on (shown with a tick). */
        std::function<bool()> isTicked;

        /** Submenu inside the category's menu, English (e.g. "Filter and EQ"); empty = top level. */
        juce::String group;
    };
}
