// SPDX-License-Identifier: AGPL-3.0-only
#include "app/effects/EffectRack.h"

#include <optional>

namespace mxj
{
    int EffectRack::add (const juce::String& typeId)
    {
        return add (dsp::EffectSettings::defaultsFor (typeId));
    }

    namespace
    {
        /** A slot for these settings. Hosted plugins get a name for their live
            plugin, so the mixer plays the same plugin the window shows. */
        std::optional<EffectRack::Slot> makeSlot (dsp::EffectSettings settings)
        {
            const auto* type = dsp::catalogue::find (settings.typeId);
            if (type == nullptr || ! type->isRealtime())
                return std::nullopt;

            if (type->isHostedPlugin() && settings.instance.isEmpty())
                settings.instance = juce::Uuid().toString();

            EffectRack::Slot slot;
            slot.type = type;
            slot.values = settings.createValues (true);
            slot.enabled = std::make_shared<std::atomic<bool>> (settings.enabled);
            slot.instance = type->isHostedPlugin() ? settings.instance : juce::String();
            return slot;
        }
    }

    int EffectRack::add (const dsp::EffectSettings& settings)
    {
        auto slot = makeSlot (settings);
        if (! slot.has_value())
            return -1;

        slots.push_back (std::move (*slot));

        selected = (int) slots.size() - 1;
        chainChanged();
        return selected;
    }

    void EffectRack::remove (int index)
    {
        if (index < 0 || index >= size())
            return;

        slots.erase (slots.begin() + index);
        selected = juce::jmin (selected, size() - 1);
        chainChanged();
    }

    void EffectRack::move (int from, int to)
    {
        if (from < 0 || from >= size() || to < 0 || to >= size() || from == to)
            return;

        auto slot = slots[(size_t) from];
        slots.erase (slots.begin() + from);
        slots.insert (slots.begin() + to, std::move (slot));

        if (selected == from)
            selected = to;

        chainChanged();
    }

    void EffectRack::clear()
    {
        slots.clear();
        selected = -1;
        chainChanged();
    }

    void EffectRack::setEnabled (int index, bool shouldBeOn)
    {
        if (index >= 0 && index < size())
        {
            slots[(size_t) index].enabled->store (shouldBeOn);
            sendChangeMessage(); // saved with the rack; the live chain reads the flag directly
        }
    }

    dsp::ChainSettings EffectRack::getSettings() const
    {
        dsp::ChainSettings chain;
        for (const auto& s : slots)
            chain.push_back ({ s.type->id, s.values->getAll(), s.enabled->load(),
                               s.values->state != nullptr ? s.values->state->save() : juce::String(), s.instance });

        return chain;
    }

    void EffectRack::setSettings (const dsp::ChainSettings& chain)
    {
        slots.clear();
        for (const auto& s : chain)
            if (auto slot = makeSlot (s))
                slots.push_back (std::move (*slot));

        selected = slots.empty() ? -1 : 0;
        chainChanged();
    }

    std::unique_ptr<dsp::ProcessorChain> EffectRack::createLiveChain() const
    {
        auto chain = std::make_unique<dsp::ProcessorChain>();
        for (const auto& s : slots)
            chain->add (s.type->create (s.values), s.enabled);

        return chain;
    }

    bool EffectRack::hasActiveEffects() const
    {
        for (const auto& s : slots)
            if (s.enabled->load())
                return true;

        return false;
    }

    void EffectRack::setMonitoring (bool shouldListen)
    {
        if (monitoring != shouldListen)
        {
            monitoring = shouldListen;
            chainChanged();
        }
    }

    void EffectRack::setSelectedSlot (int index)
    {
        index = juce::jlimit (-1, size() - 1, index);
        if (selected != index)
        {
            selected = index;
            sendChangeMessage();
        }
    }

    void EffectRack::chainChanged()
    {
        if (onChainChanged != nullptr)
            onChainChanged();

        sendChangeMessage();
    }
}
