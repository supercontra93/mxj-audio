// SPDX-License-Identifier: AGPL-3.0-only
#include "app/editor/EditorPage.h"
#include "app/theme/Fonts.h"
#include "app/theme/Icons.h"
#include "app/theme/Palette.h"

namespace mxj
{
    namespace
    {
        constexpr int scrollBarHeight = 14;

        /** "ctrl + O" -> "Ctrl+O", for tooltips. */
        juce::String shortcutText (const CommandInfo& info)
        {
            if (info.defaultShortcut.isEmpty())
                return {};

            return " (" + juce::KeyPress::createFromDescription (info.defaultShortcut).getTextDescriptionWithIcons() + ")";
        }
    }

    //==============================================================================
    /** A square transport button with a Phosphor icon. */
    class EditorPage::IconButton : public juce::Button
    {
    public:
        IconButton (const CommandInfo& info, const juce::String& iconName, RunCommand run)
            : Button (TRANS (info.name)), commandId (info.id)
        {
            setIcon (iconName);
            setTooltip (TRANS (info.name) + shortcutText (info));
            setWantsKeyboardFocus (false); // Space must reach the play command, not "click" the button
            onClick = [id = info.id, run] { run (id); };
        }

        void setIcon (const juce::String& iconName)
        {
            if (iconName == currentIcon)
                return;

            currentIcon = iconName;
            icon = icons::create (iconName, palette::text);
            iconActive = icons::create (iconName, palette::onAccent);
            repaint();
        }

        void setHighlighted (bool shouldHighlight)
        {
            if (highlighted != shouldHighlight)
            {
                highlighted = shouldHighlight;
                repaint();
            }
        }

        void paintButton (juce::Graphics& g, bool isOver, bool isDown) override
        {
            auto b = getLocalBounds().toFloat().reduced (0.5f);
            const float alpha = isEnabled() ? 1.0f : 0.4f;

            const auto fill = highlighted ? palette::accent
                            : isDown      ? palette::line
                            : isOver      ? palette::panelRaised.brighter (0.06f)
                                          : palette::panelRaised;
            g.setColour (fill.withMultipliedAlpha (alpha));
            g.fillRoundedRectangle (b, palette::cornerRadius);

            if (! highlighted)
            {
                g.setColour ((isOver && isEnabled() ? palette::accent : palette::line).withMultipliedAlpha (alpha));
                g.drawRoundedRectangle (b, palette::cornerRadius, 1.0f);
            }

            auto& d = highlighted ? iconActive : icon;
            if (d != nullptr)
                d->drawWithin (g, getLocalBounds().toFloat().reduced (8.0f), juce::RectanglePlacement::centred, alpha);
        }

        const CommandId commandId;

    private:
        juce::String currentIcon;
        std::unique_ptr<juce::Drawable> icon, iconActive;
        bool highlighted = false;
    };

    //==============================================================================
    EditorPage::EditorPage (Workspace& w, const CommandRegistry& c, RunCommand runCommand)
        : workspace (w), commands (c), waveform (w)
    {
        struct Spec { const char* command; const char* icon; };
        const Spec specs[] = {
            { "transport.toStart",  "skip-back" },
            { "transport.playStop", "play" },
            { "transport.toEnd",    "skip-forward" },
            { "view.zoomIn",        "magnifying-glass-plus" },
            { "view.zoomOut",       "magnifying-glass-minus" },
            { "view.zoomFull",      "arrows-out-line-horizontal" },
        };

        for (const auto& s : specs)
        {
            if (const auto* info = commands.find (s.command))
            {
                auto* b = buttons.add (new IconButton (*info, s.icon, runCommand));
                addAndMakeVisible (b);

                if (info->id == "transport.playStop")
                    playButton = b;
            }
        }

        addAndMakeVisible (waveform);

        scrollBar.setAutoHide (false);
        scrollBar.setColour (juce::ScrollBar::thumbColourId, palette::line.brighter (0.15f));
        scrollBar.setColour (juce::ScrollBar::backgroundColourId, palette::panel);
        scrollBar.addListener (this);
        addAndMakeVisible (scrollBar);

        workspace.addChangeListener (this);
        updateFromWorkspace();
        startTimerHz (30);
    }

