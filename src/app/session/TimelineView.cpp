// SPDX-License-Identifier: AGPL-3.0-only
#include "app/session/TimelineView.h"
#include "app/theme/Fonts.h"
#include "app/theme/Palette.h"
#include "app/ui/Dialogs.h"
#include "dsp/effects/Parameters.h"
#include "formats/import/AudioFileFormats.h"

namespace mxj
{
    namespace
    {
        constexpr double rulerSteps[] { 0.01, 0.05, 0.1, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1800, 3600 };
        constexpr int minPixelsPerLabel = 90;
        constexpr int edgeGrab = 6;
        constexpr int handleSize = 8;
        constexpr float minVolumeDb = -48.0f, maxVolumeDb = 12.0f;

        juce::String formatRulerTime (double seconds, double step)
        {
            const auto total = (int) std::floor (seconds);
            const auto m = total / 60;
            const auto s = total % 60;
            auto text = juce::String (m) + ":" + juce::String (s).paddedLeft ('0', 2);

            if (step < 1.0)
                text << "." << juce::String ((int) std::round ((seconds - total) * 100.0)).paddedLeft ('0', 2);

            return text;
        }

        juce::Colour trackColour (const Track& t)
        {
            if (t.isBus())
                return palette::textDim;

            const auto count = (int) std::size (palette::tracks);
            return palette::tracks[((t.colour % count) + count) % count];
        }
    }

    //==============================================================================
    /** The header of one track: name, buttons, volume and pan. */
    class TimelineView::Header : public juce::Component
    {
    public:
        Header (SessionWorkspace& w, int id) : trackId (id), workspace (w)
        {
            for (auto* b : { &muteButton, &soloButton, &armButton, &autoButton })
            {
                b->setClickingTogglesState (false);
                b->setWantsKeyboardFocus (false);
                addAndMakeVisible (b);
            }

            muteButton.setButtonText ("M");
            soloButton.setButtonText ("S");
            armButton.setButtonText ("R");
            autoButton.setButtonText ("A");
            muteButton.setTooltip (TRANS ("Mute"));
            soloButton.setTooltip (TRANS ("Solo: hear only this track"));
            armButton.setTooltip (TRANS ("Arm for recording: Record records onto this track"));
            autoButton.setTooltip (TRANS ("Show volume and pan automation"));
            muteButton.setColour (juce::TextButton::buttonOnColourId, palette::accent);
            soloButton.setColour (juce::TextButton::buttonOnColourId, palette::meterGreen);
            armButton.setColour (juce::TextButton::buttonOnColourId, palette::record);
            autoButton.setColour (juce::TextButton::buttonOnColourId, palette::playhead);

            muteButton.onClick = [this] { workspace.toggleMute (trackId); };
            soloButton.onClick = [this] { workspace.toggleSolo (trackId); };
            armButton.onClick = [this] { workspace.toggleArm (trackId); };
            autoButton.onClick = [this] { workspace.toggleAutomation (trackId); };

            volume.setSliderStyle (juce::Slider::LinearHorizontal);
            volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            volume.setRange (-60.0, 12.0, 0.1);
            volume.setSkewFactorFromMidPoint (-12.0);
            volume.setDoubleClickReturnValue (true, 0.0);
            volume.setColour (juce::Slider::thumbColourId, palette::accent);
            volume.setColour (juce::Slider::trackColourId, palette::accentDark);
            volume.setColour (juce::Slider::backgroundColourId, palette::line);
            volume.onValueChange = [this] { workspace.setTrackVolume (trackId, toDb (volume.getValue()), ! volume.isMouseButtonDown()); };
            volume.onDragEnd = [this] { workspace.setTrackVolume (trackId, toDb (volume.getValue()), true); };
            volume.textFromValueFunction = [] (double v) { return v <= -60.0 ? juce::String ("-inf dB") : juce::String (v, 1) + " dB"; };
            volume.setPopupDisplayEnabled (true, true, nullptr);
            addAndMakeVisible (volume);

            pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            pan.setRange (-1.0, 1.0, 0.01);
            pan.setDoubleClickReturnValue (true, 0.0);
            pan.setColour (juce::Slider::rotarySliderFillColourId, palette::accent);
            pan.setColour (juce::Slider::rotarySliderOutlineColourId, palette::line);
            pan.onValueChange = [this] { workspace.setTrackPan (trackId, (float) pan.getValue(), ! pan.isMouseButtonDown()); };
            pan.onDragEnd = [this] { workspace.setTrackPan (trackId, (float) pan.getValue(), true); };
            pan.textFromValueFunction = [] (double v)
            {
                if (std::abs (v) < 0.005) return juce::String ("C");
                return (v < 0 ? "L" : "R") + juce::String (juce::roundToInt (std::abs (v) * 100.0));
            };
            pan.setPopupDisplayEnabled (true, true, nullptr);
            pan.setTooltip (TRANS ("Pan"));
            addAndMakeVisible (pan);
        }

        static float toDb (double v) { return v <= -60.0 ? -96.0f : (float) v; }

