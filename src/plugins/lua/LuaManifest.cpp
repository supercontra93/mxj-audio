// SPDX-License-Identifier: AGPL-3.0-only
#include "plugins/lua/LuaManifest.h"
#include "core/Version.h"

namespace mxj::plugins
{
    juce::StringArray knownPermissions()
    {
        return { "commands" };
    }

    int compareVersions (const juce::String& a, const juce::String& b)
    {
        const auto pa = juce::StringArray::fromTokens (a.upToFirstOccurrenceOf ("-", false, false), ".", {});
        const auto pb = juce::StringArray::fromTokens (b.upToFirstOccurrenceOf ("-", false, false), ".", {});

        for (int i = 0; i < juce::jmax (pa.size(), pb.size()); ++i)
        {
            const auto x = pa[i].getIntValue();
            const auto y = pb[i].getIntValue();
            if (x != y)
                return x < y ? -1 : 1;
        }

        return 0;
    }

    bool isValidPluginId (const juce::String& id)
    {
        if (id.length() < 3 || id.length() > 64 || ! juce::CharacterFunctions::isLetterOrDigit (id[0]))
            return false;

        return id.containsOnly ("abcdefghijklmnopqrstuvwxyz0123456789.-_");
    }

    juce::String LuaPluginInfo::typeName (Type t)
    {
        switch (t)
        {
            case Type::effect:    return "effect";
            case Type::generator: return "generator";
            case Type::analysis:  return "analysis";
            case Type::tool:      return "tool";
        }

        return {};
    }

    namespace
    {
        bool readParameter (const juce::var& v, dsp::ParameterInfo& p, juce::String& error)
        {
            const auto id = v.getProperty ("id", {}).toString();
            const auto name = v.getProperty ("name", id).toString();
            const auto type = v.getProperty ("type", "number").toString();

            if (id.isEmpty() || ! id.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_"))
            {
                error = "A parameter has no id, or an id with characters other than letters, digits and '_'";
                return false;
            }

            if (type == "number")
            {
                const auto minValue = (float) (double) v.getProperty ("min", 0.0);
                const auto maxValue = (float) (double) v.getProperty ("max", 1.0);
                const auto def = (float) (double) v.getProperty ("default", minValue);
                if (! (minValue < maxValue) || def < minValue || def > maxValue)
                {
                    error = "Parameter '" + id + "': min must be below max, and default between them";
                    return false;
                }

                const auto mid = (float) (double) v.getProperty ("mid", 0.0);
                p = dsp::ParameterInfo::number (id, name, minValue, maxValue, def, v.getProperty ("unit", {}).toString(),
                                                juce::jlimit (0, 4, (int) v.getProperty ("decimals", 1)),
                                                mid > minValue && mid < maxValue ? mid : 0.0f);
                return true;
            }

            if (type == "choice")
            {
                juce::StringArray choices;
                if (const auto* list = v.getProperty ("choices", {}).getArray())
                    for (const auto& c : *list)
                        choices.add (c.toString());

                const auto def = (int) v.getProperty ("default", 0);
                if (choices.isEmpty() || def < 0 || def >= choices.size())
                {
                    error = "Parameter '" + id + "': a choice needs a list of choices and a default index inside it";
                    return false;
                }

                p = dsp::ParameterInfo::choice (id, name, choices, def);
                return true;
            }

            if (type == "toggle")
            {
                p = dsp::ParameterInfo::toggle (id, name, (bool) v.getProperty ("default", false));
                return true;
            }

            error = "Parameter '" + id + "': type must be number, choice or toggle";
            return false;
        }
    }

