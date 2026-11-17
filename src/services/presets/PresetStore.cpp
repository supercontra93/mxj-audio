// SPDX-License-Identifier: AGPL-3.0-only
#include "services/presets/PresetStore.h"

namespace mxj
{
    namespace
    {
        constexpr int fileVersion = 1;

        juce::String safeFileName (const juce::String& id)
        {
            return juce::File::createLegalFileName (id).removeCharacters ("/\\.");
        }

        template <typename T>
        void sortByName (std::vector<T>& list)
        {
            std::sort (list.begin(), list.end(), [] (const T& a, const T& b) { return a.name.compareNatural (b.name) < 0; });
        }
    }

    PresetStore::PresetStore (juce::File f) : folder (std::move (f)) {}

    juce::File PresetStore::fileFor (const juce::String& effectId) const
    {
        return folder.getChildFile (safeFileName (effectId) + ".json");
    }

    juce::var PresetStore::load (const juce::File& file, const char* listKey) const
    {
        if (! file.existsAsFile())
            return {};

        const auto parsed = juce::JSON::parse (file.loadFileAsString());
        return parsed.getProperty (listKey, {});
    }

    bool PresetStore::store (const juce::File& file, const char* listKey, const juce::Array<juce::var>& items) const
    {
        if (! folder.createDirectory())
            return false;

        auto* root = new juce::DynamicObject();
        root->setProperty ("version", fileVersion);
        root->setProperty (listKey, items);

        // Write a temporary file first, so a crash never leaves half a file.
        const auto temp = file.getSiblingFile (file.getFileName() + ".tmp");
        if (! temp.replaceWithText (juce::JSON::toString (juce::var (root))))
            return false;

        return temp.moveFileTo (file);
    }

    //==============================================================================
    std::vector<PresetStore::EffectPreset> PresetStore::getEffectPresets (const juce::String& effectId) const
    {
        std::vector<EffectPreset> result;

        const auto stored = load (fileFor (effectId), "presets");
        if (const auto* list = stored.getArray())
            for (const auto& item : *list)
            {
                auto settings = dsp::EffectSettings::fromVar (item.getProperty ("settings", {}));
                const auto name = item.getProperty ("name", {}).toString().trim();
                if (name.isNotEmpty() && settings.typeId == effectId)
                    result.push_back ({ name, std::move (settings) });
            }

        sortByName (result);
        return result;
    }

    bool PresetStore::saveEffectPreset (const juce::String& name, const dsp::EffectSettings& settings)
    {
        if (name.trim().isEmpty() || settings.typeId.isEmpty())
            return false;

        juce::Array<juce::var> items;
        for (const auto& p : getEffectPresets (settings.typeId))
            if (p.name != name.trim())
            {
                auto* o = new juce::DynamicObject();
                o->setProperty ("name", p.name);
                o->setProperty ("settings", p.settings.toVar());
                items.add (juce::var (o));
            }

        auto* o = new juce::DynamicObject();
        o->setProperty ("name", name.trim());
        o->setProperty ("settings", settings.toVar());
        items.add (juce::var (o));

        return store (fileFor (settings.typeId), "presets", items);
    }

    bool PresetStore::deleteEffectPreset (const juce::String& effectId, const juce::String& name)
    {
        juce::Array<juce::var> items;
        bool found = false;

        for (const auto& p : getEffectPresets (effectId))
        {
            if (p.name == name)
            {
                found = true;
                continue;
            }

            auto* o = new juce::DynamicObject();
            o->setProperty ("name", p.name);
            o->setProperty ("settings", p.settings.toVar());
            items.add (juce::var (o));
        }

        return found && store (fileFor (effectId), "presets", items);
    }

    //==============================================================================
    std::vector<PresetStore::RackPreset> PresetStore::getRackPresets() const
    {
        std::vector<RackPreset> result;

        const auto stored = load (folder.getChildFile ("racks.json"), "racks");
        if (const auto* list = stored.getArray())
            for (const auto& item : *list)
            {
                const auto name = item.getProperty ("name", {}).toString().trim();
                if (name.isNotEmpty())
                    result.push_back ({ name, dsp::chainFromVar (item.getProperty ("chain", {})) });
            }

        sortByName (result);
        return result;
    }

    bool PresetStore::saveRackPreset (const juce::String& name, const dsp::ChainSettings& chain)
    {
        if (name.trim().isEmpty())
            return false;

        juce::Array<juce::var> items;
        for (const auto& r : getRackPresets())
            if (r.name != name.trim())
            {
                auto* o = new juce::DynamicObject();
                o->setProperty ("name", r.name);
                o->setProperty ("chain", dsp::chainToVar (r.chain));
                items.add (juce::var (o));
            }

        auto* o = new juce::DynamicObject();
        o->setProperty ("name", name.trim());
        o->setProperty ("chain", dsp::chainToVar (chain));
        items.add (juce::var (o));

        return store (folder.getChildFile ("racks.json"), "racks", items);
    }

    bool PresetStore::deleteRackPreset (const juce::String& name)
    {
        juce::Array<juce::var> items;
        bool found = false;

        for (const auto& r : getRackPresets())
        {
            if (r.name == name)
            {
                found = true;
                continue;
            }

            auto* o = new juce::DynamicObject();
            o->setProperty ("name", r.name);
            o->setProperty ("chain", dsp::chainToVar (r.chain));
            items.add (juce::var (o));
        }

        return found && store (folder.getChildFile ("racks.json"), "racks", items);
    }
}
