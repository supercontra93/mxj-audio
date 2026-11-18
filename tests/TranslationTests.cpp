// SPDX-License-Identifier: AGPL-3.0-only
#include "dsp/effects/EffectCatalogue.h"
#include "services/i18n/Localisation.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    juce::String loadTurkish()
    {
        return juce::File (MXJ_TRANSLATIONS_DIR).getChildFile ("tr.txt").loadFileAsString();
    }
}

TEST_CASE ("Turkish translation loads and switches back to English", "[i18n]")
{
    mxj::Localisation::apply ("tr", loadTurkish());
    CHECK (mxj::Localisation::getCurrentLanguage() == "tr");
    CHECK (TRANS ("File") == juce::String (juce::CharPointer_UTF8 ("Dosya")));
    CHECK (TRANS ("Edit") == juce::String (juce::CharPointer_UTF8 ("D\xc3\xbczen")));

    mxj::Localisation::apply ("en", {});
    CHECK (mxj::Localisation::getCurrentLanguage() == "en");
    CHECK (TRANS ("File") == "File");
}

TEST_CASE ("Every TRANS string in the code has a Turkish translation", "[i18n]")
{
    const juce::LocalisedStrings turkish (loadTurkish(), false);

    // Command names and menu names are translated through TRANS (variable), so the
    // check also collects the string literals of registerAppCommands.
    juce::StringArray missing;
    const juce::File sourceDir (MXJ_SOURCE_DIR);

    for (const auto& entry : juce::RangedDirectoryIterator (sourceDir, true, "*.cpp;*.h"))
    {
        const auto text = entry.getFile().loadFileAsString();
        const bool isCommandFile = entry.getFile().getFileName() == "AppCommands.cpp" || entry.getFile().getFileName() == "PluginCommands.cpp";

        int pos = 0;
        while ((pos = text.indexOf (pos, "TRANS (\"")) >= 0)
        {
            pos += 8;
            const auto end = text.indexOf (pos, "\")");
            if (end < 0)
                break;

            // Join adjacent string literals: "abc " "def" -> "abc def"
            juce::String joined;
            bool inside = true;
            for (auto c : text.substring (pos, end))
            {
                if (c == '"') { inside = ! inside; continue; }
                if (inside) joined += c;
            }

            if (turkish.translate (joined, "<missing>") == "<missing>")
                missing.add (joined);

            pos = end;
        }

        if (isCommandFile)
        {
            // c.add ({ "id", "Name", "Category", "Description", ...
            const auto lines = juce::StringArray::fromLines (text);
            for (const auto& line : lines)
            {
                if (! line.trim().startsWith ("c.add ({"))
                    continue;

                juce::StringArray parts;
                parts.addTokens (line.fromFirstOccurrenceOf ("{", false, false), ",", "\"");
                for (int i = 1; i <= 3 && i < parts.size(); ++i)
                {
                    // Only string literals; names built from variables are checked elsewhere.
                    if (! parts[i].trim().startsWithChar ('"'))
                        continue;

                    const auto s = parts[i].trim().unquoted();
                    if (s.isNotEmpty() && turkish.translate (s, "<missing>") == "<missing>")
                        missing.add (s);
                }
            }
        }
    }

    missing.removeDuplicates (false);
    INFO ("Missing translations:\n" << missing.joinIntoString ("\n"));
    CHECK (missing.isEmpty());
}

TEST_CASE ("Every effect name, control and preset has a Turkish translation", "[i18n]")
{
    const juce::LocalisedStrings turkish (loadTurkish(), false);
    juce::StringArray missing;
    const auto check = [&] (const juce::String& s)
    {
        if (s.isNotEmpty() && turkish.translate (s, "<missing>") == "<missing>")
            missing.addIfNotAlreadyThere (s);
    };

    for (const auto* entry : mxj::dsp::catalogue::all())
    {
        const auto& type = *entry;
        check (type.name);
        check (type.name + "...");
        check (type.category);
        check (type.description);

        for (const auto& p : type.parameters)
        {
            check (p.name);
            for (const auto& c : p.choices)
                check (c);
        }

        for (const auto& preset : type.factoryPresets)
            check (preset.name);
    }

    INFO ("Missing translations:\n" << missing.joinIntoString ("\n"));
    CHECK (missing.isEmpty());
}
