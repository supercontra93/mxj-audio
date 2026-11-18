// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj::dialogs
{
    /** Asks for one line of text. `onOk` runs with the text if the user presses OK. */
    void askForText (const juce::String& title, const juce::String& message, const juce::String& initialText,
                     std::function<void (const juce::String&)> onOk);

    /** Shows information with an OK button. */
    void showInfo (const juce::String& title, const juce::String& message);

    /** Shows a longer text (a report) that can be selected and copied. */
    void showText (const juce::String& title, const juce::String& text, juce::Component* parent = nullptr);

    /** Shows a warning with an OK button. */
    void showWarning (const juce::String& title, const juce::String& message);

    /** Asks a question with up to three buttons. `onResult` gets 1 for the first
        button, 2 for the second, 0 for the last (or Escape). */
    void ask (const juce::String& title, const juce::String& message,
              const juce::StringArray& buttons, std::function<void (int)> onResult);
}
