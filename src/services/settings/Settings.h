// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_core/juce_core.h>
#include <functional>

namespace mxj
{
    /** User settings stored as a JSON file.

        The file carries a schema version. Older files are migrated on load.
        A damaged file is kept aside as settings.damaged.json and defaults are
        used, so a bad file never stops the program from starting.
        Saving writes a temporary file first and then replaces the old one.

        Message thread only.
    */
    class Settings
    {
    public:
        static constexpr int currentSchemaVersion = 1;

        explicit Settings (juce::File file);

        /** Loads the file. Returns false if it was missing or damaged
            (defaults are used in both cases). */
        bool load();

        /** Writes the file. Returns false on failure. */
        bool save() const;

        juce::var get (const juce::String& key) const;
        juce::String getString (const juce::String& key) const;
        bool getBool (const juce::String& key) const;
        double getDouble (const juce::String& key) const;

        /** Sets a value. Unknown keys are allowed so plugins can store values,
            but they must use their own "plugin.<id>." prefix. */
        void set (const juce::String& key, const juce::var& value);

        /** Puts a key back to its default value. */
        void reset (const juce::String& key);

        bool wasLoadedFromDamagedFile() const noexcept { return damaged; }
        juce::File getFile() const { return file; }

        /** Called after any value changes, with the key that changed. */
        std::function<void (const juce::String&)> onChange;

        /** The default value of every known key. */
        static const juce::NamedValueSet& getDefaults();

    private:
        void migrate (int fromVersion);

        juce::File file;
        juce::NamedValueSet values;
        bool damaged = false;
    };
}
