// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_core/juce_core.h>

namespace mxj
{
    /** User interface language.

        Source strings in the code are English and wrapped in TRANS().
        Each other language is a JUCE translation file (resources/i18n/<code>.txt).
        Adding a language means adding one file; no code changes.
    */
    class Localisation
    {
    public:
        /** Activates a translation. `translationFileText` is the content of the
            language's file; pass an empty string for English. */
        static void apply (const juce::String& languageCode, const juce::String& translationFileText);

        static juce::String getCurrentLanguage();

        /** Codes of the languages the program ships with. */
        static juce::StringArray getAvailableLanguages() { return { "tr", "en" }; }
    };
}
