// SPDX-License-Identifier: AGPL-3.0-only
#include "app/editor/WaveformView.h"
#include "app/theme/Fonts.h"
#include "app/theme/Palette.h"
#include "services/settings/SettingKeys.h"

namespace mxj
{
    namespace
    {
        /** Ruler steps in seconds; the first that leaves enough room per label wins. */
        constexpr double rulerSteps[] { 0.0001, 0.0002, 0.0005, 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5,
                                        1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200, 18000, 36000 };
        constexpr int minPixelsPerLabel = 90;
        constexpr int laneGap = 1;
        constexpr int spectralGap = 4;
        constexpr float waveformShare = 0.35f; ///< part of the height for the waveform when the spectrogram shows

        juce::String formatRulerTime (double seconds, double step)
        {
            const auto h = (int) (seconds / 3600.0);
            const auto m = (int) std::fmod (seconds / 60.0, 60.0);
            const auto s = std::fmod (seconds, 60.0);
            const int decimals = step >= 1.0 ? 0 : step >= 0.1 ? 1 : step >= 0.01 ? 2 : step >= 0.001 ? 3 : 4;

            auto sec = juce::String (s, decimals);
            if (s < 10.0)
                sec = "0" + sec;

            return (h > 0 ? juce::String (h) + ":" + juce::String (m).paddedLeft ('0', 2) : juce::String (m)) + ":" + sec;
        }
    }

    //==============================================================================
    /** The playhead line, on its own transparent layer so moving it repaints
        only two thin strips of the waveform. */
    class WaveformView::Playhead : public juce::Component
    {
    public:
        Playhead() { setInterceptsMouseClicks (false, false); }

        void setX (float newX, bool playing)
        {
            if (juce::approximatelyEqual (newX, x) && playing == isPlaying)
                return;

            repaintAround (x);
            x = newX;
            isPlaying = playing;
            repaintAround (x);
        }

        void paint (juce::Graphics& g) override
        {
            if (x < -1.0f || x > (float) getWidth() + 1.0f)
                return;

            g.setColour (palette::playhead);
            g.fillRect (juce::Rectangle<float> (x - 0.5f, 0.0f, 1.5f, (float) getHeight()));

            // Small triangle in the ruler.
            juce::Path marker;
            marker.addTriangle (x - 5.0f, 0.0f, x + 5.0f, 0.0f, x, 7.0f);
            g.fillPath (marker);
        }

    private:
        void repaintAround (float px)
        {
            repaint (juce::Rectangle<int> ((int) px - 7, 0, 15, getHeight()));
        }

        float x = -100.0f;
        bool isPlaying = false;
    };

    //==============================================================================
    int WaveformView::rulerHeight()
    {
        return fonts::scaled (24);
    }

    WaveformView::WaveformView (Workspace& w, const CommandRegistry& c, RunCommand r, Settings& s)
        : workspace (w), commands (c), run (std::move (r)), playhead (std::make_unique<Playhead>()), settings (s)
    {
        spectrogram.onPictureChanged = [this] { repaint (getSpectralArea()); };
        setOpaque (true);
        addAndMakeVisible (*playhead);
        workspace.addChangeListener (this);
        setMouseCursor (juce::MouseCursor::IBeamCursor);
        startTimerHz (60);
    }

    WaveformView::~WaveformView()
    {
        workspace.removeChangeListener (this);
    }

    void WaveformView::resized()
    {
        playhead->setBounds (getLocalBounds());
        workspace.setViewWidth (getWidth());
        updatePlayhead();
    }

