// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/commands/CommandInfo.h"

#include <memory>
#include <vector>

namespace mxj
{
    /** The single list of commands of the program.

        Message thread only. Not for the audio thread.
    */
    class CommandRegistry
    {
    public:
        CommandRegistry() = default;

        /** Adds a command. Returns false (and adds nothing) if the id is empty,
            already registered or has no perform function. */
        bool add (CommandInfo info);

        /** Removes a command. Returns false if it was not registered. */
        bool remove (const CommandId& id);

        bool contains (const CommandId& id) const;

        /** Returns the command, or nullptr. The pointer stays valid until the
            command is removed. */
        const CommandInfo* find (const CommandId& id) const;

        /** True if the command exists and its isEnabled check passes. */
        bool isEnabled (const CommandId& id) const;

        /** Runs the command if it exists and is enabled. Returns true if it ran. */
        bool perform (const CommandId& id);

        /** All commands, in registration order. */
        std::vector<const CommandInfo*> getAll() const;

        /** Commands of one category, in registration order. */
        std::vector<const CommandInfo*> getByCategory (const juce::String& category) const;

        int size() const noexcept { return (int) commands.size(); }

        /** Called after a command has run. Used later by macro recording. */
        std::function<void (const CommandId&)> onCommandPerformed;

    private:
        std::vector<std::unique_ptr<CommandInfo>> commands;

        JUCE_DECLARE_NON_COPYABLE (CommandRegistry)
    };
}
