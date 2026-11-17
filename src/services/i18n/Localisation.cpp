// SPDX-License-Identifier: AGPL-3.0-only
#include "services/i18n/Localisation.h"

namespace mxj
{
    namespace
    {
        juce::String currentLanguage = "en";
    }

    void Localisation::apply (const juce::String& languageCode, const juce::String& translationFileText)
    {
        if (languageCode == "en" || translationFileText.isEmpty())
        {
            juce::LocalisedStrings::setCurrentMappings (nullptr);
            currentLanguage = "en";
            return;
        }

        juce::LocalisedStrings::setCurrentMappings (new juce::LocalisedStrings (translationFileText, false));
        currentLanguage = languageCode;
    }

    juce::String Localisation::getCurrentLanguage() { return currentLanguage; }
}
