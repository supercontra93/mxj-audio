// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "dsp/effects/Parameters.h"

namespace mxj::plugins
{
    /** The plugin.json of an MXJ (Lua) plugin (master plan, section 7):

        {
          "id": "com.example.tremolo",        lower case letters, digits, '.', '-', '_'
          "name": "Tremolo",
          "version": "1.0.0",
          "author": "Jane Doe",
          "license": "MIT",
          "type": "effect",                    effect | generator | analysis | tool
          "category": "Modulation",            optional, shown on the plugin page
          "description": "...",
          "homepage": "https://...",           optional
          "minMxjVersion": "0.6.0",
          "apiVersion": 1,
          "permissions": [],                   "commands" = may run program commands
          "main": "main.lua",                  optional
          "parameters": [ { "id": "rate", "name": "Rate", "type": "number", "min": 0.5, "max": 20,
                            "default": 5, "unit": "Hz", "decimals": 1 }, ... ]
        }

        Parameters are declared; MXJ AUDIO draws their controls.
    */
    struct LuaPluginInfo
    {
        enum class Type { effect, generator, analysis, tool };

        juce::String id, name, version, author, license, category, description, homepage;
        juce::String minMxjVersion = "0.6.0";
        Type type = Type::effect;
        int apiVersion = 1;
        juce::StringArray permissions;
        juce::String mainFile = "main.lua";
        dsp::ParameterList parameters;

        bool needsCommands() const { return permissions.contains ("commands"); }

        static juce::String typeName (Type);

        /** Reads and checks a plugin.json value. Returns false and an English error if it is not valid. */
        static bool parse (const juce::var& json, LuaPluginInfo& result, juce::String& error);

        /** Reads `folder`/plugin.json. */
        static bool readFolder (const juce::File& folder, LuaPluginInfo& result, juce::String& error);
    };

    /** The plugin API version this program offers. Plugins asking for more are refused. */
    inline constexpr int luaApiVersion = 1;

    /** Permissions this program knows. A plugin asking for another is refused. */
    juce::StringArray knownPermissions();

    /** Compares "1.2.10" and "1.10": negative, 0 or positive. Missing parts count as 0. */
    int compareVersions (const juce::String& a, const juce::String& b);

    /** True for a valid plugin id. */
    bool isValidPluginId (const juce::String& id);
}
