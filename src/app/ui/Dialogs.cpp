// SPDX-License-Identifier: AGPL-3.0-only
#include "app/ui/Dialogs.h"

namespace mxj::dialogs
{
    void askForText (const juce::String& title, const juce::String& message, const juce::String& initialText,
                     std::function<void (const juce::String&)> onOk)
    {
        auto* window = new juce::AlertWindow (title, message, juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("text", initialText);
        window->addButton (TRANS ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton (TRANS ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

        if (auto* editor = window->getTextEditor ("text"))
        {
            editor->selectAll();
            editor->grabKeyboardFocus();
        }

        window->enterModalState (true, juce::ModalCallbackFunction::create ([window, onOk] (int result)
        {
            const auto text = window->getTextEditorContents ("text");
            if (result == 1 && onOk != nullptr)
                onOk (text);
        }), true); // the window deletes itself
    }

    void showInfo (const juce::String& title, const juce::String& message)
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::InfoIcon,
                                                                              title, message, TRANS ("OK")),
                                      nullptr);
    }

    void showWarning (const juce::String& title, const juce::String& message)
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon,
                                                                              title, message, TRANS ("OK")),
                                      nullptr);
    }

    void ask (const juce::String& title, const juce::String& message,
              const juce::StringArray& buttons, std::function<void (int)> onResult)
    {
        auto options = juce::MessageBoxOptions()
                           .withIconType (juce::MessageBoxIconType::QuestionIcon)
                           .withTitle (title)
                           .withMessage (message);

        // First button 1, second 2, ..., the last one (Cancel) 0.
        for (int i = 0; i < buttons.size(); ++i)
            options = options.withButton (buttons[i], i == buttons.size() - 1 ? 0 : i + 1);

        juce::AlertWindow::showAsync (options, [onResult] (int result)
        {
            if (onResult != nullptr)
                onResult (result);
        });
    }
}
