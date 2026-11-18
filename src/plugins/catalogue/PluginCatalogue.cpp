// SPDX-License-Identifier: AGPL-3.0-only
#include "plugins/catalogue/PluginCatalogue.h"
#include "core/Version.h"

#include <juce_cryptography/juce_cryptography.h>

namespace mxj::plugins
{
    bool PluginCatalogue::Entry::isCompatible() const
    {
        return compareVersions (minMxjVersion.isNotEmpty() ? minMxjVersion : juce::String ("0.6.0"), version::string) <= 0;
    }

    bool PluginCatalogue::load (const juce::File& indexFile, juce::String& error)
    {
        index = indexFile;
        entries.clear();
        name.clear();

        if (! indexFile.existsAsFile())
        {
            error = "The catalogue was not found: " + indexFile.getFullPathName();
            return false;
        }

        juce::var json;
        if (const auto parsed = juce::JSON::parse (indexFile.loadFileAsString(), json); parsed.failed())
        {
            error = "The catalogue is damaged: " + parsed.getErrorMessage();
            return false;
        }

        if ((int) json.getProperty ("format", 0) != 1)
        {
            error = "The catalogue has a format this version does not know";
            return false;
        }

        name = json.getProperty ("name", "").toString();

        if (const auto* list = json.getProperty ("plugins", {}).getArray())
            for (const auto& item : *list)
            {
                Entry e;
                const auto text = [&item] (const char* key) { return item.getProperty (key, {}).toString().trim(); };
                e.id = text ("id");
                e.name = text ("name");
                e.version = text ("version");
                e.author = text ("author");
                e.license = text ("license");
                e.type = text ("type");
                e.category = text ("category");
                e.description = text ("description");
                e.homepage = text ("homepage");
                e.minMxjVersion = text ("minMxjVersion");
                e.package = text ("package");
                e.sha256 = text ("sha256").toLowerCase();
                e.size = (juce::int64) item.getProperty ("size", 0);

                if (const auto* perms = item.getProperty ("permissions", {}).getArray())
                    for (const auto& p : *perms)
                        e.permissions.add (p.toString());

                // Records without an id, a package or a checksum are skipped.
                if (isValidPluginId (e.id) && e.name.isNotEmpty() && e.package.isNotEmpty() && e.sha256.length() == 64)
                    entries.push_back (std::move (e));
            }

        std::sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b) { return a.name.compareIgnoreCase (b.name) < 0; });
        return true;
    }

    const PluginCatalogue::Entry* PluginCatalogue::find (const juce::String& id) const
    {
        for (const auto& e : entries)
            if (e.id == id)
                return &e;

        return nullptr;
    }

    std::vector<const PluginCatalogue::Entry*> PluginCatalogue::search (const juce::String& text, const juce::String& category) const
    {
        const auto words = juce::StringArray::fromTokens (text.toLowerCase(), true);
        std::vector<const Entry*> result;

        for (const auto& e : entries)
        {
            if (category.isNotEmpty() && e.category != category)
                continue;

            const auto haystack = (e.name + " " + e.description + " " + e.author + " " + e.category + " " + e.type).toLowerCase();
            if (std::all_of (words.begin(), words.end(), [&] (const juce::String& w) { return haystack.contains (w); }))
                result.push_back (&e);
        }

        return result;
    }

    juce::StringArray PluginCatalogue::getCategories() const
    {
        juce::StringArray result;
        for (const auto& e : entries)
            if (e.category.isNotEmpty())
                result.addIfNotAlreadyThere (e.category);

        result.sort (true);
        return result;
    }

    juce::String PluginCatalogue::sha256Of (const juce::File& file)
    {
        juce::FileInputStream in (file);
        if (! in.openedOk())
            return {};

        return juce::SHA256 (in).toHexString().removeCharacters (" ").toLowerCase();
    }

    juce::File PluginCatalogue::getVerifiedPackage (const Entry& e, juce::String& error) const
    {
        if (e.package.startsWithIgnoreCase ("http:") || e.package.startsWithIgnoreCase ("https:"))
        {
            error = "Downloading from the online catalogue comes with version 0.9. This version uses the local test catalogue.";
            return {};
        }

        if (e.package.contains ("..") || juce::File::isAbsolutePath (e.package))
        {
            error = "The catalogue points outside its folder";
            return {};
        }

        const auto file = index.getParentDirectory().getChildFile (e.package);
        if (! file.existsAsFile())
        {
            error = "The package was not found: " + file.getFullPathName();
            return {};
        }

        if (e.size > 0 && file.getSize() != e.size)
        {
            error = "The package does not have the size the catalogue lists. It was not installed.";
            return {};
        }

        if (sha256Of (file) != e.sha256)
        {
            error = "The package's SHA-256 checksum does not match the catalogue. It was not installed.";
            return {};
        }

        return file;
    }
}
