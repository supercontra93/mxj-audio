// SPDX-License-Identifier: AGPL-3.0-only
#include "app/session/MixerPanel.h"
#include "app/theme/Fonts.h"
#include "app/theme/Palette.h"
#include "dsp/effects/EffectCatalogue.h"

namespace mxj
{
    namespace
    {
        int stripWidth() { return fonts::scaled (104); }

        float sliderToDb (double v) { return v <= -60.0 ? -96.0f : (float) v; }

        juce::String dbText (double v)
        {
            return v <= -60.0 ? juce::String ("-inf") : (v > 0 ? "+" : "") + juce::String (v, 1);
        }

        void styleKnob (juce::Slider& s)
        {
            s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            s.setColour (juce::Slider::rotarySliderFillColourId, palette::accent);
            s.setColour (juce::Slider::rotarySliderOutlineColourId, palette::line);
            s.setPopupDisplayEnabled (true, true, nullptr);
        }
    }

    //==============================================================================
    class MixerPanel::Strip : public juce::Component
    {
    public:
        Strip (SessionWorkspace& w, int id, std::function<void (int)> effects)
            : trackId (id), workspace (w), showEffects (std::move (effects))
        {
            for (auto* b : { &muteButton, &soloButton, &fxButton })
            {
                b->setWantsKeyboardFocus (false);
                addAndMakeVisible (b);
            }

            muteButton.setButtonText ("M");
            soloButton.setButtonText ("S");
            fxButton.setButtonText ("FX");
            muteButton.setTooltip (TRANS ("Mute"));
            soloButton.setTooltip (TRANS ("Solo: hear only this track"));
            fxButton.setTooltip (TRANS ("Effects of this track"));
            muteButton.setColour (juce::TextButton::buttonOnColourId, palette::accent);
            soloButton.setColour (juce::TextButton::buttonOnColourId, palette::meterGreen);
            fxButton.setColour (juce::TextButton::buttonOnColourId, palette::playhead);
            muteButton.onClick = [this] { workspace.toggleMute (trackId); };
            soloButton.onClick = [this] { workspace.toggleSolo (trackId); };
            fxButton.onClick = [this] { showEffects (trackId); };
            soloButton.setVisible (trackId != 0);

            fader.setSliderStyle (juce::Slider::LinearVertical);
            fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            fader.setRange (-60.0, 12.0, 0.1);
            fader.setSkewFactorFromMidPoint (-12.0);
            fader.setDoubleClickReturnValue (true, 0.0);
            fader.setColour (juce::Slider::thumbColourId, palette::accent);
            fader.setColour (juce::Slider::trackColourId, palette::accentDark);
            fader.setColour (juce::Slider::backgroundColourId, palette::line);
            fader.onValueChange = [this]
            {
                workspace.setTrackVolume (trackId, sliderToDb (fader.getValue()), ! fader.isMouseButtonDown());
                repaint();
            };
            fader.onDragEnd = [this] { workspace.setTrackVolume (trackId, sliderToDb (fader.getValue()), true); };
            fader.textFromValueFunction = [] (double v) { return dbText (v) + " dB"; };
            fader.setPopupDisplayEnabled (true, true, nullptr);
            addAndMakeVisible (fader);

            styleKnob (pan);
            pan.setRange (-1.0, 1.0, 0.01);
            pan.setDoubleClickReturnValue (true, 0.0);
            pan.setTooltip (TRANS ("Pan"));
            pan.onValueChange = [this] { workspace.setTrackPan (trackId, (float) pan.getValue(), ! pan.isMouseButtonDown()); };
            pan.onDragEnd = [this] { workspace.setTrackPan (trackId, (float) pan.getValue(), true); };
            pan.textFromValueFunction = [] (double v)
            {
                if (std::abs (v) < 0.005) return juce::String ("C");
                return (v < 0 ? "L" : "R") + juce::String (juce::roundToInt (std::abs (v) * 100.0));
            };
            addAndMakeVisible (pan);

            styleKnob (sendLevel);
            sendLevel.setRange (-60.0, 12.0, 0.1);
            sendLevel.setSkewFactorFromMidPoint (-12.0);
            sendLevel.setDoubleClickReturnValue (true, -6.0);
            sendLevel.setTooltip (TRANS ("Send level"));
            sendLevel.textFromValueFunction = [] (double v) { return dbText (v) + " dB"; };
            sendLevel.onValueChange = [this] { workspace.setSendLevel (trackId, sliderToDb (sendLevel.getValue()), ! sendLevel.isMouseButtonDown()); };
            sendLevel.onDragEnd = [this] { workspace.setSendLevel (trackId, sliderToDb (sendLevel.getValue()), true); };
            addChildComponent (sendLevel);

            output.setTooltip (TRANS ("Output: where this track plays"));
            output.onChange = [this]
            {
                if (! updating)
                    workspace.setTrackOutput (trackId, busIds[output.getSelectedItemIndex()]);
            };
            addChildComponent (output);

            send.setTooltip (TRANS ("Send: a copy of this track to a bus (for example a shared reverb)"));
            send.onChange = [this]
            {
                if (! updating)
                    workspace.setSendBus (trackId, busIds[send.getSelectedItemIndex()]);
            };
            addChildComponent (send);

            setSize (stripWidth(), 300);
        }

