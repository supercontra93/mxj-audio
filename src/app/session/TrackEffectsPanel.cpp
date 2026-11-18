// SPDX-License-Identifier: AGPL-3.0-only
#include "app/session/TrackEffectsPanel.h"
#include "app/theme/Fonts.h"
#include "app/theme/Palette.h"

namespace mxj
{
    namespace
    {
        constexpr int padding = 10;
        int rowHeight() { return fonts::scaled (28); }

        /** Parameter changes become an undo step after the controls rest this long. */
        constexpr int commitDelayMs = 700;
    }

    int TrackEffectsPanel::preferredWidth()
    {
        return fonts::scaled (320);
    }

    //==============================================================================
    class TrackEffectsPanel::SlotRow : public juce::Component
    {
    public:
        SlotRow (EffectRack& r, int i) : rack (r), index (i)
        {
            enabled.setToggleState (rack[index].enabled->load(), juce::dontSendNotification);
            enabled.setTooltip (TRANS ("Switch this effect on or off"));
            enabled.onClick = [this] { rack.setEnabled (index, enabled.getToggleState()); };
            addAndMakeVisible (enabled);

            for (auto* b : { &up, &down, &remove })
            {
                b->setWantsKeyboardFocus (false);
                addAndMakeVisible (b);
            }

            up.setButtonText (juce::String::charToString (0x25b2));
            up.setTooltip (TRANS ("Move up"));
            up.onClick = [this] { rack.move (index, index - 1); };
            up.setEnabled (index > 0);

            down.setButtonText (juce::String::charToString (0x25bc));
            down.setTooltip (TRANS ("Move down"));
            down.onClick = [this] { rack.move (index, index + 1); };
            down.setEnabled (index < rack.size() - 1);

            remove.setButtonText (juce::String::charToString (0x2715));
            remove.setTooltip (TRANS ("Remove"));
            remove.onClick = [this] { rack.remove (index); };
        }

        void paint (juce::Graphics& g) override
        {
            const bool isSelected = rack.getSelectedSlot() == index;
            auto area = getLocalBounds();
            g.setColour (isSelected ? palette::panelRaised : palette::panel);
            g.fillRoundedRectangle (area.toFloat(), palette::cornerRadius);

            if (isSelected)
            {
                g.setColour (palette::accent);
                g.fillRect (area.removeFromLeft (3));
            }

            g.setColour (rack[index].enabled->load() ? palette::text : palette::textDim);
            g.setFont (fonts::sans (13.0f));
            g.drawFittedText (juce::String (index + 1) + ".  " + TRANS (rack[index].type->name), nameArea, juce::Justification::centredLeft, 1);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (4, 2);
            enabled.setBounds (area.removeFromLeft (area.getHeight() + 4));
            const auto b = area.getHeight();
            remove.setBounds (area.removeFromRight (b));
            area.removeFromRight (2);
            down.setBounds (area.removeFromRight (b));
            area.removeFromRight (2);
            up.setBounds (area.removeFromRight (b));
            nameArea = area.withTrimmedLeft (2);
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            rack.setSelectedSlot (index);
        }

    private:
        EffectRack& rack;
        const int index;
        juce::ToggleButton enabled;
        juce::TextButton up, down, remove;
        juce::Rectangle<int> nameArea;
    };

    //==============================================================================
    TrackEffectsPanel::TrackEffectsPanel (SessionWorkspace& w) : workspace (w)
    {
        addButton.setButtonText ("+ " + TRANS ("Add Effect"));
        addButton.onClick = [this] { showAddMenu(); };
        addAndMakeVisible (addButton);

        parameterView.setScrollBarsShown (true, false);
        addAndMakeVisible (parameterView);

        // Adding, removing, moving: one undo step at once.
        rack.onChainChanged = [this]
        {
            if (! loading)
                commit (true);
        };

        rack.addChangeListener (this);
        workspace.addChangeListener (this);
        showTrack (w.getSelectedTrack());
    }

    TrackEffectsPanel::~TrackEffectsPanel()
    {
        rack.onChainChanged = nullptr;
        rack.removeChangeListener (this);
        workspace.removeChangeListener (this);
    }

    void TrackEffectsPanel::showTrack (int trackId)
    {
        if (! workspace.hasSession())
            return;

        if (isTimerRunning())
        {
            stopTimer();
            commit (true);
        }

        shownTrack = workspace.getSession().findTrack (trackId) != nullptr ? trackId : 0;
        shownJson = {};
        loadFromSession();
    }

    void TrackEffectsPanel::loadFromSession()
    {
        const auto* t = workspace.getSession().findTrack (shownTrack);
        if (t == nullptr)
        {
            shownTrack = 0;
            t = &workspace.getSession().master;
        }

        const auto json = juce::JSON::toString (t->effects, true);
        if (json == shownJson)
            return;

        loading = true;
        const auto selected = rack.getSelectedSlot();
        rack.setSettings (dsp::chainFromVar (t->effects));
        rack.setSelectedSlot (juce::jmin (selected < 0 ? 0 : selected, rack.size() - 1));
        loading = false;
        shownJson = json;
        rebuildRows();
    }

