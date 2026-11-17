// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "app/Workspace.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mxj
{
    /** Draws the open file: a time ruler on top, then one lane per channel.

        Far out it draws from the PeakCache; close in it reads the samples
        themselves, and at the closest zoom it shows every sample as a dot.
        Only the part of the view that needs repainting is drawn, so the moving
        playhead costs little.

        Mouse: click places the cursor, wheel scrolls, Ctrl + wheel zooms
        around the mouse.
    */
    class WaveformView : public juce::Component,
                         private juce::ChangeListener,
                         private juce::Timer
    {
    public:
        explicit WaveformView (Workspace& workspace);
        ~WaveformView() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

        static constexpr int rulerHeight = 24;

    private:
        class Playhead;

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;

        void paintRuler (juce::Graphics&, juce::Rectangle<int> area, int clipLeft, int clipRight);
        void paintLane (juce::Graphics&, juce::Rectangle<int> lane, int channel, int clipLeft, int clipRight);
        void paintPeaks (juce::Graphics&, juce::Rectangle<int> lane, int channel, int clipLeft, int clipRight);
        void paintSamples (juce::Graphics&, juce::Rectangle<int> lane, int channel, int clipLeft, int clipRight);

        juce::Rectangle<int> getLanesArea() const;
        double xToFrame (float x) const;
        float frameToX (double frame) const;
        void updatePlayhead();

        Workspace& workspace;
        std::unique_ptr<Playhead> playhead;
        juce::AudioBuffer<float> sampleBuffer; ///< samples read for close zoom levels
        bool wasLoading = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformView)
    };
}