        void update (const Track& t, bool selected)
        {
            track = t;
            isSelected = selected;
            muteButton.setToggleState (t.mute, juce::dontSendNotification);
            soloButton.setToggleState (t.solo, juce::dontSendNotification);
            armButton.setToggleState (t.armed, juce::dontSendNotification);
            autoButton.setToggleState (t.showAutomation, juce::dontSendNotification);
            armButton.setVisible (t.isAudio());

            if (! volume.isMouseButtonDown())
                volume.setValue (juce::jmax (-60.0f, t.volumeDb), juce::dontSendNotification);
            if (! pan.isMouseButtonDown())
                pan.setValue (t.pan, juce::dontSendNotification);

            volume.setEnabled (! t.volumeAutomation.isActive());
            pan.setEnabled (! t.panAutomation.isActive());
            volume.setTooltip (t.volumeAutomation.isActive() ? TRANS ("Volume follows the automation curve") : TRANS ("Volume"));
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds();
            g.setColour (isSelected ? palette::panelRaised : palette::panel);
            g.fillRect (b);
            g.setColour (palette::line);
            g.fillRect (b.removeFromBottom (1));
            g.fillRect (b.removeFromRight (1));
            g.setColour (trackColour (track));
            g.fillRect (b.removeFromLeft (4));

            b.reduce (8, 4);
            auto top = b.removeFromTop (fonts::scaled (20));
            g.setFont (fonts::sansBold (13.0f));
            g.setColour (palette::text);
            if (track.isBus())
            {
                g.setColour (palette::playhead);
                g.setFont (fonts::mono (10.0f));
                g.drawText ("BUS", top.removeFromRight (30), juce::Justification::centredRight);
                g.setFont (fonts::sansBold (13.0f));
                g.setColour (palette::text);
            }
            g.drawText (track.name, top, juce::Justification::centredLeft, true);

            // Level meter along the bottom edge.
            const auto [l, r] = workspace.getLevel (trackId);
            const auto level = juce::jmax (l, r);
            if (level > 0.0f)
            {
                auto meter = getLocalBounds().reduced (8, 0).removeFromBottom (4).translated (0, -2);
                const auto db = dsp::gainToDb (level);
                const auto w = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
                g.setColour (db > -0.5f ? palette::record : db > -9.0f ? palette::accent : palette::meterGreen);
                g.fillRect (meter.removeFromLeft (juce::roundToInt ((float) meter.getWidth() * w)));
            }
        }

        void resized() override
        {
            auto b = getLocalBounds().reduced (8, 4);
            b.removeFromLeft (4);
            b.removeFromTop (fonts::scaled (22));
            auto buttons = b.removeFromTop (fonts::scaled (22));
            const auto w = fonts::scaled (24);
            for (auto* button : { &muteButton, &soloButton, &armButton, &autoButton })
            {
                button->setBounds (buttons.removeFromLeft (w));
                buttons.removeFromLeft (3);
            }

            pan.setBounds (buttons.removeFromRight (fonts::scaled (26)).withSizeKeepingCentre (fonts::scaled (26), fonts::scaled (26)));
            b.removeFromTop (4);
            volume.setBounds (b.removeFromTop (fonts::scaled (20)));
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            workspace.selectTrack (trackId);

            if (e.mods.isPopupMenu())
                showMenu();
        }

        void mouseDoubleClick (const juce::MouseEvent&) override
        {
            rename();
        }

        void rename()
        {
            dialogs::askForText (TRANS ("Rename Track"), TRANS ("Name of the track:"), track.name,
                                 [weak = juce::Component::SafePointer<Header> (this)] (const juce::String& text)
            {
                if (weak != nullptr)
                    weak->workspace.setTrackName (weak->trackId, text);
            });
        }

        void showMenu()
        {
            const auto& s = workspace.getSession();
            juce::PopupMenu menu;
            menu.addItem (TRANS ("Rename Track..."), [this] { rename(); });

            if (track.isAudio())
            {
                juce::PopupMenu output, send;
                output.addItem (TRANS ("Master"), true, track.outputId == 0, [this] { workspace.setTrackOutput (trackId, 0); });
                send.addItem (TRANS ("No Send"), true, track.sends.empty(), [this] { workspace.setSendBus (trackId, 0); });

                for (const auto& b : s.tracks)
                {
                    if (! b.isBus())
                        continue;

                    const auto busId = b.id;
                    output.addItem (b.name, true, track.outputId == busId, [this, busId] { workspace.setTrackOutput (trackId, busId); });
                    send.addItem (b.name, true, ! track.sends.empty() && track.sends.front().busId == busId,
                                  [this, busId] { workspace.setSendBus (trackId, busId); });
                }

                menu.addSubMenu (TRANS ("Output"), output);
                menu.addSubMenu (TRANS ("Send"), send);
            }

            menu.addItem (TRANS ("Clear Automation"), track.volumeAutomation.isActive() || track.panAutomation.isActive(),
                          false, [this] { workspace.clearAutomation (trackId); });
            menu.addSeparator();
            menu.addItem (TRANS ("Move Track Up"), [this] { workspace.moveSelectedTrack (-1); });
            menu.addItem (TRANS ("Move Track Down"), [this] { workspace.moveSelectedTrack (1); });
            menu.addSeparator();
            menu.addItem (TRANS ("Remove Track"), [this] { workspace.removeSelectedTrack(); });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
        }

        const int trackId;

    private:
        SessionWorkspace& workspace;
        Track track;
        bool isSelected = false;
        juce::TextButton muteButton, soloButton, armButton, autoButton;
        juce::Slider volume, pan;
    };

    //==============================================================================
    int TimelineView::headerWidth()  { return fonts::scaled (220); }
    int TimelineView::rulerHeight()  { return fonts::scaled (26); }
    int TimelineView::trackHeight()  { return fonts::scaled (96); }
    int TimelineView::laneHeight()   { return fonts::scaled (46); }

