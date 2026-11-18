// SPDX-License-Identifier: AGPL-3.0-only
#include "app/session/SessionPage.h"
#include "app/editor/EditorPage.h"
#include "app/theme/Fonts.h"
#include "app/theme/Palette.h"
#include "services/settings/SettingKeys.h"

namespace mxj
{
    namespace
    {
        constexpr int scrollBarSize = 14;

        void styleScrollBar (juce::ScrollBar& bar)
        {
            bar.setAutoHide (false);
            bar.setColour (juce::ScrollBar::thumbColourId, palette::line.brighter (0.15f));
            bar.setColour (juce::ScrollBar::backgroundColourId, palette::panel);
        }
    }

    SessionPage::SessionPage (SessionWorkspace& w, const CommandRegistry& c, RunCommand runCommand, AppContext& ctx)
        : workspace (w), commands (c), context (ctx),
          timeline (w, c, runCommand),
          mixer (w, [this] (int trackId)
          {
              if (! context.settings.getBool (keys::showTrackEffects))
              {
                  context.settings.set (keys::showTrackEffects, true);
                  updatePanels();
              }
              trackEffects.showTrack (trackId);
          }),
          trackEffects (w),
          meterPanel (ctx.audio.getSessionPlayback().getMeter())
    {
        struct Spec { const char* command; const char* icon; bool endsGroup; };
        const Spec specs[] = {
            { "transport.toStart",       "skip-back", false },
            { "transport.playStop",      "play", false },
            { "transport.record",        "record-fill", false },
            { "transport.toEnd",         "skip-forward", true },
            { "edit.undo",               "arrow-counter-clockwise", false },
            { "edit.redo",               "arrow-clockwise", true },
            { "multitrack.import",       "music-notes-plus", false },
            { "multitrack.addTrack",     "plus", false },
            { "multitrack.split",        "scissors", true },
            { "multitrack.snap",         "magnet", false },
            { "window.mixer",            "faders", true },
            { "view.zoomIn",             "magnifying-glass-plus", false },
            { "view.zoomOut",            "magnifying-glass-minus", false },
            { "view.zoomFull",           "arrows-out-line-horizontal", true },
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

                if (info->id == "transport.playStop")  playButton = b;
                if (isRecord)                          recordButton = b;
                if (info->id == "multitrack.snap")     snapButton = b;
                if (info->id == "window.mixer")        mixerButton = b;
            }
        }

        addAndMakeVisible (timeline);
        addChildComponent (mixer);
        addChildComponent (trackEffects);
        addChildComponent (meterPanel);

        styleScrollBar (horizontal);
        styleScrollBar (vertical);
        horizontal.addListener (this);
        vertical.addListener (this);
        addAndMakeVisible (horizontal);
        addAndMakeVisible (vertical);

        trackEffects.showTrack (workspace.getSelectedTrack());
        updatePanels();