    bool LuaPluginInfo::parse (const juce::var& json, LuaPluginInfo& r, juce::String& error)
    {
        if (! json.isObject())
        {
            error = "plugin.json is not a JSON object";
            return false;
        }

        const auto text = [&json] (const char* key, const juce::String& fallback = {})
        {
            return json.getProperty (key, fallback).toString().trim();
        };

        r = {};
        r.id = text ("id");
        r.name = text ("name");
        r.version = text ("version");
        r.author = text ("author");
        r.license = text ("license");
        r.category = text ("category");
        r.description = text ("description");
        r.homepage = text ("homepage");
        r.minMxjVersion = text ("minMxjVersion", "0.6.0");
        r.apiVersion = (int) json.getProperty ("apiVersion", 1);
        r.mainFile = text ("main", "main.lua");

        if (! isValidPluginId (r.id))
        {
            error = "The id must be 3 to 64 lower case letters, digits, '.', '-' or '_' (for example com.example.tremolo)";
            return false;
        }

        if (r.name.isEmpty() || r.version.isEmpty() || r.author.isEmpty() || r.license.isEmpty())
        {
            error = "name, version, author and license are required";
            return false;
        }

        const auto type = text ("type");
        if (type == "effect")         r.type = Type::effect;
        else if (type == "generator") r.type = Type::generator;
        else if (type == "analysis")  r.type = Type::analysis;
        else if (type == "tool")      r.type = Type::tool;
        else
        {
            error = "type must be effect, generator, analysis or tool";
            return false;
        }

        if (r.apiVersion < 1 || r.apiVersion > luaApiVersion)
        {
            error = "The plugin needs plugin API " + juce::String (r.apiVersion) + "; this version of MXJ AUDIO offers "
                  + juce::String (luaApiVersion);
            return false;
        }

        if (compareVersions (r.minMxjVersion, version::string) > 0)
        {
            error = "The plugin needs MXJ AUDIO " + r.minMxjVersion + " or newer";
            return false;
        }

        if (r.mainFile.isEmpty() || r.mainFile.containsAnyOf ("/\\:") || ! r.mainFile.endsWithIgnoreCase (".lua"))
        {
            error = "main must be the name of a .lua file in the plugin folder";
            return false;
        }

        if (const auto* list = json.getProperty ("permissions", {}).getArray())
            for (const auto& p : *list)
            {
                if (! knownPermissions().contains (p.toString()))
                {
                    error = "The plugin asks for a permission this version does not know: " + p.toString();
                    return false;
                }

                r.permissions.addIfNotAlreadyThere (p.toString());
            }

        if (const auto* list = json.getProperty ("parameters", {}).getArray())
        {
            juce::StringArray ids;
            for (const auto& item : *list)
            {
                dsp::ParameterInfo p;
                if (! readParameter (item, p, error))
                    return false;

                if (ids.contains (p.id))
                {
                    error = "Two parameters have the id '" + p.id + "'";
                    return false;
                }

                ids.add (p.id);
                r.parameters.push_back (p);
            }
        }

        if (r.type == Type::generator && ! [&r]
            {
                for (const auto& p : r.parameters)
                    if (p.id == "duration")
                        return true;
                return false;
            }())
        {
            // Generators need a length: add the usual one.
            r.parameters.insert (r.parameters.begin(), dsp::ParameterInfo::number ("duration", "Duration", 0.1f, 600.0f, 5.0f, "s", 1, 30.0f));
        }

        return true;
    }

    bool LuaPluginInfo::readFolder (const juce::File& folder, LuaPluginInfo& result, juce::String& error)
    {
        const auto file = folder.getChildFile ("plugin.json");
        if (! file.existsAsFile())
        {
            error = "There is no plugin.json in " + folder.getFullPathName();
            return false;
        }

        juce::var json;
        const auto parsed = juce::JSON::parse (file.loadFileAsString(), json);
        if (parsed.failed())
        {
            error = "plugin.json: " + parsed.getErrorMessage();
            return false;
        }

        if (! parse (json, result, error))
            return false;

        if (! folder.getChildFile (result.mainFile).existsAsFile())
        {
            error = result.mainFile + " is missing";
            return false;
        }

        return true;
    }
}
