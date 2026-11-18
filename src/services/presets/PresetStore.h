// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "dsp/effects/EffectCatalogue.h"

namespace mxj
{
    /** The user's own effect presets and effect rack presets, stored as JSON in
        the presets folder (AppPaths::getPresetsFolder()):

            presets/<effect id>.json   {"version": 1, "presets": [{"name", "settings"}]}
            presets/racks.json         {"version": 1, "racks": [{"name", "chain"}]}

        Values are stored by parameter id (EffectSettings::toVar), so presets
        keep working when an effect gains parameters. Factory presets come with
        the effects themselves (EffectType::factoryPresets). Message thread only.
    */
    class PresetStore
    {
    public:
        explicit PresetStore (juce::File folder);

        struct EffectPreset
        {
            juce::String name;
            dsp::EffectSettings settings;
        };

        struct RackPreset
        {
            juce::String name;
            dsp::ChainSettings chain;
        };

        /** The user's presets of one effect, sorted by name. */
        std::vector<EffectPreset> getEffectPresets (const juce::String& effectId) const;

        /** Saves (or replaces, by name) a preset. Returns false if the file cannot be written. */
        bool saveEffectPreset (const juce::String& name, const dsp::EffectSettings& settings);
        bool deleteEffectPreset (const juce::String& effectId, const juce::String& name);

        std::vector<RackPreset> getRackPresets() const;
        bool saveRackPreset (const juce::String& name, const dsp::ChainSettings& chain);
        bool deleteRackPreset (const juce::String& name);

        juce::File getFolder() const { return folder; }

    private:
        juce::File fileFor (const juce::String& effectId) const;
        juce::var load (const juce::File&, const char* listKey) const;
        bool store (const juce::File&, const char* listKey, const juce::Array<juce::var>& items) const;

        juce::File folder;
    };
}
