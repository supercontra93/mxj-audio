// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "formats/export/AudioExporter.h"
#include "services/settings/Settings.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** "Save As" and "Export": choose the format and its bit depth or quality,
        then the file name. Remembers the last choice in the settings. */
    class ExportDialog : public juce::Component
    {
    public:
        using Callback = std::function<void (const ExportSettings&)>;

        /** Opens the dialog. `onChosen` runs after the user picked a file. */
        static void show (Settings& settings, const juce::String& title, const juce::File& suggestedFile,
                          double sampleRate, juce::Component* parent, Callback onChosen);

        ExportDialog (Settings&, juce::File suggestedFile, double sampleRate, Callback onChosen);

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void formatChanged();
        void chooseFile();
        ExportFormat getFormat() const;
        void closeDialog();

        Settings& settings;
        juce::File suggested;
        double rate;
        Callback callback;
        juce::ComboBox formatBox, detailBox;
        juce::TextButton saveButton, cancelButton;
        juce::String detailLabel, note;
        juce::Array<int> bitChoices; ///< per detail item: 16, 24, 32 (float), or -1 for quality items

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExportDialog)
    };
}
