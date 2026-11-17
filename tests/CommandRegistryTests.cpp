// SPDX-License-Identifier: AGPL-3.0-only
#include "core/commands/CommandRegistry.h"

#include <catch2/catch_test_macros.hpp>

using mxj::CommandInfo;
using mxj::CommandRegistry;

namespace
{
    CommandInfo makeCommand (const char* id, std::function<void()> perform, std::function<bool()> enabled = nullptr)
    {
        CommandInfo c;
        c.id = id;
        c.name = "Test";
        c.category = "File";
        c.perform = std::move (perform);
        c.isEnabled = std::move (enabled);
        return c;
    }
}

TEST_CASE ("Commands can be added, found and run", "[commands]")
{
    CommandRegistry registry;
    int runs = 0;

    REQUIRE (registry.add (makeCommand ("file.test", [&] { ++runs; })));
    CHECK (registry.contains ("file.test"));
    CHECK (registry.size() == 1);
    CHECK (registry.perform ("file.test"));
    CHECK (runs == 1);
}

TEST_CASE ("Invalid or duplicate commands are rejected", "[commands]")
{
    CommandRegistry registry;

    CHECK (registry.add (makeCommand ("a", [] {})));
    CHECK_FALSE (registry.add (makeCommand ("a", [] {})));       // duplicate id
    CHECK_FALSE (registry.add (makeCommand ("", [] {})));        // empty id
    CHECK_FALSE (registry.add (makeCommand ("b", nullptr)));     // nothing to run
    CHECK (registry.size() == 1);
}

TEST_CASE ("Disabled and unknown commands do not run", "[commands]")
{
    CommandRegistry registry;
    bool enabled = false;
    int runs = 0;

    registry.add (makeCommand ("edit.undo", [&] { ++runs; }, [&] { return enabled; }));

    CHECK_FALSE (registry.isEnabled ("edit.undo"));
    CHECK_FALSE (registry.perform ("edit.undo"));
    CHECK_FALSE (registry.perform ("does.not.exist"));
    CHECK (runs == 0);

    enabled = true;
    CHECK (registry.perform ("edit.undo"));
    CHECK (runs == 1);
}

TEST_CASE ("Commands keep registration order and can be listed by category", "[commands]")
{
    CommandRegistry registry;
    auto edit = makeCommand ("edit.cut", [] {});
    edit.category = "Edit";

    registry.add (makeCommand ("file.one", [] {}));
    registry.add (std::move (edit));
    registry.add (makeCommand ("file.two", [] {}));

    const auto all = registry.getAll();
    REQUIRE (all.size() == 3);
    CHECK (all[0]->id == "file.one");
    CHECK (all[2]->id == "file.two");

    const auto file = registry.getByCategory ("File");
    REQUIRE (file.size() == 2);
    CHECK (file[1]->id == "file.two");
}

TEST_CASE ("Listeners hear about performed commands", "[commands]")
{
    CommandRegistry registry;
    juce::String heard;

    registry.add (makeCommand ("file.test", [] {}));
    registry.onCommandPerformed = [&] (const mxj::CommandId& id) { heard = id; };

    registry.perform ("file.test");
    CHECK (heard == "file.test");
}

TEST_CASE ("A command may remove itself while running", "[commands]")
{
    CommandRegistry registry;
    registry.add (makeCommand ("self.remove", [&] { registry.remove ("self.remove"); }));

    CHECK (registry.perform ("self.remove"));
    CHECK_FALSE (registry.contains ("self.remove"));
}