    void WaveformView::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        repaint();
        updatePlayhead();
    }

    void WaveformView::timerCallback()
    {
        updatePlayhead();

        // While the file loads, the waveform grows from left to right.
        const auto* doc = workspace.getDocument();
        const bool loading = doc != nullptr && doc->isLoading();
        if (loading || wasLoading)
            repaint();
        wasLoading = loading;
    }

    void WaveformView::updatePlayhead()
    {
        const auto* doc = workspace.getDocument();
        playhead->setX (doc != nullptr ? frameToX ((double) workspace.getPlayheadPosition()) : -100.0f,
                        workspace.isPlaying());
    }

    double WaveformView::xToFrame (float x) const
    {
        const auto& v = workspace.getView();
        return v.startFrame + (double) x * v.framesPerPixel;
    }

    float WaveformView::frameToX (double frame) const
    {
        const auto& v = workspace.getView();
        return (float) ((frame - v.startFrame) / v.framesPerPixel);
    }

    juce::Rectangle<int> WaveformView::getLanesArea() const
    {
        return getLocalBounds().withTrimmedTop (rulerHeight());
    }

    bool WaveformView::isSpectralShown() const
    {
        return settings.getBool (keys::showSpectral) && workspace.hasDocument();
    }

    juce::Rectangle<int> WaveformView::getWaveformArea() const
    {
        auto lanes = getLanesArea();
        return isSpectralShown() ? lanes.removeFromTop (juce::roundToInt ((float) lanes.getHeight() * waveformShare)) : lanes;
    }

    juce::Rectangle<int> WaveformView::getSpectralArea() const
    {
        if (! isSpectralShown())
            return {};

        auto lanes = getLanesArea();
        lanes.removeFromTop (juce::roundToInt ((float) lanes.getHeight() * waveformShare) + spectralGap);
        return lanes;
    }

    std::vector<juce::Rectangle<int>> WaveformView::splitIntoLanes (juce::Rectangle<int> area) const
    {
        const auto* doc = workspace.getDocument();
        const int channels = doc != nullptr ? juce::jmax (1, doc->getNumChannels()) : 1;
        const int laneHeight = (area.getHeight() - laneGap * (channels - 1)) / channels;

        std::vector<juce::Rectangle<int>> lanes;
        for (int c = 0; c < channels; ++c)
        {
            lanes.push_back (area.removeFromTop (laneHeight));
            area.removeFromTop (laneGap);
        }

        return lanes;
    }

    dsp::FrequencyScale WaveformView::getFrequencyScale() const
    {
        const auto* doc = workspace.getDocument();
        return { dsp::FrequencyScale::fromName (settings.getString (keys::spectralScale)),
                 doc != nullptr ? doc->getSampleRate() * 0.5 : 24000.0 };
    }

    double WaveformView::hzAt (juce::Rectangle<int> lane, int y) const
    {
        const auto p = 1.0 - (double) (juce::jlimit (lane.getY(), lane.getBottom(), y) - lane.getY()) / juce::jmax (1, lane.getHeight());
        return getFrequencyScale().fromProportion (p);
    }

    float WaveformView::yForHz (juce::Rectangle<int> lane, double hz) const
    {
        return (float) lane.getY() + (float) ((1.0 - getFrequencyScale().toProportion (hz)) * lane.getHeight());
    }

    int WaveformView::spectralLaneAt (juce::Point<int> p) const
    {
        const auto area = getSpectralArea();
        if (! area.contains (p))
            return -1;

        const auto lanes = splitIntoLanes (area);
        for (size_t i = 0; i < lanes.size(); ++i)
            if (lanes[i].contains (p))
                return (int) i;

        return -1;
    }

    //==============================================================================
    void WaveformView::paint (juce::Graphics& g)
    {
        const auto clip = g.getClipBounds();
        g.setColour (palette::background);
        g.fillRect (clip);

        const auto* doc = workspace.getDocument();
        const int clipLeft = clip.getX();
        const int clipRight = clip.getRight();

        paintRuler (g, getLocalBounds().removeFromTop (rulerHeight()), clipLeft, clipRight);

        if (doc == nullptr)
            return;

        const auto lanes = splitIntoLanes (getWaveformArea());
        for (int c = 0; c < (int) lanes.size(); ++c)
            if (lanes[(size_t) c].intersects (clip))
                paintLane (g, lanes[(size_t) c], c, clipLeft, clipRight);

        if (isSpectralShown())
        {
            const auto area = getSpectralArea();
            g.setColour (palette::line);
            g.fillRect (area.getX(), area.getY() - spectralGap, area.getWidth(), spectralGap);
            if (area.intersects (clip))
                paintSpectral (g, clipLeft, clipRight);
        }
        else
        {
            spectrogram.clear();
        }

        paintSelection (g);
        paintMarkers (g, clipLeft, clipRight);

        // Unloaded part while opening.
        if (doc->isLoading() && doc->getLoadingPeaks() != nullptr)
        {
            const auto readyX = frameToX ((double) doc->getLoadingPeaks()->getFramesReady());
            if (readyX < (float) getWidth())
            {
                const auto rest = getLanesArea().toFloat().withTrimmedLeft (juce::jmax (0.0f, readyX));
                g.setColour (palette::background.withAlpha (0.6f));
                g.fillRect (rest);
                g.setColour (palette::textDim);
                g.setFont (fonts::sans (13.0f));
                g.drawText (TRANS ("Opening") + "... " + juce::String (juce::roundToInt (doc->getProgress() * 100.0)) + "%",
                            rest.toNearestInt(), juce::Justification::centred);
            }
        }
    }

    void WaveformView::paintRuler (juce::Graphics& g, juce::Rectangle<int> area, int clipLeft, int clipRight)
    {
        g.setColour (palette::panel);
        g.fillRect (area);
        g.setColour (palette::line);
        g.fillRect (area.removeFromBottom (1));

        const auto* doc = workspace.getDocument();
        if (doc == nullptr)
            return;

        const auto rate = doc->getSampleRate();
        const auto& v = workspace.getView();
        if (rate <= 0)
            return;
        const auto pixelsPerSecond = rate / v.framesPerPixel;

        double step = rulerSteps[std::size (rulerSteps) - 1];
        for (auto s : rulerSteps)
            if (s * pixelsPerSecond >= minPixelsPerLabel) { step = s; break; }

        const auto firstSecond = std::floor ((xToFrame ((float) clipLeft - minPixelsPerLabel) / rate) / step) * step;
        const auto lastSecond = xToFrame ((float) clipRight) / rate;

        g.setFont (fonts::mono (11.0f));

        for (auto t = juce::jmax (0.0, firstSecond); t <= lastSecond; t += step)
        {
            const auto x = frameToX (t * rate);
            g.setColour (palette::textDim);
            g.fillRect (juce::Rectangle<float> (x, (float) area.getBottom() - 8.0f, 1.0f, 8.0f));

            // Four small ticks between labels.
            for (int i = 1; i < 5; ++i)
            {
                const auto sx = x + (float) (i * step * pixelsPerSecond / 5.0);
                g.setColour (palette::line);
                g.fillRect (juce::Rectangle<float> (sx, (float) area.getBottom() - 4.0f, 1.0f, 4.0f));
            }

            g.setColour (palette::textDim);
            g.drawText (formatRulerTime (t, step), juce::Rectangle<float> (x + 4.0f, 0.0f, (float) minPixelsPerLabel, (float) area.getHeight() - 6.0f),
                        juce::Justification::centredLeft, false);
        }
    }

    void WaveformView::paintLane (juce::Graphics& g, juce::Rectangle<int> lane, int channel, int clipLeft, int clipRight)
    {
        const auto* doc = workspace.getDocument();
        const auto& v = workspace.getView();

        g.setColour (palette::panel);
        g.fillRect (lane.getX(), lane.getY(), lane.getWidth(), lane.getHeight());

        // Centre line and -6 dB guides.
        const auto mid = (float) lane.getCentreY();
        const auto half = (float) lane.getHeight() * 0.5f - 2.0f;
        g.setColour (palette::line);
        g.fillRect (juce::Rectangle<float> ((float) clipLeft, mid, (float) (clipRight - clipLeft), 1.0f));
        g.setColour (palette::line.withAlpha (0.45f));
        g.fillRect (juce::Rectangle<float> ((float) clipLeft, mid - half * 0.5f, (float) (clipRight - clipLeft), 1.0f));
        g.fillRect (juce::Rectangle<float> ((float) clipLeft, mid + half * 0.5f, (float) (clipRight - clipLeft), 1.0f));

        // End of file.
        const auto endX = frameToX ((double) doc->getLength());
        if (endX < (float) clipRight)
        {
            g.setColour (palette::background);
            g.fillRect (juce::Rectangle<float> (endX, (float) lane.getY(), (float) clipRight - endX, (float) lane.getHeight()));
        }

        const bool closeZoom = v.framesPerPixel < (double) PeakCache::baseFrames;

        if (closeZoom && doc->isReady())
            paintSamples (g, lane, channel, clipLeft, clipRight);
        else
            paintPeaks (g, lane, channel, clipLeft, juce::jmin (clipRight, (int) std::ceil (endX)));

        // Channel label.
        if (clipLeft < 40)
        {
            const auto channels = doc->getNumChannels();
            const auto label = channels == 2 ? (channel == 0 ? "L" : "R") : juce::String (channel + 1);
            g.setColour (palette::textDim);
            g.setFont (fonts::sansBold (11.0f));
            g.drawText (label, lane.getX() + 6, lane.getY() + 4, 20, 14, juce::Justification::centredLeft);
        }
    }

    void WaveformView::paintPeaks (juce::Graphics& g, juce::Rectangle<int> lane, int channel, int clipLeft, int clipRight)
    {
        const auto* doc = workspace.getDocument();
        const auto loadingPeaks = doc->isLoading() ? doc->getLoadingPeaks() : nullptr;
        const auto& audio = doc->getAudio();
        const auto mid = (float) lane.getCentreY();
        const auto half = (float) lane.getHeight() * 0.5f - 2.0f;
        g.setColour (palette::accent);

        for (int x = clipLeft; x < clipRight; ++x)
        {
            const auto start = (juce::int64) std::floor (xToFrame ((float) x));
            const auto end = juce::jmax (start + 1, (juce::int64) std::floor (xToFrame ((float) x + 1.0f)));
            float lo, hi;

            const bool known = loadingPeaks != nullptr ? loadingPeaks->getMinMax (channel, start, end, lo, hi)
                                                       : audio.getMinMax (channel, start, end, lo, hi);
            if (! known)
                continue;

            const auto top = mid - juce::jlimit (-1.0f, 1.0f, hi) * half;
            const auto bottom = mid - juce::jlimit (-1.0f, 1.0f, lo) * half;
            g.fillRect (juce::Rectangle<float> ((float) x, top, 1.0f, juce::jmax (1.0f, bottom - top)));
        }
    }

    void WaveformView::paintSamples (juce::Graphics& g, juce::Rectangle<int> lane, int channel, int clipLeft, int clipRight)
    {
        const auto* doc = workspace.getDocument();
        const auto& v = workspace.getView();
        const auto& seq = doc->getAudio();

        const auto mid = (float) lane.getCentreY();
        const auto half = (float) lane.getHeight() * 0.5f - 2.0f;

        // One extra sample on each side so lines run off the edges.
        const auto first = juce::jmax ((juce::int64) 0, (juce::int64) std::floor (xToFrame ((float) clipLeft)) - 1);
        const auto last = juce::jmin (seq.getNumFrames(), (juce::int64) std::ceil (xToFrame ((float) clipRight)) + 2);
        const auto count = (int) (last - first);
        if (count <= 0)
            return;

        sampleBuffer.setSize (seq.getNumChannels(), count, false, false, true);
        seq.read (sampleBuffer.getArrayOfWritePointers(), seq.getNumChannels(), first, count);
        const auto* data = sampleBuffer.getReadPointer (channel);

        auto yFor = [&] (float s) { return mid - juce::jlimit (-1.0f, 1.0f, s) * half; };

        if (v.framesPerPixel >= 1.0)
        {
            // Several samples per pixel: min/max per column, exact.
            g.setColour (palette::accent);
            for (int x = clipLeft; x < clipRight; ++x)
            {
                const auto s = (juce::int64) std::floor (xToFrame ((float) x)) - first;
                const auto e = juce::jmin ((juce::int64) count, (juce::int64) std::floor (xToFrame ((float) x + 1.0f)) - first);
                if (s < 0 || s >= e)
                    continue;

                const auto range = juce::FloatVectorOperations::findMinAndMax (data + s, (int) (e - s));
                const auto top = yFor (range.getEnd());
                g.fillRect (juce::Rectangle<float> ((float) x, top, 1.0f, juce::jmax (1.0f, yFor (range.getStart()) - top)));
            }
            return;
        }

        // Fewer than one sample per pixel: a line through the samples.
        juce::Path path;
        for (int i = 0; i < count; ++i)
        {
            const auto p = juce::Point<float> (frameToX ((double) (first + i)), yFor (data[i]));
            if (i == 0) path.startNewSubPath (p); else path.lineTo (p);
        }

        g.setColour (palette::accent);
        g.strokePath (path, juce::PathStrokeType (1.5f));

        // Very close: every sample as a dot.
        if (v.framesPerPixel <= 1.0 / 8.0)
            for (int i = 0; i < count; ++i)
                g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ frameToX ((double) (first + i)), yFor (data[i]) }));
    }

    //==============================================================================
    void WaveformView::paintSelection (juce::Graphics& g)
    {
        const auto sel = workspace.getSelection();
        if (sel.isEmpty())
            return;

        const auto left = juce::jmax (-2.0f, frameToX ((double) sel.getStart()));
        const auto right = juce::jmin ((float) getWidth() + 2.0f, frameToX ((double) sel.getEnd()));
        if (right < 0.0f || left > (float) getWidth())
            return;

        auto drawRange = [&] (juce::Rectangle<float> area, juce::Colour fill, juce::Colour edge)
        {
            g.setColour (fill);
            g.fillRect (area);
            g.setColour (edge);
            g.fillRect (area.withWidth (1.0f));
            g.fillRect (area.withX (area.getRight() - 1.0f).withWidth (1.0f));
        };

        const auto band = workspace.getSelectedBand();
        if (band.isEmpty() || ! isSpectralShown())
        {
            drawRange (juce::Rectangle<float> (left, 0.0f, juce::jmax (1.0f, right - left), (float) getHeight()),
                       palette::selection, palette::playhead.withAlpha (0.7f));
            return;
        }

        // Spectral selection: the time range lightly in the waveform, a rectangle in each spectrogram lane.
        const auto wave = getWaveformArea().toFloat();
        drawRange (juce::Rectangle<float> (left, 0.0f, juce::jmax (1.0f, right - left), wave.getBottom()),
                   palette::selection.withMultipliedAlpha (0.5f), palette::playhead.withAlpha (0.4f));

        for (const auto& lane : splitIntoLanes (getSpectralArea()))
        {
            const auto top = yForHz (lane, band.getEnd()), bottom = yForHz (lane, band.getStart());
            const auto box = juce::Rectangle<float> (left, top, juce::jmax (1.0f, right - left), juce::jmax (1.0f, bottom - top));
            g.setColour (palette::spectrogram::selectionFill);
            g.fillRect (box);
            g.setColour (palette::spectrogram::selectionLine);
            g.drawRect (box, 1.5f);
        }
    }

    void WaveformView::requestSpectrogram()
    {
        const auto* doc = workspace.getDocument();
        if (doc == nullptr || ! doc->isReady() || ! isSpectralShown())
            return;

        const auto lanes = splitIntoLanes (getSpectralArea());
        if (lanes.empty() || lanes.front().getHeight() <= 0)
            return;

        SpectrogramRenderer::Request r;
        r.audio = doc->getAudio();
        r.startFrame = workspace.getView().startFrame;
        r.framesPerPixel = workspace.getView().framesPerPixel;
        r.width = getWidth();
        r.height = lanes.front().getHeight();
        r.fftSize = juce::jlimit (256, 16384, (int) settings.get (keys::spectralResolution));
        r.scale = dsp::FrequencyScale::fromName (settings.getString (keys::spectralScale));
        r.colours = settings.getString (keys::spectralColours);
        spectrogram.request (r);
    }

    void WaveformView::paintSpectral (juce::Graphics& g, int clipLeft, int clipRight)
    {
        const auto* doc = workspace.getDocument();
        requestSpectrogram();

        const auto lanes = splitIntoLanes (getSpectralArea());
        const auto* complete = spectrogram.getCompletePicture();
        const auto* newest = spectrogram.getPicture();

        for (int c = 0; c < (int) lanes.size(); ++c)
        {
            const auto& lane = lanes[(size_t) c];
            g.setColour (SpectrogramRenderer::colourFor (settings.getString (keys::spectralColours), 0.0f));
            g.fillRect (lane.withLeft (clipLeft).withRight (clipRight));

            if (doc->isReady())
            {
                if (complete != nullptr)
                    paintPicture (g, *complete, c, lane, false);
                if (newest != nullptr && newest != complete && ! newest->isComplete())
                    paintPicture (g, *newest, c, lane, true);
            }

            // End of file.
            const auto endX = frameToX ((double) doc->getLength());
            if (endX < (float) clipRight)
            {
                g.setColour (palette::background);
                g.fillRect (juce::Rectangle<float> (endX, (float) lane.getY(), (float) clipRight - endX, (float) lane.getHeight()));
            }

            if (clipLeft < 80)
                paintFrequencyLabels (g, lane);
        }
    }

    void WaveformView::paintPicture (juce::Graphics& g, const SpectrogramRenderer::Picture& picture, int channel,
                                     juce::Rectangle<int> lane, bool partial)
    {
        if (channel >= (int) picture.channels.size() || picture.request.height <= 0)
            return;

        const auto& v = workspace.getView();
        const auto& req = picture.request;
        const auto sx = req.framesPerPixel / v.framesPerPixel;
        const auto tx = (req.startFrame - v.startFrame) / v.framesPerPixel;
        const auto sy = (double) lane.getHeight() / req.height;

        juce::Graphics::ScopedSaveState save (g);
        auto clip = lane;
        if (partial)
            clip = clip.withRight (juce::jmin (lane.getRight(), lane.getX() + (int) std::floor (tx + picture.columnsDone * sx)));
        g.reduceClipRegion (clip);
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImageTransformed (picture.channels[(size_t) channel],
                                juce::AffineTransform::scale ((float) sx, (float) sy)
                                    .translated ((float) (lane.getX() + tx), (float) lane.getY()));
    }

    void WaveformView::paintFrequencyLabels (juce::Graphics& g, juce::Rectangle<int> lane)
    {
        static const double candidates[] { 50, 100, 200, 500, 1000, 2000, 3000, 4000, 5000, 6000, 8000, 10000, 12000,
                                           15000, 16000, 20000, 24000, 30000, 40000, 48000, 60000, 80000, 96000 };
        const auto nyquist = getFrequencyScale().nyquist;
        g.setFont (fonts::mono (10.0f));
        float lastY = 1.0e9f;

        for (auto hz : candidates)
        {
            if (hz >= nyquist)
                break;

            const auto y = yForHz (lane, hz);
            if (lastY - y < 16.0f || y < (float) lane.getY() + 6.0f)
                continue;

            lastY = y;
            const auto text = hz >= 1000.0 ? juce::String (hz / 1000.0, hz < 10000.0 && std::fmod (hz, 1000.0) != 0.0 ? 1 : 0) + "k"
                                           : juce::String ((int) hz);
            g.setColour (palette::background.withAlpha (0.6f));
            g.fillRect (juce::Rectangle<float> ((float) lane.getX(), y - 6.0f, 34.0f, 12.0f));
            g.setColour (palette::text.withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float> ((float) lane.getX(), y, 4.0f, 1.0f));
            g.drawText (text, juce::Rectangle<float> ((float) lane.getX() + 6.0f, y - 6.0f, 30.0f, 12.0f),
                        juce::Justification::centredLeft, false);
        }
    }

    void WaveformView::paintMarkers (juce::Graphics& g, int clipLeft, int clipRight)
    {
        const auto* doc = workspace.getDocument();
        const auto& markers = doc->getMarkers();
        const auto selected = workspace.getSelectedMarker();
        g.setFont (fonts::sansBold (11.0f));

        for (int i = 0; i < (int) markers.size(); ++i)
        {
            const auto& m = markers[(size_t) i];
            const auto x = frameToX ((double) m.position);
            const auto labelWidth = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), m.name) + 10.0f;
            if (x + labelWidth + 12.0f < (float) clipLeft || x - 2.0f > (float) clipRight)
                continue;

            const auto colour = i == selected ? palette::text : palette::accent;

            // Dashed line through the lanes.
            g.setColour (colour.withAlpha (0.55f));
            for (float y = (float) rulerHeight(); y < (float) getHeight(); y += 6.0f)
                g.fillRect (juce::Rectangle<float> (x, y, 1.0f, 3.0f));

            // Flag with the name in the ruler.
            const auto flag = juce::Rectangle<float> (x, 2.0f, labelWidth, (float) rulerHeight() - 10.0f);
            g.setColour (colour);
            g.fillRect (flag);
            g.fillRect (juce::Rectangle<float> (x, 0.0f, 1.0f, (float) rulerHeight()));
            g.setColour (palette::onAccent);
            g.drawText (m.name, flag.withTrimmedLeft (5.0f), juce::Justification::centredLeft, false);
        }
    }

    juce::int64 WaveformView::frameAt (int x) const
    {
        const auto* doc = workspace.getDocument();
        const auto length = doc != nullptr ? doc->getLength() : 0;
        return juce::jlimit ((juce::int64) 0, length, (juce::int64) std::llround (xToFrame ((float) x)));
    }

    int WaveformView::markerAt (int x) const
    {
        const auto* doc = workspace.getDocument();
        if (doc == nullptr)
            return -1;

        const auto& markers = doc->getMarkers();
        for (int i = (int) markers.size(); --i >= 0;)
        {
            const auto mx = frameToX ((double) markers[(size_t) i].position);
            if ((float) x >= mx - 4.0f && (float) x <= mx + 12.0f)
                return i;
        }

        return -1;
    }

    void WaveformView::mouseMove (const juce::MouseEvent& e)
    {
        if (! workspace.isReady())
            return;

        if (e.y < rulerHeight())
        {
            setMouseCursor (markerAt (e.x) >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            return;
        }

        if (spectralLaneAt (e.getPosition()) >= 0)
        {
            setMouseCursor (juce::MouseCursor::CrosshairCursor);
            return;
        }

        // Near an edge of the selection: it can be dragged.
        const auto sel = workspace.getSelection();
        const bool nearEdge = ! sel.isEmpty()
                           && (std::abs ((float) e.x - frameToX ((double) sel.getStart())) < 5.0f
                               || std::abs ((float) e.x - frameToX ((double) sel.getEnd())) < 5.0f);
        setMouseCursor (nearEdge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::IBeamCursor);
    }

    void WaveformView::mouseDown (const juce::MouseEvent& e)
    {
        drag = Drag::none;

        if (! workspace.hasDocument())
            return;

        if (e.mods.isPopupMenu())
        {
            if (e.y < rulerHeight())
                if (const auto m = markerAt (e.x); m >= 0)
                    workspace.selectMarker (m);

            showContextMenu();
            return;
        }

        if (! workspace.isReady() || workspace.isRecording())
            return;

        if (e.y < rulerHeight())
        {
            if (const auto m = markerAt (e.x); m >= 0)
            {
                workspace.selectMarker (m);
                return;
            }
        }

        const auto frame = frameAt (e.x);
        const auto sel = workspace.getSelection();

        // In the spectrogram: drag a rectangle (time and frequency).
        if (const auto lane = spectralLaneAt (e.getPosition()); lane >= 0 && ! e.mods.isShiftDown())
        {
            drag = Drag::spectral;
            dragAnchor = frame;
            dragLane = splitIntoLanes (getSpectralArea())[(size_t) lane];
            dragAnchorHz = hzAt (dragLane, e.y);
            workspace.setCursor (frame);
            return;
        }

        if (e.mods.isShiftDown())
        {
            // Extend the selection (or select from the cursor) to the click.
            dragAnchor = sel.isEmpty() ? workspace.getCursor()
                       : (std::abs (frame - sel.getStart()) < std::abs (frame - sel.getEnd()) ? sel.getEnd() : sel.getStart());
            drag = Drag::select;
            workspace.setSelection ({ juce::jmin (dragAnchor, frame), juce::jmax (dragAnchor, frame) });
            return;
        }

        if (! sel.isEmpty() && std::abs ((float) e.x - frameToX ((double) sel.getStart())) < 5.0f)
        {
            drag = Drag::select;
            dragAnchor = sel.getEnd();
            return;
        }

        if (! sel.isEmpty() && std::abs ((float) e.x - frameToX ((double) sel.getEnd())) < 5.0f)
        {
            drag = Drag::select;
            dragAnchor = sel.getStart();
            return;
        }

        drag = Drag::select;
        dragAnchor = frame;
        workspace.setCursor (frame);
    }

    void WaveformView::mouseDrag (const juce::MouseEvent& e)
    {
        if ((drag != Drag::select && drag != Drag::spectral) || ! workspace.isReady() || e.getDistanceFromDragStart() < 3)
            return;

        // Dragging past an edge scrolls the view.
        if (e.x < 0 || e.x > getWidth())
        {
            auto v = workspace.getView();
            v.startFrame += (double) (e.x < 0 ? e.x : e.x - getWidth()) * v.framesPerPixel * 0.5;
            workspace.setView (v);
        }

        const auto frame = frameAt (juce::jlimit (0, getWidth(), e.x));
        const FrameRange range { juce::jmin (dragAnchor, frame), juce::jmax (dragAnchor, frame) };

        if (drag == Drag::spectral)
        {
            const auto hz = hzAt (dragLane, e.y);
            workspace.setSpectralSelection (range, { juce::jmin (dragAnchorHz, hz), juce::jmax (dragAnchorHz, hz) });
            return;
        }

        workspace.setSelection (range);
    }

    void WaveformView::mouseUp (const juce::MouseEvent&)
    {
        drag = Drag::none;
    }

    void WaveformView::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (e.y < rulerHeight())
        {
            if (markerAt (e.x) >= 0)
                run ("markers.rename");
            return;
        }

        // Double click selects what is visible, as in Audition.
        if (workspace.isReady())
        {
            const auto& v = workspace.getView();
            workspace.setSelection ({ (juce::int64) v.startFrame, (juce::int64) (v.startFrame + v.framesPerPixel * getWidth()) });
        }
    }

    void WaveformView::showContextMenu()
    {
        juce::PopupMenu menu;

        auto add = [&] (const char* id)
        {
            if (const auto* info = commands.find (id))
            {
                juce::PopupMenu::Item item (TRANS (info->name));
                item.setEnabled (commands.isEnabled (id));
                if (info->defaultShortcut.isNotEmpty())
                    item.shortcutKeyDescription = juce::KeyPress::createFromDescription (info->defaultShortcut).getTextDescriptionWithIcons();
                item.setAction ([this, cmd = juce::String (id)] { run (cmd); });
                menu.addItem (item);
            }
        };

        for (const auto* id : { "edit.cut", "edit.copy", "edit.paste", "edit.mixPaste", "edit.delete" })
            add (id);
        menu.addSeparator();
        for (const auto* id : { "edit.selectAll", "edit.trim", "edit.silence", "edit.fadeIn", "edit.fadeOut" })
            add (id);
        menu.addSeparator();
        for (const auto* id : { "markers.add", "markers.rename", "markers.delete" })
            add (id);
        menu.addSeparator();
        for (const auto* id : { "spectral.heal", "spectral.changeLevel", "spectral.deleteBand", "repair.captureNoisePrint" })
            add (id);

        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
    }

    void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        if (! workspace.hasDocument())
            return;

        if (e.mods.isCommandDown())
        {
            workspace.zoomBy (std::pow (2.0, (double) wheel.deltaY * 2.0), e.x);
            return;
        }

        // Vertical wheel scrolls sideways too; trackpads give deltaX directly.
        const auto delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        auto v = workspace.getView();
        v.startFrame -= (double) delta * getWidth() * 0.5 * v.framesPerPixel;
        workspace.setView (v);
    }

    void WaveformView::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
    {
        if (workspace.hasDocument())
            workspace.zoomBy (scaleFactor, e.x);
    }
}