        workspace.addChangeListener (this);
        updateFromWorkspace();
        startTimerHz (30);
    }

    SessionPage::~SessionPage()
    {
        workspace.removeChangeListener (this);
    }

    void SessionPage::resized()
    {
        auto area = getLocalBounds();
        auto bar = area.removeFromTop (EditorPage::transportHeight()).reduced (12, 8);

        for (int i = 0; i < buttons.size(); ++i)
        {
            buttons[i]->setBounds (bar.removeFromLeft (bar.getHeight()));
            bar.removeFromLeft (groupEnds.contains (i) ? 16 : 4);
        }

        bar.removeFromLeft (4);
        timeArea = bar.removeFromLeft (fonts::scaled (150));
        bar.removeFromLeft (16);
        infoArea = bar;

        if (meterPanel.isVisible())
            meterPanel.setBounds (area.removeFromRight (MeterPanel::preferredWidth()));

        if (trackEffects.isVisible())
        {
            trackEffects.setBounds (area.removeFromRight (TrackEffectsPanel::preferredWidth()));
            area.removeFromRight (1);
        }

        if (mixer.isVisible())
        {
            mixer.setBounds (area.removeFromBottom (juce::jmin (MixerPanel::preferredHeight(), area.getHeight() / 2)));
            area.removeFromBottom (1);
        }

        auto bottom = area.removeFromBottom (scrollBarSize);
        bottom.removeFromRight (scrollBarSize);
        horizontal.setBounds (bottom.withTrimmedLeft (TimelineView::headerWidth()));
        area.removeFromTop (1);
        vertical.setBounds (area.removeFromRight (scrollBarSize).withTrimmedTop (TimelineView::rulerHeight()));
        timeline.setBounds (area);
        updateScrollBars();
    }

    void SessionPage::paint (juce::Graphics& g)
    {
        g.fillAll (palette::background);

        auto bar = getLocalBounds().removeFromTop (EditorPage::transportHeight());
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
        g.drawText (describeSession(), selectionText.isEmpty() ? info : info.removeFromTop (info.getHeight() / 2),
                    juce::Justification::centredRight);

        if (selectionText.isNotEmpty())
        {
            g.setColour (palette::playhead);
            g.drawText (selectionText, info, juce::Justification::centredRight);
        }
    }

    juce::String SessionPage::describeSession() const
    {
        if (! workspace.hasSession())
            return {};

        const auto& s = workspace.getSession();
        int buses = 0;
        for (const auto& t : s.tracks)
            if (t.kind == Track::Kind::bus)
                ++buses;

        juce::StringArray parts { workspace.getDisplayName() + (workspace.isModified() ? " *" : "") };
        parts.add (TRANS ("Multitrack"));
        parts.add (juce::String (juce::roundToInt (s.sampleRate)) + " Hz");
        parts.add (juce::String (s.getNumAudioTracks()) + " " + TRANS ("tracks"));
        if (buses > 0)
            parts.add (juce::String (buses) + " " + TRANS ("buses"));
        parts.add (Workspace::formatTime (s.getLength(), s.sampleRate));

        return parts.joinIntoString ("   |   ");
    }

    juce::String SessionPage::describeSelection() const
    {
        if (! workspace.hasSession())
            return {};

        const auto rate = workspace.getSampleRate();
        const auto sel = workspace.getTimeSelection();
        if (! sel.isEmpty())
            return TRANS ("Selection") + ": " + Workspace::formatTime (sel.getStart(), rate) + " - "
                 + Workspace::formatTime (sel.getEnd(), rate) + "   (" + Workspace::formatTime (sel.getLength(), rate) + ")";

        const auto numClips = workspace.getSelectedClips().size();
        if (numClips == 1)
            if (const auto* clip = workspace.getSession().findClip (workspace.getSelectedClips().front()))
                return TRANS ("Clip") + ": " + clip->name + "   " + Workspace::formatTime (clip->position, rate)
                     + "   (" + Workspace::formatTime (clip->length, rate) + ")";

        if (numClips > 1)
            return juce::String ((int) numClips) + " " + TRANS ("clips selected");

        return {};
    }

    void SessionPage::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        updateFromWorkspace();
    }

    void SessionPage::timerCallback()
    {
        const auto text = workspace.hasSession() ? Workspace::formatTime (workspace.getPlayheadPosition(), workspace.getSampleRate())
                                                 : juce::String();
        if (text != timeText)
        {
            timeText = text;
            repaint (timeArea);
        }
    }

    void SessionPage::updatePanels()
    {
        const auto& s = context.settings;
        const bool mixerShown = s.getBool (keys::showMixer);
        const bool effectsShown = s.getBool (keys::showTrackEffects);
        const bool metersShown = s.getBool (keys::showMeters);

        if (mixerShown != mixer.isVisible() || effectsShown != trackEffects.isVisible() || metersShown != meterPanel.isVisible())
        {
            mixer.setVisible (mixerShown);
            trackEffects.setVisible (effectsShown);
            meterPanel.setVisible (metersShown);
            resized();
        }
    }

    void SessionPage::updateScrollBars()
    {
        if (! workspace.hasSession())
            return;

        const auto& v = workspace.getView();
        const auto visibleFrames = v.framesPerPixel * juce::jmax (1, timeline.getWidth() - TimelineView::headerWidth());
        horizontal.setRangeLimits (0.0, juce::jmax ((double) workspace.getTimelineLength(), v.startFrame + visibleFrames),
                                   juce::dontSendNotification);
        horizontal.setCurrentRange (v.startFrame, visibleFrames, juce::dontSendNotification);

        const auto visibleHeight = juce::jmax (1, timeline.getHeight() - TimelineView::rulerHeight());
        vertical.setRangeLimits (0.0, (double) juce::jmax (timeline.getContentHeight(), visibleHeight), juce::dontSendNotification);
        vertical.setCurrentRange ((double) v.scrollY, (double) visibleHeight, juce::dontSendNotification);
    }

    void SessionPage::updateFromWorkspace()
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
        if (snapButton != nullptr)
            snapButton->setHighlighted (workspace.isSnapping());
        if (mixerButton != nullptr)
            mixerButton->setHighlighted (mixer.isVisible());

        // The effects panel follows the selected track (and shows the master
        // when the FX button of the master strip is pressed).
        if (workspace.hasSession() && workspace.getSelectedTrack() != followedTrack)
        {
            followedTrack = workspace.getSelectedTrack();
            if (workspace.getSession().findTrack (followedTrack) != nullptr)
                trackEffects.showTrack (followedTrack);
        }

        updateScrollBars();
        timerCallback();
        repaint (getLocalBounds().removeFromTop (EditorPage::transportHeight()));
    }

    void SessionPage::scrollBarMoved (juce::ScrollBar* bar, double newRangeStart)
    {
        auto v = workspace.getView();

        if (bar == &horizontal)
            v.startFrame = newRangeStart;
        else
            v.scrollY = juce::roundToInt (newRangeStart);

        workspace.setView (v);
    }
}