    TimelineView::TimelineView (SessionWorkspace& w, const CommandRegistry& c, RunCommand r)
        : workspace (w), commands (c), run (std::move (r))
    {
        workspace.addChangeListener (this);
        setWantsKeyboardFocus (false);
        updateRows();
        startTimerHz (30);
    }

    TimelineView::~TimelineView()
    {
        workspace.removeChangeListener (this);
    }

    float TimelineView::frameToX (juce::int64 frame) const
    {
        return (float) timelineLeft() + (float) workspace.frameToX (frame);
    }

    juce::int64 TimelineView::xToFrame (int x) const
    {
        return workspace.xToFrame ((double) (x - timelineLeft()));
    }

    int TimelineView::getContentHeight() const
    {
        if (! workspace.hasSession())
            return 0;

        int h = 0;
        for (const auto& t : workspace.getSession().tracks)
            h += trackHeight() + (t.showAutomation ? 2 * laneHeight() : 0);

        return h + trackHeight(); // room below the last track
    }

    void TimelineView::updateRows()
    {
        rows.clear();
        if (! workspace.hasSession())
            return;

        auto y = rulerHeight() - workspace.getView().scrollY;
        for (const auto& t : workspace.getSession().tracks)
        {
            Row r;
            r.trackId = t.id;
            r.y = y;
            r.clipsHeight = trackHeight();
            r.automation = t.showAutomation;
            r.totalHeight = trackHeight() + (t.showAutomation ? 2 * laneHeight() : 0);
            rows.push_back (r);
            y += r.totalHeight;
        }
    }

    void TimelineView::updateHeaders()
    {
        if (! workspace.hasSession())
        {
            headers.clear();
            return;
        }

        const auto& s = workspace.getSession();
        bool same = headers.size() == (int) s.tracks.size();
        for (int i = 0; same && i < headers.size(); ++i)
            same = headers[i]->trackId == s.tracks[(size_t) i].id;

        if (! same)
        {
            headers.clear();
            for (const auto& t : s.tracks)
                addAndMakeVisible (headers.add (new Header (workspace, t.id)));
        }

        for (size_t i = 0; i < rows.size(); ++i)
        {
            auto* h = headers[(int) i];
            h->update (s.tracks[i], s.tracks[i].id == workspace.getSelectedTrack());
            h->setBounds (0, rows[i].y, headerWidth(), trackHeight());
            h->setVisible (rows[i].y + trackHeight() > rulerHeight() && rows[i].y < getHeight());
        }
    }

    void TimelineView::resized()
    {
        workspace.setViewWidth (getWidth() - timelineLeft());
        updateRows();
        updateHeaders();
    }

