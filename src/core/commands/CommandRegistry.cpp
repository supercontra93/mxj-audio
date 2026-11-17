// SPDX-License-Identifier: AGPL-3.0-only
#include "core/commands/CommandRegistry.h"

#include <algorithm>

namespace mxj
{
    bool CommandRegistry::add (CommandInfo info)
    {
        if (info.id.isEmpty() || info.perform == nullptr || contains (info.id))
            return false;

        commands.push_back (std::make_unique<CommandInfo> (std::move (info)));
        return true;
    }

    bool CommandRegistry::remove (const CommandId& id)
    {
        const auto it = std::find_if (commands.begin(), commands.end(),
                                      [&id] (const auto& c) { return c->id == id; });
        if (it == commands.end())
            return false;

        commands.erase (it);
        return true;
    }

    bool CommandRegistry::contains (const CommandId& id) const
    {
        return find (id) != nullptr;
    }

    const CommandInfo* CommandRegistry::find (const CommandId& id) const
    {
        for (const auto& c : commands)
            if (c->id == id)
                return c.get();

        return nullptr;
    }

    bool CommandRegistry::isEnabled (const CommandId& id) const
    {
        const auto* c = find (id);
        return c != nullptr && (c->isEnabled == nullptr || c->isEnabled());
    }

    bool CommandRegistry::perform (const CommandId& id)
    {
        if (! isEnabled (id))
            return false;

        // Copy the function first: a command may remove itself while running.
        const auto fn = find (id)->perform;
        fn();

        if (onCommandPerformed != nullptr)
            onCommandPerformed (id);

        return true;
    }

    std::vector<const CommandInfo*> CommandRegistry::getAll() const
    {
        std::vector<const CommandInfo*> result;
        result.reserve (commands.size());

        for (const auto& c : commands)
            result.push_back (c.get());

        return result;
    }

    std::vector<const CommandInfo*> CommandRegistry::getByCategory (const juce::String& category) const
    {
        std::vector<const CommandInfo*> result;

        for (const auto& c : commands)
            if (c->category == category)
                result.push_back (c.get());

        return result;
    }
}