    void TrackEffectsPanel::commit (bool asUndoStep)
    {
        if (shownTrack < 0)
            return;

        const auto effects = dsp::chainToVar (rack.getSettings());
        const auto json = juce::JSON::toString (effects, true);

        // The rack also reports changes that are not edits (a slot selected, or
        // its settings loaded from the session, reported later): nothing to record.
        const auto* t = workspace.getSession().findTrack (shownTrack);
        if (t != nullptr && asUndoStep
            && json == juce::JSON::toString (t->effects, true) && json == shownJson)
            return;

        shownJson = json;
        workspace.setTrackEffects (shownTrack, effects, asUndoStep);
    }

    void TrackEffectsPanel::timerCallback()
    {
        stopTimer();
        commit (true);
    }

    void TrackEffectsPanel::changeListenerCallback (juce::ChangeBroadcaster* source)
    {
        if (source == &rack)
        {
            // Switched on or off, or another slot selected.
            if (! loading)
                commit (true);

            rebuildRows();
            return;
        }

        if (! workspace.hasSession() || isTimerRunning())
            return;

        if (workspace.getSession().findTrack (shownTrack) == nullptr)
            shownTrack = 0;

        loadFromSession(); // after undo, or a change made elsewhere
        repaint();
    }

    void TrackEffectsPanel::rebuildRows()
    {
        rows.clear();
        for (int i = 0; i < rack.size(); ++i)
            addAndMakeVisible (rows.add (new SlotRow (rack, i)));

        const auto selected = rack.getSelectedSlot();
        if (selected != shownSlot || parameters == nullptr || selected >= rack.size())
        {
            parameterView.setViewedComponent (nullptr, false);
            parameters = nullptr;
            shownSlot = selected;

            if (selected >= 0 && selected < rack.size())
            {
                const auto& slot = rack[selected];
                parameters = std::make_unique<ParameterPanel> (*slot.type, slot.values, workspace.getSampleRate());
                parameters->onChange = [this]
                {
                    commit (false);          // heard now
                    startTimer (commitDelayMs); // one undo step when the controls rest
                };
                parameterView.setViewedComponent (parameters.get(), false);
            }
        }
        else if (parameters != nullptr)
        {
            parameters->refreshFromValues();
        }

        resized();
        repaint();
    }

    void TrackEffectsPanel::showAddMenu()
    {
        juce::PopupMenu menu;
        const auto all = dsp::catalogue::all();

        for (const auto& category : dsp::catalogue::categories())
        {
            juce::PopupMenu sub;
            for (size_t i = 0; i < all.size(); ++i)
                if (all[i]->category == category && all[i]->isRealtime())
                    sub.addItem ((int) i + 1, TRANS (all[i]->name));

            if (sub.getNumItems() > 0)
                menu.addSubMenu (TRANS (category), sub);
        }

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addButton),
                            [safe = juce::Component::SafePointer<TrackEffectsPanel> (this), all] (int result)
        {
            if (safe != nullptr && result > 0 && result <= (int) all.size())
            {
                const auto index = safe->rack.add (all[(size_t) result - 1]->id);
                safe->rack.setSelectedSlot (index);
            }
        });
    }

    void TrackEffectsPanel::paint (juce::Graphics& g)
    {
        g.fillAll (palette::panel);
        g.setColour (palette::line);
        g.fillRect (getLocalBounds().removeFromLeft (1));

        juce::String name = TRANS ("Master");
        if (workspace.hasSession())
            if (const auto* t = workspace.getSession().findTrack (shownTrack); t != nullptr && shownTrack != 0)
                name = t->name;

        g.setColour (palette::text);
        g.setFont (fonts::sansBold (14.0f));
        g.drawText (TRANS ("Effects") + ": " + name, titleArea, juce::Justification::centredLeft, true);

        if (rack.size() == 0)
        {
            g.setColour (palette::textDim);
            g.setFont (fonts::sans (12.5f));
            g.drawFittedText (TRANS ("No effects yet. Add an EQ, a compressor or a reverb; the clips stay unchanged."),
                              getLocalBounds().reduced (padding).withTrimmedTop (titleArea.getBottom() + rowHeight() + 10),
                              juce::Justification::centredTop, 4);
        }
    }

    void TrackEffectsPanel::resized()
    {
        auto area = getLocalBounds().reduced (padding);
        titleArea = area.removeFromTop (fonts::scaled (26));
        addButton.setBounds (area.removeFromTop (rowHeight()));
        area.removeFromTop (8);

        for (auto* r : rows)
        {
            r->setBounds (area.removeFromTop (rowHeight()));
            area.removeFromTop (3);
        }

        area.removeFromTop (8);
        parameterView.setBounds (area);

        if (parameters != nullptr)
            parameters->setSize (juce::jmax (1, area.getWidth() - parameterView.getScrollBarThickness()), parameters->getIdealHeight());
    }
}
