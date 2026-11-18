// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "app/Workspace.h"
#include "core/commands/CommandRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** Draws the open file: a time ruler on top, then one lane per channel.

        Far out it draws from the PeakCache; close in it reads the samples
        themselves, and at the closest zoom it shows every sample as a dot.
        Only the part of the view that needs repainting is drawn, so the moving
        playhead costs little.

        Mouse: click places the cursor, dragging selects (Shift + click extends
        the selection, dragging an edge of it moves that edge), clicking a marker
        in the ruler jumps to it, right click opens the edit menu, wheel scrolls,
        Ctrl + wheel zooms around the mouse.
    */
    class WaveformView : public juce::Component,
                         private juce::ChangeListener,
                         private juce::Timer
    {
    public:
        using RunCommand = std::function<void (const CommandId&)>;

        WaveformView (Workspace& workspace, const CommandRegistry& commands, RunCommand runCommand);
        ~WaveformView() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

        /** Height of the time ruler (grows with the text size). */
        static int rulerHeight();

    private:
        class Playhead;

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;

        void paintRuler (juce::Graphics&, juce::Rectangle<int> area, int clipLeft, int clipRight);
        void paintLane (juce::Graphics&, juce::Rectangle<int> lane, int channel, int clipLeft, int clipRight);
        void paintPeaks (juce::Graphics&, juce::Rectangle<int> lane, int channel, int clipLeft, int clipRight);
        void paintSamples (juce::Graphics&, juce::Rectangle<int> lane, int channel, int clipLeft, int clipRight);
        void paintSelection (juce::Graphics&);
        void paintMarkers (juce::Graphics&, int clipLeft, int clipRight);

        /** Index of the marker whose flag is under x in the ruler, or -1. */
        int markerAt (int x) const;
        void showContextMenu();
        juce::int64 frameAt (int x) const;

        juce::Rectangle<int> getLanesArea() const;
        double xToFrame (float x) const;
        float frameToX (double frame) const;
        void updatePlayhead();

        Workspace& workspace;
        const CommandRegistry& commands;
        RunCommand run;
        std::unique_ptr<Playhead> playhead;

        enum class Drag { none, select, startEdge, endEdge };
        Drag drag = Drag::none;
        juce::int64 dragAnchor = 0;
        juce::AudioBuffer<float> sampleBuffer; ///< samples read for close zoom levels
        bool wasLoading = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformView)
    };
}