    void TimelineView::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        updateRows();
        updateHeaders();
        repaint();
    }

    void TimelineView::timerCallback()
    {
        if (workspace.isPlaying() || workspace.isRecording())
        {
            repaint();
            for (auto* h : headers)
                h->repaint();
        }
    }

    //==============================================================================
    const TimelineView::Row* TimelineView::rowAt (int y) const
    {
        for (const auto& r : rows)
            if (y >= r.y && y < r.y + r.totalHeight)
                return &r;

        return nullptr;
    }

    juce::Rectangle<int> TimelineView::clipBounds (const Row& r, const Clip& c) const
    {
        const auto x0 = juce::roundToInt (frameToX (c.position));
        const auto x1 = juce::roundToInt (frameToX (c.getEnd()));
        return { x0, r.y + 2, juce::jmax (2, x1 - x0), r.clipsHeight - 4 };
    }

    juce::Rectangle<int> TimelineView::laneBounds (const Row& r, int lane) const
    {
        return { timelineLeft(), r.y + r.clipsHeight + lane * laneHeight(), getWidth() - timelineLeft(), laneHeight() };
    }

    float TimelineView::laneValueToY (juce::Rectangle<int> lane, int laneIndex, float value) const
    {
        const auto area = lane.reduced (0, 5).toFloat();
        const auto norm = laneIndex == 0 ? (value - minVolumeDb) / (maxVolumeDb - minVolumeDb) : (1.0f - value) * 0.5f;
        return area.getBottom() - juce::jlimit (0.0f, 1.0f, laneIndex == 0 ? norm : 1.0f - norm) * area.getHeight();
    }

    float TimelineView::laneYToValue (juce::Rectangle<int> lane, int laneIndex, int y) const
    {
        const auto area = lane.reduced (0, 5).toFloat();
        const auto norm = juce::jlimit (0.0f, 1.0f, (area.getBottom() - (float) y) / juce::jmax (1.0f, area.getHeight()));
        if (laneIndex == 0)
            return minVolumeDb + norm * (maxVolumeDb - minVolumeDb);

        return juce::jlimit (-1.0f, 1.0f, 1.0f - 2.0f * (1.0f - norm));
    }

    TimelineView::Hit TimelineView::hitTest (juce::Point<int> p) const
    {
        Hit hit;
        const auto* row = rowAt (p.y);
        if (row == nullptr || p.x < timelineLeft() || p.y < rulerHeight())
            return hit;

        hit.trackId = row->trackId;
        const auto* track = workspace.getSession().findTrack (row->trackId);
        if (track == nullptr)
            return hit;

        // Automation lanes
        if (p.y >= row->y + row->clipsHeight)
        {
            hit.lane = (p.y - row->y - row->clipsHeight) / laneHeight();
            hit.action = Drag::automation;
            const auto lane = laneBounds (*row, hit.lane);
            const auto& points = (hit.lane == 0 ? track->volumeAutomation : track->panAutomation).points;

            for (size_t i = 0; i < points.size(); ++i)
            {
                const juce::Point<float> pt (frameToX (points[i].frame), laneValueToY (lane, hit.lane, points[i].value));
                if (pt.getDistanceFrom (p.toFloat()) <= 6.0f)
                    hit.point = (int) i;
            }

            return hit;
        }

        // Clips, the later one first (it is drawn on top).
        for (auto it = track->clips.rbegin(); it != track->clips.rend(); ++it)
        {
            const auto r = clipBounds (*row, *it);
            if (! r.expanded (2, 0).contains (p))
                continue;

            hit.clipId = it->id;
            const auto fadeInX = (int) frameToX (it->position + it->fadeIn);
            const auto fadeOutX = (int) frameToX (it->getEnd() - it->fadeOut);

            if (p.y < r.getY() + handleSize + 6 && std::abs (p.x - fadeInX) <= handleSize)
                hit.action = Drag::fadeIn;
            else if (p.y < r.getY() + handleSize + 6 && std::abs (p.x - fadeOutX) <= handleSize)
                hit.action = Drag::fadeOut;
            else if (p.x - r.getX() <= edgeGrab && r.getWidth() > 3 * edgeGrab)
                hit.action = Drag::trimStart;
            else if (r.getRight() - p.x <= edgeGrab && r.getWidth() > 3 * edgeGrab)
                hit.action = Drag::trimEnd;
            else
                hit.action = Drag::moveClips;

            return hit;
        }

        hit.action = Drag::select;
        return hit;
    }

    //==============================================================================
    void TimelineView::paint (juce::Graphics& g)
    {
        g.fillAll (palette::background);

        if (! workspace.hasSession())
            return;

        const auto& s = workspace.getSession();
        const auto clip = g.getClipBounds();

        for (const auto& r : rows)
            if (r.y + r.totalHeight >= clip.getY() && r.y <= clip.getBottom())
                if (const auto* t = s.findTrack (r.trackId))
                    paintRow (g, r, *t);

        const auto left = timelineLeft();
        const auto body = juce::Rectangle<int> (left, rulerHeight(), getWidth() - left, getHeight() - rulerHeight());

        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (body);

            // Time selection
            const auto sel = workspace.getTimeSelection();
            if (! sel.isEmpty())
            {
                const auto x0 = frameToX (sel.getStart());
                const auto x1 = frameToX (sel.getEnd());
                g.setColour (palette::selection);
                g.fillRect (juce::Rectangle<float> (x0, (float) body.getY(), x1 - x0, (float) body.getHeight()));
            }

            // Markers
            for (const auto& m : s.markers)
            {
                g.setColour (palette::accent.withAlpha (0.5f));
                g.fillRect (juce::Rectangle<float> (frameToX (m.position), (float) body.getY(), 1.0f, (float) body.getHeight()));
            }

            // Recording: the take grows from where it started.
            if (workspace.isRecording())
            {
                for (const auto& r : rows)
                    if (r.trackId == workspace.getRecordingTrack())
                    {
                        const auto x0 = frameToX (workspace.getRecordingStart());
                        const auto x1 = frameToX (workspace.getPlayheadPosition());
                        g.setColour (palette::record.withAlpha (0.25f));
                        g.fillRect (juce::Rectangle<float> (x0, (float) r.y + 2.0f, juce::jmax (2.0f, x1 - x0), (float) r.clipsHeight - 4.0f));
                        g.setColour (palette::record);
                        g.drawRect (juce::Rectangle<float> (x0, (float) r.y + 2.0f, juce::jmax (2.0f, x1 - x0), (float) r.clipsHeight - 4.0f), 1.0f);
                    }
            }

            // Cursor and playhead
            g.setColour (palette::accent.withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float> (frameToX (workspace.getCursor()), (float) body.getY(), 1.0f, (float) body.getHeight()));

            if (workspace.isPlaying())
            {
                g.setColour (palette::playhead);
                g.fillRect (juce::Rectangle<float> (frameToX (workspace.getPlayheadPosition()) - 0.5f, (float) body.getY(), 2.0f, (float) body.getHeight()));
            }
        }

        paintRuler (g, { left, 0, getWidth() - left, rulerHeight() });

        // Top left corner
        g.setColour (palette::panel);
        g.fillRect (0, 0, left, rulerHeight());
        g.setColour (palette::line);
        g.fillRect (0, rulerHeight() - 1, left, 1);
        g.setColour (palette::textDim);
        g.setFont (fonts::sans (11.5f));
        g.drawText (juce::String (s.getNumAudioTracks()) + " " + TRANS ("tracks") + "  |  "
                        + juce::String (juce::roundToInt (s.sampleRate)) + " Hz",
                    juce::Rectangle<int> (8, 0, left - 16, rulerHeight()), juce::Justification::centredLeft);

        if (dropOver)
        {
            g.setColour (palette::accent.withAlpha (0.12f));
            g.fillRect (body);
            g.setColour (palette::accent);
            g.drawRect (body, 2);
        }
    }

    void TimelineView::paintRuler (juce::Graphics& g, juce::Rectangle<int> area)
    {
        g.setColour (palette::panel);
        g.fillRect (area);
        g.setColour (palette::line);
        g.fillRect (area.withTop (area.getBottom() - 1));

        const auto rate = workspace.getSampleRate();
        const auto pixelsPerSecond = rate / workspace.getView().framesPerPixel;

        double step = rulerSteps[std::size (rulerSteps) - 1];
        for (auto st : rulerSteps)
            if (st * pixelsPerSecond >= minPixelsPerLabel) { step = st; break; }

        const auto first = std::floor ((double) xToFrame (area.getX()) / rate / step) * step;
        const auto last = (double) xToFrame (area.getRight()) / rate;

        g.setFont (fonts::mono (11.0f));
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (area);

        for (auto t = juce::jmax (0.0, first); t <= last; t += step)
        {
            const auto x = frameToX ((juce::int64) std::llround (t * rate));
            g.setColour (palette::textDim);
            g.fillRect (juce::Rectangle<float> (x, (float) area.getBottom() - 8.0f, 1.0f, 8.0f));

            for (int i = 1; i < 5; ++i)
            {
                g.setColour (palette::line);
                g.fillRect (juce::Rectangle<float> (x + (float) (i * step * pixelsPerSecond / 5.0), (float) area.getBottom() - 4.0f, 1.0f, 4.0f));
            }

            g.setColour (palette::textDim);
            g.drawText (formatRulerTime (t, step), juce::Rectangle<float> (x + 4.0f, 0.0f, (float) minPixelsPerLabel, (float) area.getHeight() - 6.0f),
                        juce::Justification::centredLeft, false);
        }

        // Cursor mark
        const auto cx = frameToX (workspace.getCursor());
        juce::Path p;
        p.addTriangle (cx - 5.0f, (float) area.getY(), cx + 5.0f, (float) area.getY(), cx, (float) area.getY() + 7.0f);
        g.setColour (palette::accent);
        g.fillPath (p);
    }

    void TimelineView::paintRow (juce::Graphics& g, const Row& r, const Track& t)
    {
        const auto left = timelineLeft();
        const auto lane = juce::Rectangle<int> (left, r.y, getWidth() - left, r.clipsHeight);
        const bool selectedTrack = t.id == workspace.getSelectedTrack();

        g.setColour (selectedTrack ? palette::panel.brighter (0.03f) : palette::background);
        g.fillRect (lane);
        g.setColour (palette::line);
        g.fillRect (lane.withTop (lane.getBottom() - 1));

        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (juce::Rectangle<int> (left, rulerHeight(), getWidth() - left, getHeight() - rulerHeight()));

        if (t.isBus())
        {
            g.setColour (palette::textDim);
            g.setFont (fonts::sans (12.0f));
            g.drawText (TRANS ("Bus: tracks reach it through their Output or Send (right-click a track header)."),
                        lane.reduced (12, 0), juce::Justification::centredLeft, true);
        }

        const auto colour = trackColour (t);
        for (const auto& c : t.clips)
        {
            const auto b = clipBounds (r, c);
            if (b.getRight() < left || b.getX() > getWidth())
                continue;

            paintClip (g, b, c, colour, workspace.isClipSelected (c.id));
        }

        if (r.automation)
            for (int i = 0; i < 2; ++i)
                paintLane (g, laneBounds (r, i), t, i);
    }

    void TimelineView::paintClip (juce::Graphics& g, juce::Rectangle<int> r, const Clip& c, juce::Colour colour, bool selected)
    {
        const auto alpha = c.muted ? 0.35f : 1.0f;
        const auto bounds = r.toFloat();

        g.setColour (colour.withAlpha (0.16f * alpha).overlaidWith (palette::panel.withAlpha (0.6f)));
        g.fillRoundedRectangle (bounds, 3.0f);

        auto title = r.removeFromTop (fonts::scaled (17));
        g.setColour (colour.withAlpha (0.55f * alpha));
        g.fillRect (title.reduced (1, 1));
        g.setColour (palette::onAccent.withAlpha (alpha));
        g.setFont (fonts::sans (11.5f));
        auto name = c.name;
        if (std::abs (c.gainDb) > 0.05f)
            name << "  (" << (c.gainDb > 0 ? "+" : "") << juce::String (c.gainDb, 1) << " dB)";
        if (c.muted)
            name << "  [" << TRANS ("muted") << "]";
        g.drawText (name, title.reduced (6, 0), juce::Justification::centredLeft, true);

        // Waveform (all channels together), scaled by the clip volume.
        const auto wave = r.reduced (0, 3);
        const auto mid = (float) wave.getCentreY();
        const auto half = (float) wave.getHeight() * 0.5f;
        const auto gain = juce::jmin (4.0f, dsp::dbToGain (c.gainDb));
        const auto clipArea = g.getClipBounds();
        const auto x0 = juce::jmax (r.getX(), clipArea.getX());
        const auto x1 = juce::jmin (r.getRight(), clipArea.getRight());
        const auto channels = c.audio.getNumChannels();

        g.setColour (colour.withAlpha (0.9f * alpha));
        for (int x = x0; x < x1; ++x)
        {
            const auto f0 = juce::jmax (c.position, xToFrame (x));
            const auto f1 = juce::jmin (c.getEnd(), juce::jmax (f0 + 1, xToFrame (x + 1)));
            if (f0 >= c.getEnd())
                break;

            float lo = 0.0f, hi = 0.0f;
            bool any = false;
            for (int ch = 0; ch < channels; ++ch)
            {
                float mn, mx;
                if (c.audio.getMinMax (ch, c.sourceStart + (f0 - c.position), c.sourceStart + (f1 - c.position), mn, mx))
                {
                    lo = any ? juce::jmin (lo, mn) : mn;
                    hi = any ? juce::jmax (hi, mx) : mx;
                    any = true;
                }
            }

            if (! any)
                continue;

            const auto top = mid - juce::jlimit (-1.0f, 1.0f, hi * gain) * half;
            const auto bottom = mid - juce::jlimit (-1.0f, 1.0f, lo * gain) * half;
            g.fillRect (juce::Rectangle<float> ((float) x, top, 1.0f, juce::jmax (1.0f, bottom - top)));
        }

        // Fades: shaded triangles and handles.
        const auto fadeInX = frameToX (c.position + c.fadeIn);
        const auto fadeOutX = frameToX (c.getEnd() - c.fadeOut);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        if (c.fadeIn > 0)
        {
            juce::Path p;
            p.startNewSubPath (bounds.getX(), bounds.getY());
            for (int i = 0; i <= 16; ++i)
            {
                const auto t = (float) i / 16.0f;
                p.lineTo (bounds.getX() + (fadeInX - bounds.getX()) * t, bounds.getBottom() - bounds.getHeight() * std::sin (juce::MathConstants<float>::halfPi * t));
            }
            p.lineTo (bounds.getX(), bounds.getBottom());
            p.closeSubPath();
            g.fillPath (p);
        }
        if (c.fadeOut > 0)
        {
            juce::Path p;
            p.startNewSubPath (bounds.getRight(), bounds.getY());
            for (int i = 0; i <= 16; ++i)
            {
                const auto t = (float) i / 16.0f;
                p.lineTo (bounds.getRight() - (bounds.getRight() - fadeOutX) * t, bounds.getBottom() - bounds.getHeight() * std::sin (juce::MathConstants<float>::halfPi * t));
            }
            p.lineTo (bounds.getRight(), bounds.getBottom());
            p.closeSubPath();
            g.fillPath (p);
        }

        if (selected || bounds.getWidth() > 30.0f)
        {
            g.setColour ((selected ? palette::accent : palette::text).withAlpha (0.85f));
            g.fillRect (juce::Rectangle<float> (fadeInX - handleSize * 0.5f, bounds.getY() + 2.0f, (float) handleSize, (float) handleSize));
            g.fillRect (juce::Rectangle<float> (fadeOutX - handleSize * 0.5f, bounds.getY() + 2.0f, (float) handleSize, (float) handleSize));
        }

        g.setColour (selected ? palette::accent : colour.withAlpha (0.7f * alpha));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, selected ? 2.0f : 1.0f);
    }

    void TimelineView::paintLane (juce::Graphics& g, juce::Rectangle<int> r, const Track& t, int laneIndex)
    {
        g.setColour (palette::panel.darker (0.15f));
        g.fillRect (r);
        g.setColour (palette::line);
        g.fillRect (r.withTop (r.getBottom() - 1));

        const auto& lane = laneIndex == 0 ? t.volumeAutomation : t.panAutomation;
        const auto fallback = laneIndex == 0 ? t.volumeDb : t.pan;
        const auto colour = laneIndex == 0 ? palette::accent : palette::playhead;

        g.setFont (fonts::sans (11.0f));
        g.setColour (palette::textDim);
        const auto label = laneIndex == 0 ? TRANS ("Volume") : TRANS ("Pan");
        g.drawText (label + (lane.isActive() ? juce::String() : "  -  " + TRANS ("click to add points")),
                    r.reduced (8, 2).removeFromTop (14).translated ((int) juce::jmax (0.0f, (float) timelineLeft() - (float) r.getX()), 0),
                    juce::Justification::centredLeft, true);

        if (! lane.isActive())
        {
            g.setColour (colour.withAlpha (0.35f));
            const auto y = laneValueToY (r, laneIndex, fallback);
            g.drawHorizontalLine ((int) y, (float) r.getX(), (float) r.getRight());
            return;
        }

        juce::Path p;
        const auto& points = lane.points;
        p.startNewSubPath ((float) r.getX(), laneValueToY (r, laneIndex, points.front().value));
        for (const auto& pt : points)
            p.lineTo (frameToX (pt.frame), laneValueToY (r, laneIndex, pt.value));
        p.lineTo ((float) r.getRight(), laneValueToY (r, laneIndex, points.back().value));

        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (1.5f));

        for (const auto& pt : points)
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ frameToX (pt.frame), laneValueToY (r, laneIndex, pt.value) }));
    }

    //==============================================================================
    void TimelineView::mouseMove (const juce::MouseEvent& e)
    {
        const auto hit = hitTest (e.getPosition());
        switch (hit.action)
        {
            case Drag::trimStart:
            case Drag::trimEnd:   setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;
            case Drag::fadeIn:
            case Drag::fadeOut:   setMouseCursor (juce::MouseCursor::PointingHandCursor); break;
            case Drag::moveClips: setMouseCursor (juce::MouseCursor::DraggingHandCursor); break;
            case Drag::automation: setMouseCursor (hit.point >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::CrosshairCursor); break;
            case Drag::select:
            case Drag::none:      setMouseCursor (juce::MouseCursor::IBeamCursor); break;
        }
    }

    void TimelineView::mouseDown (const juce::MouseEvent& e)
    {
        if (! workspace.hasSession())
            return;

        drag = Drag::none;
        dragMoved = false;
        const auto frame = xToFrame (e.x);

        // Ruler: place the cursor, drag to select.
        if (e.y < rulerHeight() && e.x >= timelineLeft())
        {
            drag = Drag::select;
            dragStartFrame = workspace.snap (frame);
            workspace.setCursor (dragStartFrame);
            return;
        }

        const auto hit = hitTest (e.getPosition());
        if (hit.trackId != 0)
            workspace.selectTrack (hit.trackId);

        if (! workspace.canEdit())
            return;

        if (hit.action == Drag::automation)
        {
            const auto* track = workspace.getSession().findTrack (hit.trackId);
            if (track == nullptr)
                return;

            if (e.mods.isPopupMenu())
            {
                showLaneMenu (hit);
                return;
            }

            workspace.beginGesture();
            dragBase = workspace.getSession();
            dragHit = hit;

            if (dragHit.point < 0)
            {
                // A new point on the curve where the user clicked.
                const auto* row = rowAt (e.y);
                const auto value = laneYToValue (laneBounds (*row, hit.lane), hit.lane, e.y);
                auto& lane = hit.lane == 0 ? dragBase.findTrack (hit.trackId)->volumeAutomation : dragBase.findTrack (hit.trackId)->panAutomation;

                // The first point starts from the fader's value, so nothing jumps.
                if (! lane.isActive())
                    lane.add ({ 0, hit.lane == 0 ? track->volumeDb : track->pan });

                dragHit.point = lane.add ({ frame, value });
                workspace.updateGesture (dragBase);
            }

            drag = Drag::automation;
            return;
        }

        if (hit.clipId != 0)
        {
            if (e.mods.isPopupMenu())
            {
                if (! workspace.isClipSelected (hit.clipId))
                    workspace.selectClip (hit.clipId, false);

                workspace.setCursor (frame);
                showClipMenu();
                return;
            }

            if (e.mods.isShiftDown() || e.mods.isCommandDown())
                workspace.selectClip (hit.clipId, true);
            else if (! workspace.isClipSelected (hit.clipId))
                workspace.selectClip (hit.clipId, false);

            // The cursor follows the click (the clip selection stays), so Split cuts where the user clicked.
            workspace.setCursor (workspace.snap (frame));

            const auto* clip = workspace.getSession().findClip (hit.clipId);
            dragHit = hit;
            dragStartFrame = frame;
            dragClipStart = clip != nullptr ? clip->position : 0;
            drag = hit.action;
            return;
        }

        if (e.mods.isPopupMenu())
            return;

        drag = Drag::select;
        dragStartFrame = workspace.snap (frame);
        workspace.setCursor (dragStartFrame);
    }

    void TimelineView::mouseDrag (const juce::MouseEvent& e)
    {
        if (drag == Drag::none || ! workspace.hasSession())
            return;

        if (! dragMoved && e.getDistanceFromDragStart() < 3)
            return;

        const auto frame = xToFrame (e.x);

        if (drag == Drag::select)
        {
            const auto end = workspace.snap (frame);
            workspace.setTimeSelection ({ juce::jmin (dragStartFrame, end), juce::jmax (dragStartFrame, end) });
            dragMoved = true;
            return;
        }

        if (! dragMoved)
        {
            workspace.beginGesture();
            if (drag != Drag::automation)
                dragBase = workspace.getSession();
            dragMoved = true;
        }

        auto s = dragBase;
        const auto selected = workspace.getSelectedClips();

        switch (drag)
        {
            case Drag::moveClips:
            {
                auto newStart = juce::jmax ((juce::int64) 0, dragClipStart + (frame - dragStartFrame));
                newStart = workspace.snap (newStart, selected);

                // Snap the end too, if that is closer.
                if (const auto* c = dragBase.findClip (dragHit.clipId))
                {
                    const auto endSnapped = workspace.snap (newStart + c->length, selected);
                    if (endSnapped != newStart + c->length && workspace.snap (newStart, selected) == newStart)
                        newStart = endSnapped - c->length;
                }

                const auto delta = newStart - dragClipStart;

                // A single clip may change track; several keep their own tracks.
                int targetTrack = 0;
                if (selected.size() == 1)
                    if (const auto* row = rowAt (e.y))
                        if (const auto* t = dragBase.findTrack (row->trackId); t != nullptr && t->isAudio())
                            targetTrack = t->id;

                for (auto id : selected)
                {
                    const auto* c = dragBase.findClip (id);
                    const auto* t = dragBase.findTrackOfClip (id);
                    if (c == nullptr || t == nullptr)
                        continue;

                    sessionEdits::moveClip (s, id, targetTrack != 0 ? targetTrack : t->id, juce::jmax ((juce::int64) 0, c->position + delta));
                }
                break;
            }

            case Drag::trimStart:
                sessionEdits::trimClipStart (s, dragHit.clipId, workspace.snap (frame, { dragHit.clipId }));
                break;

            case Drag::trimEnd:
                sessionEdits::trimClipEnd (s, dragHit.clipId, workspace.snap (frame, { dragHit.clipId }));
                break;

            case Drag::fadeIn:
            case Drag::fadeOut:
                if (const auto* c = dragBase.findClip (dragHit.clipId))
                {
                    if (drag == Drag::fadeIn)
                        sessionEdits::setClipFades (s, c->id, juce::jmax ((juce::int64) 0, frame - c->position), c->fadeOut);
                    else
                        sessionEdits::setClipFades (s, c->id, c->fadeIn, juce::jmax ((juce::int64) 0, c->getEnd() - frame));
                }
                break;

            case Drag::automation:
            {
                const auto* row = [this]
                {
                    for (const auto& r : rows)
                        if (r.trackId == dragHit.trackId)
                            return &r;
                    return (const Row*) nullptr;
                }();

                if (row == nullptr)
                    break;

                auto& lane = dragHit.lane == 0 ? s.findTrack (dragHit.trackId)->volumeAutomation : s.findTrack (dragHit.trackId)->panAutomation;
                if (dragHit.point < 0 || dragHit.point >= (int) lane.points.size())
                    break;

                // A point stays between its neighbours.
                const auto i = (size_t) dragHit.point;
                const auto lo = i > 0 ? lane.points[i - 1].frame + 1 : (juce::int64) 0;
                const auto hi = i + 1 < lane.points.size() ? lane.points[i + 1].frame - 1 : std::numeric_limits<juce::int64>::max();
                lane.points[i].frame = juce::jlimit (lo, juce::jmax (lo, hi), frame);
                lane.points[i].value = laneYToValue (laneBounds (*row, dragHit.lane), dragHit.lane, e.y);
                break;
            }

            case Drag::select:
            case Drag::none:
                break;
        }

        workspace.updateGesture (s);
    }

    void TimelineView::mouseUp (const juce::MouseEvent&)
    {
        if (workspace.isInGesture())
        {
            const auto name = drag == Drag::moveClips ? TRANS ("Move Clip")
                            : drag == Drag::trimStart || drag == Drag::trimEnd ? TRANS ("Trim Clip")
                            : drag == Drag::fadeIn || drag == Drag::fadeOut ? TRANS ("Clip Fade")
                                                                             : TRANS ("Edit Automation");
            workspace.endGesture (name);
        }

        drag = Drag::none;
    }

    void TimelineView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        if (! workspace.hasSession())
            return;

        const auto delta = wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX;

        if (e.mods.isCommandDown())
        {
            workspace.zoomBy (delta > 0 ? 1.25 : 0.8, (double) (e.x - timelineLeft()));
            return;
        }

        auto v = workspace.getView();
        if (e.mods.isShiftDown() || ! juce::approximatelyEqual (wheel.deltaX, 0.0f))
        {
            v.startFrame -= (double) delta * 200.0 * v.framesPerPixel;
        }
        else
        {
            const auto maxScroll = juce::jmax (0, getContentHeight() - (getHeight() - rulerHeight()));
            v.scrollY = juce::jlimit (0, maxScroll, v.scrollY - juce::roundToInt (delta * 160.0f));
        }

        workspace.setView (v);
    }

    //==============================================================================
    void TimelineView::showClipMenu()
    {
        juce::PopupMenu menu;
        const auto add = [this, &menu] (const char* id, const juce::String& text)
        {
            menu.addItem (text, commands.isEnabled (id), false, [this, id = juce::String (id)] { run (id); });
        };

        add ("multitrack.split", TRANS ("Split at Cursor"));
        add ("edit.delete", TRANS ("Delete"));
        add ("multitrack.rippleDelete", TRANS ("Ripple Delete"));
        menu.addSeparator();
        add ("edit.copy", TRANS ("Copy"));
        add ("multitrack.clipVolume", TRANS ("Clip Volume..."));
        add ("multitrack.clipMute", TRANS ("Mute Clip"));
        add ("multitrack.renameClip", TRANS ("Rename Clip..."));
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
    }

    void TimelineView::showLaneMenu (const Hit& hit)
    {
        juce::PopupMenu menu;
        const auto trackId = hit.trackId;
        const auto laneIndex = hit.lane;
        const auto point = hit.point;

        menu.addItem (TRANS ("Delete Point"), point >= 0, false, [this, trackId, laneIndex, point]
        {
            auto s = workspace.getSession();
            if (auto* t = s.findTrack (trackId))
            {
                auto& lane = laneIndex == 0 ? t->volumeAutomation : t->panAutomation;
                if (point >= 0 && point < (int) lane.points.size())
                {
                    lane.points.erase (lane.points.begin() + point);
                    workspace.applyEdit (TRANS ("Edit Automation"), s);
                }
            }
        });

        menu.addItem (TRANS ("Clear This Lane"), true, false, [this, trackId, laneIndex]
        {
            auto s = workspace.getSession();
            if (auto* t = s.findTrack (trackId))
            {
                (laneIndex == 0 ? t->volumeAutomation : t->panAutomation) = {};
                workspace.applyEdit (TRANS ("Clear Automation"), s);
            }
        });

        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
    }

    //==============================================================================
    bool TimelineView::isInterestedInFileDrag (const juce::StringArray& files)
    {
        if (! workspace.canEdit())
            return false;

        for (const auto& f : files)
            if (! audioFormats::canOpen (juce::File (f)))
                return false;

        dropOver = true;
        repaint();
        return true;
    }

    void TimelineView::fileDragExit (const juce::StringArray&)
    {
        dropOver = false;
        repaint();
    }

    void TimelineView::filesDropped (const juce::StringArray& files, int x, int y)
    {
        dropOver = false;
        repaint();

        juce::Array<juce::File> list;
        for (const auto& f : files)
            list.add (juce::File (f));

        const auto* row = rowAt (y);
        workspace.importFiles (list, row != nullptr ? row->trackId : 0, workspace.snap (x >= timelineLeft() ? xToFrame (x) : workspace.getCursor()));
    }
}
