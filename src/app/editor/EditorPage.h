// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "app/Workspace.h"
#include "app/editor/RecordingPanel.h"
#include "app/editor/WaveformView.h"
#include "app/effects/RackPanel.h"
#include "app/meters/MeterPanel.h"
#include "app/meters/SpectrumView.h"
#include "app/ui/IconButton.h"
#include "core/commands/CommandRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** The page shown while a file is open: transport and edit bar, waveform
        (or the recording panel while recording) and scroll bar. Buttons run
        commands, so they behave exactly like menus and shortcuts.

        Phase 3 adds three panels the user can show or hide (Window menu): the
        effect rack on the left, the level meters on the right and the spectrum
        analyser under the waveform. Their visibility is kept in the settings. */
    class EditorPage : public juce::Component,
                       private juce::ChangeListener,
                       private juce::ScrollBar::Listener,
                       private juce::Timer
    {
    public:
        using RunCommand = std::function<void (const CommandId&)>;

        EditorPage (Workspace& workspace, const CommandRegistry& commands, RunCommand runCommand, Recorder& recorder,
                    AppContext& context);
        ~EditorPage() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        /** Height of the transport bar (grows with the text size). */
        static int transportHeight();

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
        void timerCallback() override;
        void updateFromWorkspace();
        void updatePanels();

        juce::String describeFile() const;
        juce::String describeSelection() const;

        Workspace& workspace;
        const CommandRegistry& commands;
        juce::OwnedArray<IconButton> buttons;
        IconButton* playButton = nullptr;
        IconButton* recordButton = nullptr;
        juce::Array<int> groupEnds; ///< button indices after which a wider gap follows
        WaveformView waveform;
        RecordingPanel recordingPanel;
        AppContext& context;
        RackPanel rackPanel;
        MeterPanel meterPanel;
        SpectrumView spectrumView;
        juce::ScrollBar scrollBar { false };
        juce::Rectangle<int> timeArea, infoArea;
        juce::String timeText;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorPage)
    };
}
