// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "plugins/lua/LuaManifest.h"

namespace mxj::plugins
{
    /** The community plugin catalogue (master plan, section 7): an index of MXJ
        plugins, one JSON record each, with the package to download and its
        SHA-256 checksum.

        The real catalogue is a GitHub repository that opens with the release
        (phase 8). Until then the program reads a local test catalogue that
        comes with it (the `catalog` folder next to the program), so nothing is
        downloaded (CLAUDE.md, rule 9). index.json:

        { "format": 1, "name": "...",
          "plugins": [ { "id", "name", "version", "author", "license", "type", "category",
                         "description", "homepage", "minMxjVersion", "permissions": [],
                         "package": "packages/x.mxjplugin", "sha256": "...", "size": 1234 } ] }
    */
    class PluginCatalogue
    {
    public:
        struct Entry
        {
            juce::String id, name, version, author, license, type, category, description, homepage, minMxjVersion;
            juce::StringArray permissions;
            juce::String package, sha256;
            juce::int64 size = 0;

            /** True if this version of MXJ AUDIO can run it. */
            bool isCompatible() const;
        };

        /** Reads index.json. Returns false with an error. */
        bool load (const juce::File& indexFile, juce::String& error);

        const std::vector<Entry>& getEntries() const noexcept { return entries; }
        const Entry* find (const juce::String& id) const;
        juce::String getName() const { return name; }
        juce::File getIndexFile() const { return index; }

        /** Entries whose name, description, author or category contain all words of `search`,
            in `category` (empty = all). */
        std::vector<const Entry*> search (const juce::String& search, const juce::String& category) const;
        juce::StringArray getCategories() const;

        /** The package of an entry, checked: it must exist, have the listed size and SHA-256.
            Returns the file to install, or an empty file with an error. */
        juce::File getVerifiedPackage (const Entry& entry, juce::String& error) const;

        /** SHA-256 of a file, as 64 lower case hex digits. */
        static juce::String sha256Of (const juce::File& file);

    private:
        juce::File index;
        juce::String name;
        std::vector<Entry> entries;
    };
}
