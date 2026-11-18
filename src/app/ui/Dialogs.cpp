// SPDX-License-Identifier: AGPL-3.0-only
#include "app/ui/Dialogs.h"
#include "app/theme/Fonts.h"
#include "app/theme/Palette.h"

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

    namespace
    {
        class TextReport : public juce::Component
        {
        public:
            explicit TextReport (const juce::String& text)
            {
                editor.setMultiLine (true);
                editor.setReadOnly (true);
                editor.setScrollbarsShown (true);
                editor.setFont (fonts::mono (13.0f));
                editor.setText (text, false);
                addAndMakeVisible (editor);

                copy.setButtonText (TRANS ("Copy"));
                copy.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (editor.getText()); };
                addAndMakeVisible (copy);

                close.setButtonText (TRANS ("Close"));
                close.onClick = [this]
                {
                    if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
                        w->exitModalState (0);
                };
                addAndMakeVisible (close);
                setSize (fonts::scaled (560), fonts::scaled (420));
            }

            void paint (juce::Graphics& g) override { g.fillAll (palette::panel); }

            void resized() override
            {
                auto area = getLocalBounds().reduced (12);
                auto buttons = area.removeFromBottom (fonts::scaled (28));
                area.removeFromBottom (10);
                editor.setBounds (area);
                close.setBounds (buttons.removeFromRight (fonts::scaled (100)));
                buttons.removeFromRight (8);
                copy.setBounds (buttons.removeFromRight (fonts::scaled (100)));
            }

        private:
            juce::TextEditor editor;
            juce::TextButton copy, close;
        };
    }

    void showText (const juce::String& title, const juce::String& text, juce::Component* parent)
    {
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (new TextReport (text));
        options.dialogTitle = title;
        options.dialogBackgroundColour = palette::panel;
        options.componentToCentreAround = parent;
        options.useNativeTitleBar = true;
        options.resizable = true;
        options.escapeKeyTriggersCloseButton = true;
        options.launchAsync();
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
