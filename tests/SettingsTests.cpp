// SPDX-License-Identifier: AGPL-3.0-only
#include "services/settings/SettingKeys.h"
#include "services/settings/Settings.h"
#include "services/paths/AppPaths.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

using mxj::Settings;
namespace keys = mxj::keys;

TEST_CASE ("Missing settings file gives defaults", "[settings]")
{
    mxj::test::TempFolder temp;
    Settings settings (temp.folder.getChildFile ("settings.json"));

    CHECK_FALSE (settings.load());
    CHECK_FALSE (settings.wasLoadedFromDamagedFile());
    CHECK (settings.getString (keys::language) == "tr");
    CHECK (settings.getBool (keys::updateCheckOnStart) == false);
    CHECK (settings.getDouble (keys::uiScale) == 1.0);
}

TEST_CASE ("Settings survive a save and load", "[settings]")
{
    mxj::test::TempFolder temp;
    const auto file = temp.folder.getChildFile ("sub").getChildFile ("settings.json");

    {
        Settings settings (file);
        settings.set (keys::language, "en");
        settings.set (keys::uiScale, 1.25);
        settings.set ("plugin.example.gain", 3);
        REQUIRE (settings.save());
    }

    Settings loaded (file);
    REQUIRE (loaded.load());
    CHECK (loaded.getString (keys::language) == "en");
    CHECK (loaded.getDouble (keys::uiScale) == 1.25);
    CHECK ((int) loaded.get ("plugin.example.gain") == 3);
    CHECK (file.loadFileAsString().contains ("\"schemaVersion\": 1"));
}

TEST_CASE ("A damaged settings file is kept aside and defaults are used", "[settings]")
{
    mxj::test::TempFolder temp;
    const auto file = temp.folder.getChildFile ("settings.json");
    file.replaceWithText ("{ this is not json");

    Settings settings (file);
    CHECK_FALSE (settings.load());
    CHECK (settings.wasLoadedFromDamagedFile());
    CHECK (settings.getString (keys::language) == "tr");
    CHECK (temp.folder.getChildFile ("settings.damaged.json").existsAsFile());
}

TEST_CASE ("Changing a setting notifies listeners once", "[settings]")
{
    mxj::test::TempFolder temp;
    Settings settings (temp.folder.getChildFile ("settings.json"));
    juce::StringArray changed;
    settings.onChange = [&] (const juce::String& key) { changed.add (key); };

    settings.set (keys::language, "en");
    settings.set (keys::language, "en"); // same value: no notification
    settings.reset (keys::language);

    CHECK (changed.size() == 2);
    CHECK (settings.getString (keys::language) == "tr");
}

TEST_CASE ("App paths create their folders", "[paths]")
{
    mxj::test::TempFolder temp;
    const mxj::AppPaths paths (temp.folder.getChildFile ("MXJ AUDIO"));

    REQUIRE (paths.createFolders());
    CHECK (paths.getLogsFolder().isDirectory());
    CHECK (paths.getPluginsFolder().isDirectory());
    CHECK (paths.getSettingsFile().getParentDirectory() == paths.getRoot());
}
