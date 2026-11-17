// SPDX-License-Identifier: AGPL-3.0-only
#include "services/settings/Settings.h"
#include "services/settings/SettingKeys.h"

namespace mxj
{
    namespace
    {
        constexpr const char* schemaKey = "schemaVersion";
        constexpr const char* valuesKey = "values";
    }

    const juce::NamedValueSet& Settings::getDefaults()
    {
        static const juce::NamedValueSet defaults = []
        {
            juce::NamedValueSet d;
            d.set (keys::language,           "tr");
            d.set (keys::uiScale,            1.0);
            d.set (keys::windowState,        juce::String());
            // Off by default: update checks only exist in official release builds (phase 8).
            d.set (keys::updateCheckOnStart, false);
            d.set (keys::updateChannel,      "stable");
            d.set (keys::audioDeviceState,   juce::String());
            d.set (keys::lastOpenFolder,     juce::String());
            return d;
        }();

        return defaults;
    }

    Settings::Settings (juce::File f) : file (std::move (f)), values (getDefaults()) {}

    bool Settings::load()
    {
        values = getDefaults();
        damaged = false;

        if (! file.existsAsFile())
            return false;

        const auto parsed = juce::JSON::parse (file.loadFileAsString());
        const auto* root = parsed.getDynamicObject();
        const auto* stored = root != nullptr ? root->getProperty (valuesKey).getDynamicObject() : nullptr;

        if (stored == nullptr)
        {
            damaged = true;
            file.copyFileTo (file.getSiblingFile ("settings.damaged.json"));
            return false;
        }

        for (const auto& [name, value] : stored->getProperties())
            values.set (name, value);

        const int version = root->getProperty (schemaKey);
        if (version < currentSchemaVersion)
            migrate (version);

        return true;
    }

    void Settings::migrate (int fromVersion)
    {
        // Schema 1 is the first version. Future migrations go here, one step at a time:
        // if (fromVersion < 2) { ... }
        juce::ignoreUnused (fromVersion);
    }

    bool Settings::save() const
    {
        auto* stored = new juce::DynamicObject();
        for (const auto& [name, value] : values)
            stored->setProperty (name, value);

        auto* root = new juce::DynamicObject();
        root->setProperty (schemaKey, currentSchemaVersion);
        root->setProperty (valuesKey, juce::var (stored));

        if (! file.getParentDirectory().createDirectory().wasOk())
            return false;

        const auto text = juce::JSON::toString (juce::var (root), juce::JSON::FormatOptions{}.withSpacing (juce::JSON::Spacing::multiLine));

        // replaceWithText writes a temporary file and swaps it in, so a crash
        // while saving never leaves a half-written settings file.
        return file.replaceWithText (text, false, false, "\n");
    }

    juce::var Settings::get (const juce::String& key) const
    {
        if (const auto* v = values.getVarPointer (key))
            return *v;

        return getDefaults()[juce::Identifier (key)];
    }

    juce::String Settings::getString (const juce::String& key) const { return get (key).toString(); }
    bool Settings::getBool (const juce::String& key) const           { return (bool) get (key); }
    double Settings::getDouble (const juce::String& key) const       { return (double) get (key); }

    void Settings::set (const juce::String& key, const juce::var& value)
    {
        if (values.set (key, value) && onChange != nullptr)
            onChange (key);
    }

    void Settings::reset (const juce::String& key)
    {
        const auto& defaults = getDefaults();

        if (defaults.contains (key))
            set (key, defaults[juce::Identifier (key)]);
        else if (values.remove (key) && onChange != nullptr)
            onChange (key);
    }
}
