// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "app/session/SessionWorkspace.h"
#include "core/commands/CommandRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** The tracks of the session: a header per track on the left (name, mute,
        solo, record, automation, volume, pan) and the time line on the right
        with the ruler, the clips and the automation lanes.

        Mouse: click to place the cursor, drag to select time; drag a clip to
        move it (also to another track), its edges to trim it and the small
        squares at its top corners to set its fades; right-click a clip for
        more. In an automation lane click to add a point, drag points, and
        right-click a point to remove it. Ctrl + wheel zooms, Shift + wheel
        scrolls sideways. Audio files dropped here are imported where they land.
    */
    class TimelineView : public juce::Component,
                         public juce::FileDragAndDropTarget,
                         private juce::ChangeListener,
                         private juce::Timer
    {
    public:
        using RunCommand = std::function<void (const CommandId&)>;

        TimelineView (SessionWorkspace& workspace, const CommandRegistry& commands, RunCommand runCommand);
        ~TimelineView() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        bool isInterestedInFileDrag (const juce::StringArray& files) override;
        void filesDropped (const juce::StringArray& files, int x, int y) override;
        void fileDragExit (const juce::StringArray& files) override;

        /** Height of everything (for the vertical scroll bar). */
        int getContentHeight() const;

        static int headerWidth();
        static int rulerHeight();
        static int trackHeight();
        static int laneHeight();

    private:
        class Header;

        struct Row
        {
            int trackId = 0;
            int y = 0;        ///< top, in component coordinates (after scrolling)
            int clipsHeight = 0;
            int totalHeight = 0;
            bool automation = false;
        };

        enum class Drag { none, select, moveClips, trimStart, trimEnd, fadeIn, fadeOut, automation };

        struct Hit
        {
            int trackId = 0;
            int clipId = 0;
            Drag action = Drag::none;
            int lane = -1;      ///< automation lane: 0 volume, 1 pan
            int point = -1;     ///< automation point index
        };

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;
        void updateRows();
        void updateHeaders();

        const Row* rowAt (int y) const;
        Hit hitTest (juce::Point<int> p) const;
        juce::Rectangle<int> clipBounds (const Row&, const Clip&) const;
        juce::Rectangle<int> laneBounds (const Row&, int lane) const;
        int timelineLeft() const noexcept { return headerWidth(); }
        float frameToX (juce::int64 frame) const;
        juce::int64 xToFrame (int x) const;

        float laneValueToY (juce::Rectangle<int> lane, int laneIndex, float value) const;
        float laneYToValue (juce::Rectangle<int> lane, int laneIndex, int y) const;

        void paintRuler (juce::Graphics&, juce::Rectangle<int> area);
        void paintRow (juce::Graphics&, const Row&, const Track&);
        void paintClip (juce::Graphics&, juce::Rectangle<int> r, const Clip&, juce::Colour colour, bool selected);
        void paintLane (juce::Graphics&, juce::Rectangle<int> r, const Track&, int laneIndex);
        void showClipMenu();
        void showLaneMenu (const Hit&);

        SessionWorkspace& workspace;
        const CommandRegistry& commands;
        RunCommand run;
        std::vector<Row> rows;
        juce::OwnedArray<Header> headers;
        bool dropOver = false;

        // Current mouse drag
        Drag drag = Drag::none;
        Hit dragHit;
        Session dragBase;
        juce::int64 dragStartFrame = 0, dragClipStart = 0;
        bool dragMoved = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimelineView)
    };
}
