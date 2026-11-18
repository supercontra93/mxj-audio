// SPDX-License-Identifier: AGPL-3.0-only
#include "app/editor/ExportDialog.h"
#include "app/theme/Fonts.h"
#include "app/theme/Palette.h"
#include "services/settings/SettingKeys.h"

namespace mxj
{
    namespace
    {
        constexpr ExportFormat formats[] { ExportFormat::wav, ExportFormat::flac, ExportFormat::ogg,
                                           ExportFormat::opus, ExportFormat::aiff };
        constexpr int padding = 20;
        int rowHeight()  { return fonts::scaled (28); }
        int labelWidth() { return fonts::scaled (130); }
    }

    void ExportDialog::show (Settings& settings, const juce::String& title, const juce::File& suggestedFile,
                             double sampleRate, juce::Component* parent, Callback onChosen)
    {
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (new ExportDialog (settings, suggestedFile, sampleRate, std::move (onChosen)));
        options.dialogTitle = title;
        options.dialogBackgroundColour = palette::panel;
        options.componentToCentreAround = parent;
        options.useNativeTitleBar = true;
        options.resizable = false;
        options.escapeKeyTriggersCloseButton = true;
        options.launchAsync();
    }

    ExportDialog::ExportDialog (Settings& s, juce::File file, double sampleRate, Callback onChosen)
        : settings (s), suggested (std::move (file)), rate (sampleRate), callback (std::move (onChosen))
    {
        int id = 1;
        for (auto f : formats)
            formatBox.addItem (audioExport::getDisplayName (f) + "  (." + audioExport::getExtension (f) + ")", id++);

        int selected = 0;
        for (int i = 0; i < (int) std::size (formats); ++i)
            if (audioExport::getExtension (formats[i]) == settings.getString (keys::exportFormat))
                selected = i;

        formatBox.setSelectedItemIndex (selected, juce::dontSendNotification);
        formatBox.onChange = [this] { formatChanged(); };
        addAndMakeVisible (formatBox);
        addAndMakeVisible (detailBox);

        saveButton.setButtonText (TRANS ("Save..."));
        saveButton.onClick = [this] { chooseFile(); };
        addAndMakeVisible (saveButton);

        cancelButton.setButtonText (TRANS ("Cancel"));
        cancelButton.onClick = [this] { closeDialog(); };
        addAndMakeVisible (cancelButton);

        formatChanged();
        setSize (fonts::scaled (460), fonts::scaled (220));
    }

    ExportFormat ExportDialog::getFormat() const
    {
        return formats[(size_t) juce::jlimit (0, (int) std::size (formats) - 1, formatBox.getSelectedItemIndex())];
    }

    void ExportDialog::formatChanged()
    {
        const auto format = getFormat();
        detailBox.clear (juce::dontSendNotification);
        bitChoices.clear();

        const auto bits = audioExport::getBitDepths (format);
        if (! bits.isEmpty())
        {
            detailLabel = TRANS ("Bit depth");
            int selected = 0;

            for (auto b : bits)
            {
                bitChoices.add (b);
                detailBox.addItem (b == 32 ? TRANS ("32 bit float") : juce::String (b) + " bit", bitChoices.size());
                if (b == (int) settings.get (keys::exportBits))
                    selected = bitChoices.size() - 1;
            }

            if (bitChoices[selected] != (int) settings.get (keys::exportBits))
                selected = bitChoices.indexOf (24);

            detailBox.setSelectedItemIndex (juce::jmax (0, selected), juce::dontSendNotification);
        }
        else
        {
            detailLabel = format == ExportFormat::flac ? TRANS ("Compression") : TRANS ("Quality");
            const auto options = audioExport::getQualityOptions (format);
            for (int i = 0; i < options.size(); ++i)
            {
                bitChoices.add (-1);
                detailBox.addItem (options[i] == "Auto" ? TRANS ("Automatic") : options[i], i + 1);
            }

            detailBox.setSelectedItemIndex (audioExport::getDefaultQualityIndex (format), juce::dontSendNotification);
        }

        note = audioExport::supportsSampleRate (format, rate)
                   ? (format == ExportFormat::wav ? TRANS ("WAV keeps markers as cue points.") : juce::String())
                   : TRANS ("This format does not support the sample rate of the audio.")
                         + " (" + juce::String (juce::roundToInt (rate)) + " Hz)";

        saveButton.setEnabled (audioExport::supportsSampleRate (format, rate));
        repaint();
    }

    void ExportDialog::chooseFile()
    {
        const auto format = getFormat();
        ExportSettings result;
        result.format = format;

        const auto index = detailBox.getSelectedItemIndex();
        if (! audioExport::getBitDepths (format).isEmpty())
        {
            result.bitsPerSample = bitChoices[index];
            result.floatingPoint = result.bitsPerSample == 32;
            settings.set (keys::exportBits, result.bitsPerSample);
        }
        else
        {
            result.qualityIndex = index;
        }

        settings.set (keys::exportFormat, audioExport::getExtension (format));

        const auto ext = "." + audioExport::getExtension (format);
        const auto start = suggested.withFileExtension (ext);

        // Shared, so the chooser outlives this dialog, which closes now.
        auto chooser = std::make_shared<juce::FileChooser> (TRANS ("Save audio file"), start, "*" + ext, true);
        auto cb = callback;
        auto& s = settings;

        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [chooser, cb, result, ext, &s] (const juce::FileChooser& fc) mutable
        {
            auto file = fc.getResult();
            if (file == juce::File())
                return;

            if (! file.hasFileExtension (ext))
                file = file.withFileExtension (ext);

            s.set (keys::lastSaveFolder, file.getParentDirectory().getFullPathName());
            result.file = file;
            cb (result);
        });

        closeDialog();
    }

    void ExportDialog::closeDialog()
    {
        if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
            window->exitModalState (0);
    }

    void ExportDialog::resized()
    {
        auto area = getLocalBounds().reduced (padding);
        area.removeFromTop (6);

        for (auto* box : { &formatBox, &detailBox })
        {
            auto row = area.removeFromTop (rowHeight());
            row.removeFromLeft (labelWidth());
            box->setBounds (row);
            area.removeFromTop (10);
        }

        auto buttons = area.removeFromBottom (rowHeight());
        cancelButton.setBounds (buttons.removeFromRight (100));
        buttons.removeFromRight (10);
        saveButton.setBounds (buttons.removeFromRight (120));
    }

    void ExportDialog::paint (juce::Graphics& g)
    {
        g.fillAll (palette::panel);
        auto area = getLocalBounds().reduced (padding);
        area.removeFromTop (6);
        g.setFont (fonts::sans (13.0f));
        g.setColour (palette::textDim);
        g.drawText (TRANS ("Format"), area.removeFromTop (rowHeight()).removeFromLeft (labelWidth()), juce::Justification::centredLeft);
        area.removeFromTop (10);
        g.drawText (detailLabel, area.removeFromTop (rowHeight()).removeFromLeft (labelWidth()), juce::Justification::centredLeft);
        area.removeFromTop (12);

        g.setColour (saveButton.isEnabled() ? palette::textDim : palette::record);
        g.setFont (fonts::sans (12.0f));
        g.drawFittedText (note, area.removeFromTop (36), juce::Justification::topLeft, 2);
    }
}
