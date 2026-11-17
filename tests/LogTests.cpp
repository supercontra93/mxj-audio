// SPDX-License-Identifier: AGPL-3.0-only
#include "services/logging/Log.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

using mxj::Log;

TEST_CASE ("Log writes to a file in the logs folder", "[log]")
{
    mxj::test::TempFolder temp;

    REQUIRE (Log::start (temp.folder));
    Log::info ("hello from the test");
    const auto file = Log::getCurrentFile();
    Log::stop();

    REQUIRE (file.existsAsFile());
    CHECK (file.getParentDirectory() == temp.folder);
    CHECK (file.loadFileAsString().contains ("hello from the test"));
    CHECK (file.loadFileAsString().contains ("INFO"));
}

TEST_CASE ("Old log files are pruned", "[log]")
{
    mxj::test::TempFolder temp;

    for (int i = 0; i < 15; ++i)
        temp.folder.getChildFile ("mxj_2026-01-" + juce::String (i + 10) + "_00-00-00.log").replaceWithText ("x");

    Log::pruneOldFiles (temp.folder, 10);

    const auto remaining = temp.folder.findChildFiles (juce::File::findFiles, false, "mxj_*.log");
    CHECK (remaining.size() == 10);
    CHECK_FALSE (temp.folder.getChildFile ("mxj_2026-01-10_00-00-00.log").exists()); // oldest went first
    CHECK (temp.folder.getChildFile ("mxj_2026-01-24_00-00-00.log").exists());
}
