// SPDX-License-Identifier: AGPL-3.0-only
#include "core/history/History.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct AddAction : juce::UndoableAction
    {
        AddAction (int& v, int a) : value (v), amount (a) {}
        bool perform() override { value += amount; return true; }
        bool undo() override    { value -= amount; return true; }

        int& value;
        int amount;
    };
}

TEST_CASE ("History undoes and redoes steps in order", "[history]")
{
    mxj::History history;
    int value = 0;

    history.beginStep ("Add one");
    history.perform (new AddAction (value, 1));
    history.beginStep ("Add ten");
    history.perform (new AddAction (value, 10));

    CHECK (value == 11);
    CHECK (history.getUndoName() == "Add ten");

    REQUIRE (history.undo());
    CHECK (value == 1);
    CHECK (history.canRedo());
    CHECK (history.getRedoName() == "Add ten");

    REQUIRE (history.undo());
    CHECK (value == 0);
    CHECK_FALSE (history.canUndo());

    REQUIRE (history.redo());
    REQUIRE (history.redo());
    CHECK (value == 11);
}

TEST_CASE ("History has no fixed step limit", "[history]")
{
    mxj::History history;
    int value = 0;

    for (int i = 0; i < 5000; ++i)
    {
        history.beginStep ("step");
        history.perform (new AddAction (value, 1));
    }

    int undone = 0;
    while (history.undo())
        ++undone;

    CHECK (undone == 5000);
    CHECK (value == 0);
}
