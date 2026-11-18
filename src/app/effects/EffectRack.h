// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "dsp/effects/EffectCatalogue.h"

#include <juce_events/juce_events.h>

namespace mxj
{
    /** The effect rack (master plan, module E: "build a chain, listen live,
        presets"): an ordered list of effects that can be heard while the file
        plays and then applied to the selection or the whole file.

        Each slot owns shared parameter values and an on/off flag, so the live
        chain on the audio thread follows the controls without being rebuilt.
        Adding, removing or moving slots sends a change message; the workspace
        then builds a new live chain. Message thread only.
    */
    class EffectRack : public juce::ChangeBroadcaster
    {
    public:
        struct Slot
        {
            const dsp::EffectType* type = nullptr;
            dsp::ParameterValuesPtr values;
            std::shared_ptr<std::atomic<bool>> enabled;
        };

        /** Adds an effect at the end. Returns its index, or -1 for an unknown or
            non-real-time type. */
        int add (const juce::String& typeId);
        int add (const dsp::EffectSettings& settings);

        void remove (int index);
        void move (int from, int to);
        void clear();

        void setEnabled (int index, bool shouldBeOn);

        int size() const noexcept                   { return (int) slots.size(); }
        const Slot& operator[] (int index) const    { return slots[(size_t) index]; }

        /** The rack as a value (for presets, the settings file and applying it). */
        dsp::ChainSettings getSettings() const;
        void setSettings (const dsp::ChainSettings&);

        /** A chain for live listening that shares this rack's values and switches. */
        std::unique_ptr<dsp::ProcessorChain> createLiveChain() const;

        /** True if at least one slot is switched on. */
        bool hasActiveEffects() const;

        /** "Listen live": the rack is heard while the file plays. */
        bool isMonitoring() const noexcept { return monitoring; }
        void setMonitoring (bool shouldListen);

        /** The slot the rack panel shows the controls of (-1 = none). */
        int getSelectedSlot() const noexcept { return selected; }
        void setSelectedSlot (int index);

        /** Called (at once, not later) when the live chain must be rebuilt: slots
            added, removed or moved, or listening switched on or off. */
        std::function<void()> onChainChanged;

    private:
        void chainChanged();

        std::vector<Slot> slots;
        bool monitoring = false;
        int selected = -1;
    };
}