        void update (const Session& s, const Track& t, bool selected)
        {
            track = t;
            isSelected = selected;
            updating = true;

            muteButton.setToggleState (t.mute, juce::dontSendNotification);
            soloButton.setToggleState (t.solo, juce::dontSendNotification);
            fxButton.setToggleState (dsp::chainFromVar (t.effects).size() > 0, juce::dontSendNotification);

            if (! fader.isMouseButtonDown())
                fader.setValue (juce::jmax (-60.0f, t.volumeDb), juce::dontSendNotification);
            if (! pan.isMouseButtonDown())
                pan.setValue (t.pan, juce::dontSendNotification);
            fader.setEnabled (! t.volumeAutomation.isActive());
            pan.setEnabled (! t.panAutomation.isActive());

            const bool audio = t.isAudio();
            output.setVisible (audio);
            send.setVisible (audio);

            if (audio)
            {
                busIds = { 0 };
                output.clear (juce::dontSendNotification);
                send.clear (juce::dontSendNotification);
                output.addItem (TRANS ("Master"), 1);
                send.addItem (TRANS ("No Send"), 1);

                for (const auto& b : s.tracks)
                {
                    if (! b.isBus())
                        continue;

                    busIds.push_back (b.id);
                    output.addItem (b.name, (int) busIds.size());
                    send.addItem (b.name, (int) busIds.size());
                }

                const auto indexOf = [this] (int id)
                {
                    const auto it = std::find (busIds.begin(), busIds.end(), id);
                    return it == busIds.end() ? 0 : (int) (it - busIds.begin());
                };

                output.setSelectedItemIndex (indexOf (t.outputId), juce::dontSendNotification);
                send.setSelectedItemIndex (t.sends.empty() ? 0 : indexOf (t.sends.front().busId), juce::dontSendNotification);
                sendLevel.setVisible (! t.sends.empty());
                if (! t.sends.empty() && ! sendLevel.isMouseButtonDown())
                    sendLevel.setValue (juce::jmax (-60.0f, t.sends.front().levelDb), juce::dontSendNotification);
            }
            else
            {
                sendLevel.setVisible (false);
            }

            updating = false;
            repaint();
        }

        void updateMeter()
        {
            const auto [l, r] = workspace.getLevel (trackId);
            const auto fall = [] (float& shown, float level)
            {
                const auto db = dsp::gainToDb (level);
                shown = db > shown ? db : juce::jmax (-70.0f, shown - 1.5f);
            };
            fall (meterL, l);
            fall (meterR, r);
            repaint (meterArea);
        }

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds();
            g.setColour (isSelected ? palette::panelRaised : palette::panel);
            g.fillRect (b);
            g.setColour (palette::line);
            g.fillRect (b.removeFromRight (1));

            const auto count = (int) std::size (palette::tracks);
            g.setColour (trackId == 0 ? palette::accent : track.isBus() ? palette::textDim
                                                                        : palette::tracks[((track.colour % count) + count) % count]);
            g.fillRect (b.removeFromTop (3));

            g.setColour (palette::text);
            g.setFont (fonts::sansBold (12.0f));
            g.drawText (track.name, nameArea, juce::Justification::centred, true);

            if (track.isAudio())
            {
                g.setColour (palette::textDim);
                g.setFont (fonts::sans (10.5f));
                g.drawText (TRANS ("Out"), outLabel, juce::Justification::centredLeft);
                g.drawText (TRANS ("Send"), sendLabel, juce::Justification::centredLeft);
            }

            // Meter
            g.setColour (palette::background);
            g.fillRect (meterArea);
            const auto bar = [&] (juce::Rectangle<int> area, float db)
            {
                const auto h = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f) * (float) area.getHeight();
                auto filled = area.removeFromBottom (juce::roundToInt (h));
                g.setColour (db > -0.5f ? palette::record : db > -9.0f ? palette::accent : palette::meterGreen);
                g.fillRect (filled);
            };
            auto m = meterArea.reduced (1);
            bar (m.removeFromLeft (m.getWidth() / 2 - 1), meterL);
            m.removeFromLeft (2);
            bar (m, meterR);