    EditorPage::~EditorPage()
    {
        workspace.removeChangeListener (this);
    }

    void EditorPage::resized()
    {
        auto area = getLocalBounds();
        auto bar = area.removeFromTop (transportHeight).reduced (12, 8);

        for (int i = 0; i < buttons.size(); ++i)
        {
            buttons[i]->setBounds (bar.removeFromLeft (bar.getHeight()));
            bar.removeFromLeft (i == 2 ? 20 : 4); // gap between transport and zoom groups
        }

        bar.removeFromLeft (16);
        timeArea = bar.removeFromLeft (150);
        bar.removeFromLeft (16);
        infoArea = bar;

        scrollBar.setBounds (area.removeFromBottom (scrollBarHeight));
        area.removeFromTop (1);
        waveform.setBounds (area);
    }

    void EditorPage::paint (juce::Graphics& g)
    {
        g.fillAll (palette::background);

        auto bar = getLocalBounds().removeFromTop (transportHeight);
        g.setColour (palette::panel);
        g.fillRect (bar);
        g.setColour (palette::line);
        g.fillRect (bar.removeFromBottom (1));

        g.setColour (palette::accent);
        g.setFont (fonts::mono (20.0f));
        g.drawText (timeText, timeArea, juce::Justification::centredLeft);

        g.setColour (palette::textDim);
        g.setFont (fonts::sans (12.5f));
        g.drawText (describeFile(), infoArea, juce::Justification::centredRight);
    }

    juce::String EditorPage::describeFile() const
    {
        const auto* doc = workspace.getDocument();
        if (doc == nullptr)
            return {};

        const auto& info = doc->getInfo();
        const auto channels = info.numChannels == 1 ? TRANS ("Mono")
                            : info.numChannels == 2 ? TRANS ("Stereo")
                                                    : juce::String (info.numChannels) + " " + TRANS ("channels");

        juce::StringArray parts { info.file.getFileName(),
                                  info.formatName.upToFirstOccurrenceOf (" ", false, false),
                                  juce::String (juce::roundToInt (info.sampleRate)) + " Hz" };
        if (info.bitsPerSample > 0 && info.isUncompressed)
            parts.add (juce::String (info.bitsPerSample) + " bit");
        parts.add (channels);
        parts.add (Workspace::formatTime (info.numFrames, info.sampleRate));

        return parts.joinIntoString ("   |   ");
    }

    void EditorPage::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        updateFromWorkspace();
    }

    void EditorPage::timerCallback()
    {
        const auto* doc = workspace.getDocument();
        const auto text = doc != nullptr ? Workspace::formatTime (workspace.getPlayheadPosition(), doc->getInfo().sampleRate)
                                         : juce::String();
        if (text != timeText)
        {
            timeText = text;
            repaint (timeArea);
        }
    }

    void EditorPage::updateFromWorkspace()
    {
        for (auto* b : buttons)
            b->setEnabled (commands.isEnabled (b->commandId));

        if (playButton != nullptr)
        {
            const bool playing = workspace.isPlaying();
            playButton->setIcon (playing ? "stop" : "play");
            playButton->setHighlighted (playing);
        }

        if (const auto* doc = workspace.getDocument())
        {
            const auto& v = workspace.getView();
            scrollBar.setRangeLimits (0.0, (double) juce::jmax ((juce::int64) 1, doc->getInfo().numFrames), juce::dontSendNotification);
            scrollBar.setCurrentRange (v.startFrame, v.framesPerPixel * workspace.getViewWidth(), juce::dontSendNotification);
        }

        timerCallback();
        repaint (infoArea);
    }

    void EditorPage::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
    {
        auto v = workspace.getView();
        v.startFrame = newRangeStart;
        workspace.setView (v);
    }
}
