// SPDX-License-Identifier: AGPL-3.0-only
#include "app/editor/EditorPage.h"
#include "app/ui/IconButton.h"
#include "app/theme/Fonts.h"
#include "app/theme/Icons.h"
#include "app/theme/Palette.h"
#include "services/settings/SettingKeys.h"

namespace mxj
{
    namespace
    {
        constexpr int scrollBarHeight = 14;
    }

    //==============================================================================
    EditorPage::EditorPage (Workspace& w, const CommandRegistry& c, RunCommand runCommand, Recorder& recorder, AppContext& ctx)
        : workspace (w), commands (c), waveform (w, c, runCommand, ctx.settings), recordingPanel (recorder), context (ctx),
          rackPanel (w, ctx.presets), meterPanel (ctx.audio.getPlayback().getMeter()), spectrumView (ctx.audio.getPlayback())
    {
        struct Spec { const char* command; const char* icon; bool endsGroup; };
        const Spec specs[] = {
            { "transport.toStart",  "skip-back", false },
            { "transport.playStop", "play", false },
            { "transport.record",   "record-fill", false },
            { "transport.toEnd",    "skip-forward", true },
            { "edit.undo",          "arrow-counter-clockwise", false },
            { "edit.redo",          "arrow-clockwise", true },
            { "edit.cut",           "scissors", false },
            { "edit.copy",          "copy", false },
            { "edit.paste",         "clipboard-text", false },
            { "markers.add",        "map-pin", true },
            { "view.zoomIn",        "magnifying-glass-plus", false },
            { "view.zoomOut",       "magnifying-glass-minus", false },
            { "view.zoomFull",      "arrows-out-line-horizontal", true },
        };

        for (const auto& s : specs)
        {
            if (const auto* info = commands.find (s.command))
            {
                const bool isRecord = info->id == "transport.record";
                auto* b = buttons.add (new IconButton (*info, s.icon, runCommand,
                                                       isRecord ? palette::record : palette::text,
                                                       isRecord ? palette::record : palette::accent));
                addAndMakeVisible (b);

                if (s.endsGroup)
                    groupEnds.add (buttons.size() - 1);

                if (info->id == "transport.playStop")
                    playButton = b;
                if (isRecord)
                    recordButton = b;
            }
        }

        addAndMakeVisible (waveform);
        addChildComponent (recordingPanel);
        addChildComponent (rackPanel);
        addChildComponent (meterPanel);
        addChildComponent (spectrumView);
        updatePanels();

        scrollBar.setAutoHide (false);
        scrollBar.setColour (juce::ScrollBar::thumbColourId, palette::line.brighter (0.15f));
        scrollBar.setColour (juce::ScrollBar::backgroundColourId, palette::panel);
        scrollBar.addListener (this);
        addAndMakeVisible (scrollBar);

        workspace.addChangeListener (this);
        updateFromWorkspace();
        startTimerHz (30);
    }

    int EditorPage::transportHeight()
    {
        return fonts::scaled (44) + 4;
    }

    EditorPage::~EditorPage()
    {
        workspace.removeChangeListener (this);
    }

    void EditorPage::resized()
    {
        auto area = getLocalBounds();
        auto bar = area.removeFromTop (transportHeight()).reduced (12, 8);

        for (int i = 0; i < buttons.size(); ++i)
        {
            buttons[i]->setBounds (bar.removeFromLeft (bar.getHeight()));
            bar.removeFromLeft (groupEnds.contains (i) ? 16 : 4); // wider gap between groups
        }

        bar.removeFromLeft (4);
        timeArea = bar.removeFromLeft (fonts::scaled (150));
        bar.removeFromLeft (16);
        infoArea = bar;

        if (rackPanel.isVisible())
            rackPanel.setBounds (area.removeFromLeft (RackPanel::preferredWidth()));
        if (meterPanel.isVisible())
            meterPanel.setBounds (area.removeFromRight (MeterPanel::preferredWidth()));

        if (spectrumView.isVisible())
        {
            spectrumView.setBounds (area.removeFromBottom (SpectrumView::preferredHeight()));
            area.removeFromBottom (1);
        }

        scrollBar.setBounds (area.removeFromBottom (scrollBarHeight));
        area.removeFromTop (1);
        waveform.setBounds (area);
        recordingPanel.setBounds (area);
    }