            g.setColour (palette::accent);
            g.setFont (fonts::mono (11.0f));
            g.drawText (dbText (fader.getValue()), valueArea, juce::Justification::centred);
        }

        void resized() override
        {
            auto b = getLocalBounds().reduced (6, 4);
            b.removeFromTop (2);
            nameArea = b.removeFromTop (fonts::scaled (18));
            const auto row = fonts::scaled (20);

            if (track.isAudio() || output.isVisible())
            {
                auto r1 = b.removeFromTop (row);
                outLabel = r1.removeFromLeft (fonts::scaled (30));
                output.setBounds (r1);
                b.removeFromTop (3);
                auto r2 = b.removeFromTop (row);
                sendLabel = r2.removeFromLeft (fonts::scaled (30));
                sendLevel.setBounds (r2.removeFromRight (row + 2));
                send.setBounds (r2);
                b.removeFromTop (4);
            }

            auto knobs = b.removeFromTop (fonts::scaled (30));
            fxButton.setBounds (knobs.removeFromLeft (fonts::scaled (34)).withSizeKeepingCentre (fonts::scaled (34), row));
            pan.setBounds (knobs.removeFromRight (fonts::scaled (30)));
            b.removeFromTop (4);

            auto buttons = b.removeFromTop (row);
            muteButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 2));
            buttons.removeFromLeft (4);
            soloButton.setBounds (buttons);
            b.removeFromTop (6);

            valueArea = b.removeFromBottom (fonts::scaled (16));
            meterArea = b.removeFromRight (fonts::scaled (14));
            b.removeFromRight (4);
            fader.setBounds (b);
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            if (trackId != 0)
                workspace.selectTrack (trackId);
        }

        const int trackId;

    private:
        SessionWorkspace& workspace;
        std::function<void (int)> showEffects;
        Track track;
        bool isSelected = false, updating = false;
        std::vector<int> busIds { 0 };
        juce::TextButton muteButton, soloButton, fxButton;
        juce::Slider fader, pan, sendLevel;
        juce::ComboBox output, send;
        juce::Rectangle<int> nameArea, outLabel, sendLabel, meterArea, valueArea;
        float meterL = -70.0f, meterR = -70.0f;
    };

    //==============================================================================
    MixerPanel::MixerPanel (SessionWorkspace& w, std::function<void (int)> effects)
        : workspace (w), showEffects (std::move (effects))
    {
        viewport.setViewedComponent (&strips, false);
        viewport.setScrollBarsShown (false, true);
        addAndMakeVisible (viewport);

        masterStrip = std::make_unique<Strip> (workspace, 0, showEffects);
        addAndMakeVisible (*masterStrip);

        workspace.addChangeListener (this);
        update();
        startTimerHz (30);
    }

    MixerPanel::~MixerPanel()
    {
        workspace.removeChangeListener (this);
    }

    int MixerPanel::preferredHeight()
    {
        return fonts::scaled (330);
    }

    void MixerPanel::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        update();
    }

    void MixerPanel::timerCallback()
    {
        for (auto* s : trackStrips)
            s->updateMeter();
        masterStrip->updateMeter();
    }

    void MixerPanel::update()
    {
        if (! workspace.hasSession())
            return;

        const auto& s = workspace.getSession();
        bool same = trackStrips.size() == (int) s.tracks.size();
        for (int i = 0; same && i < trackStrips.size(); ++i)
            same = trackStrips[i]->trackId == s.tracks[(size_t) i].id;

        if (! same)
        {
            trackStrips.clear();
            for (const auto& t : s.tracks)
                strips.addAndMakeVisible (trackStrips.add (new Strip (workspace, t.id, showEffects)));

            resized();
        }

        for (size_t i = 0; i < s.tracks.size(); ++i)
            trackStrips[(int) i]->update (s, s.tracks[i], s.tracks[i].id == workspace.getSelectedTrack());

        auto master = s.master;
        master.name = TRANS ("Master");
        masterStrip->update (s, master, false);

        for (auto* strip : trackStrips)
            strip->resized();
    }

    void MixerPanel::paint (juce::Graphics& g)
    {
        g.fillAll (palette::background);
        g.setColour (palette::line);
        g.fillRect (getLocalBounds().removeFromTop (1));
    }

    void MixerPanel::resized()
    {
        auto b = getLocalBounds().withTrimmedTop (1);
        masterStrip->setBounds (b.removeFromRight (stripWidth() + 8));
        b.removeFromRight (2);
        viewport.setBounds (b);

        const auto h = b.getHeight() - viewport.getScrollBarThickness();
        strips.setSize (juce::jmax (1, trackStrips.size() * stripWidth()), juce::jmax (1, h));
        for (int i = 0; i < trackStrips.size(); ++i)
            trackStrips[i]->setBounds (i * stripWidth(), 0, stripWidth(), h);
    }
}
