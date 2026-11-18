// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "dsp/process/Analysis.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** "Loudness and Levels": the measurements of the selection or the file as a
        table (master plan, module G: amplitude statistics), with a Copy button. */
    class StatsDialog : public juce::Component
    {
    public:
        static void show (const dsp::AudioStats& stats, const juce::String& what, juce::Component* parent);

        StatsDialog (const dsp::AudioStats& stats, juce::String what);

        void paint (juce::Graphics&) override;
        void resized() override;

        /** The table as plain text (for the clipboard and tests). */
        static juce::String toText (const dsp::AudioStats& stats);

    private:
        struct Row { juce::String label, value; bool header = false; };
        static std::vector<Row> rowsFor (const dsp::AudioStats& stats);

        dsp::AudioStats stats;
        juce::String description;
        std::vector<Row> rows;
        juce::TextButton copyButton, closeButton;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StatsDialog)
    };
}