    void EditorPage::paint (juce::Graphics& g)
    {
        g.fillAll (palette::background);

        auto bar = getLocalBounds().removeFromTop (transportHeight());
        g.setColour (palette::panel);
        g.fillRect (bar);
        g.setColour (palette::line);
        g.fillRect (bar.removeFromBottom (1));

        g.setColour (palette::accent);
        g.setFont (fonts::mono (20.0f));
        g.drawText (timeText, timeArea, juce::Justification::centredLeft);

        auto info = infoArea;
        const auto selectionText = describeSelection();
        g.setFont (fonts::sans (12.5f));
        g.setColour (palette::textDim);
        g.drawText (describeFile(), selectionText.isEmpty() ? info : info.removeFromTop (info.getHeight() / 2),
                    juce::Justification::centredRight);

        if (selectionText.isNotEmpty())
        {
            g.setColour (palette::playhead);
            g.drawText (selectionText, info, juce::Justification::centredRight);
        }
    }

    juce::String EditorPage::describeFile() const
    {
        const auto* doc = workspace.getDocument();
        if (doc == nullptr)
            return {};

        const auto& info = doc->getInfo();
        const auto numChannels = doc->getNumChannels();
        const auto channels = numChannels == 1 ? TRANS ("Mono")
                            : numChannels == 2 ? TRANS ("Stereo")
                                               : juce::String (numChannels) + " " + TRANS ("channels");

        juce::StringArray parts { doc->getDisplayName() + (doc->isModified() ? " *" : "") };
        if (info.formatName.isNotEmpty())
            parts.add (info.formatName.upToFirstOccurrenceOf (" ", false, false));
        parts.add (juce::String (juce::roundToInt (doc->getSampleRate())) + " Hz");
        if (info.bitsPerSample > 0 && info.isUncompressed)
            parts.add (juce::String (info.bitsPerSample) + " bit");
        parts.add (channels);
        parts.add (Workspace::formatTime (doc->getLength(), doc->getSampleRate()));

        return parts.joinIntoString ("   |   ");
    }

    juce::String EditorPage::describeSelection() const
    {
        const auto* doc = workspace.getDocument();
        const auto sel = workspace.getSelection();
        if (doc == nullptr || sel.isEmpty())
            return {};

        const auto rate = doc->getSampleRate();
        return TRANS ("Selection") + ": " + Workspace::formatTime (sel.getStart(), rate) + " - "
             + Workspace::formatTime (sel.getEnd(), rate) + "   (" + Workspace::formatTime (sel.getLength(), rate) + ")";
    }

    void EditorPage::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        updateFromWorkspace();
    }

    void EditorPage::timerCallback()
    {
        const auto* doc = workspace.getDocument();
        const auto text = doc != nullptr ? Workspace::formatTime (workspace.getPlayheadPosition(), doc->getSampleRate())
                                         : juce::String();
        if (text != timeText)
        {
            timeText = text;
            repaint (timeArea);
        }
    }

    void EditorPage::updatePanels()
    {
        const auto& s = context.settings;
        const bool rack = s.getBool (keys::showRack);
        const bool meters = s.getBool (keys::showMeters);
        const bool spectrum = s.getBool (keys::showSpectrum);

        if (rack != rackPanel.isVisible() || meters != meterPanel.isVisible() || spectrum != spectrumView.isVisible())
        {
            rackPanel.setVisible (rack);
            meterPanel.setVisible (meters);
            spectrumView.setVisible (spectrum);
            resized();
        }
    }

    void EditorPage::updateFromWorkspace()
    {
        updatePanels();

        for (auto* b : buttons)
            b->setEnabled (commands.isEnabled (b->commandId));

        if (playButton != nullptr)
        {
            const bool playing = workspace.isPlaying();
            playButton->setIcon (playing ? "stop" : "play");
            playButton->setHighlighted (playing);
        }

        if (recordButton != nullptr)
            recordButton->setHighlighted (workspace.isRecording());

        recordingPanel.setVisible (workspace.isRecording());
        waveform.setVisible (! workspace.isRecording());

        if (const auto* doc = workspace.getDocument())
        {
            const auto& v = workspace.getView();
            scrollBar.setRangeLimits (0.0, (double) juce::jmax ((juce::int64) 1, doc->getLength()), juce::dontSendNotification);
            scrollBar.setCurrentRange (v.startFrame, v.framesPerPixel * workspace.getViewWidth(), juce::dontSendNotification);
        }

        timerCallback();
        repaint (getLocalBounds().removeFromTop (transportHeight()));
    }

    void EditorPage::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
    {
        auto v = workspace.getView();
        v.startFrame = newRangeStart;
        workspace.setView (v);
    }
}
