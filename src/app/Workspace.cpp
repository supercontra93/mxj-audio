// SPDX-License-Identifier: AGPL-3.0-only
#include "app/Workspace.h"
#include "services/logging/Log.h"
#include "services/settings/SettingKeys.h"

namespace mxj
{
    Workspace::Workspace (AppContext& ctx) : context (ctx)
    {
        startTimerHz (30);
    }

    Workspace::~Workspace()
    {
        context.audio.getPlayback().setSequence ({});
        document = nullptr;
    }

    void Workspace::setStatus (const juce::String& message)
    {
        if (onStatusMessage != nullptr)
            onStatusMessage (message);
    }

    //==============================================================================
    void Workspace::showOpenDialog()
    {
        auto folder = juce::File (context.settings.getString (keys::lastOpenFolder));
        if (! folder.isDirectory())
            folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory);

        chooser = std::make_unique<juce::FileChooser> (TRANS ("Open audio file"), folder,
                                                       audioFormats::getWildcard(), true);

        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file != juce::File())
            {
                context.settings.set (keys::lastOpenFolder, file.getParentDirectory().getFullPathName());
                openFile (file);
            }
        });
    }

    bool Workspace::openFile (const juce::File& file)
    {
        closeFile();

        juce::String error;
        document = AudioDocument::open (file, context.blocks(), context.paths.getCacheFolder().getChildFile ("peaks"), error);

        if (document == nullptr)
        {
            Log::warning ("Could not open " + file.getFullPathName() + ": " + error);
            setStatus (TRANS ("Could not open the file."));
            juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon,
                                              TRANS ("Could not open the file"),
                                              file.getFileName() + "\n\n" + error),
                                          nullptr);
            sendChangeMessage();
            return false;
        }

        const auto& info = document->getInfo();
        Log::info ("Opening " + file.getFullPathName() + " (" + info.formatName + ", "
                   + juce::String (info.numChannels) + " ch, " + juce::String (info.sampleRate) + " Hz, "
                   + juce::String (info.numFrames) + " frames)");

        document->onLoadFinished = [this] { loadFinished(); };
        cursor = 0;
        view = {};
        zoomToFit();
        setStatus (TRANS ("Opening") + " " + file.getFileName() + "...");
        sendChangeMessage();
        return true;
    }

    void Workspace::loadFinished()
    {
        if (document == nullptr)
            return;

        if (document->isReady())
        {
            context.audio.getPlayback().setSequence (document->getSequence());
            Log::info ("Opened in " + juce::String (document->getLoadSeconds(), 2) + " s"
                       + (document->usedCachedPeaks() ? " (peaks from cache)" : ""));
            setStatus (TRANS ("Opened") + " " + document->getFile().getFileName()
                       + " (" + juce::String (document->getLoadSeconds(), 1) + " s)");
        }
        else
        {
            Log::warning ("Loading failed: " + document->getError());
            setStatus (TRANS ("Could not open the file."));
            juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon,
                                              TRANS ("Could not open the file"),
                                              document->getFile().getFileName() + "\n\n" + document->getError()),
                                          nullptr);
        }

        sendChangeMessage();
    }

    void Workspace::closeFile()
    {
        if (document == nullptr)
            return;

        stop();
        context.audio.getPlayback().setSequence ({});
        document = nullptr;
        cursor = 0;
        setStatus (TRANS ("Ready"));
        sendChangeMessage();
    }

    //==============================================================================
    void Workspace::togglePlayback()
    {
        if (isPlaying())
            stop();
        else
            play();
    }

    void Workspace::play()
    {
        if (! isReady())
            return;

        auto& playback = context.audio.getPlayback();
        auto start = cursor;
        if (start >= document->getSequence().getNumFrames())
            start = 0;

        if (! playback.play (start))
        {
            setStatus (TRANS ("Cannot play: no audio output device is running. Check Edit > Audio Device Settings."));
            return;
        }

        wasPlaying = true;
        setStatus (TRANS ("Playing"));
        sendChangeMessage();
    }

    void Workspace::stop()
    {
        auto& playback = context.audio.getPlayback();
        const bool playing = playback.isPlaying() || wasPlaying;
        playback.stop();

        if (playing)
        {
            wasPlaying = false;

            if (playback.getUnderrunCount() > 0)
                Log::warning ("Playback had " + juce::String (playback.getUnderrunCount()) + " underruns");

            setStatus (TRANS ("Stopped"));
            sendChangeMessage();
        }
    }

    bool Workspace::isPlaying() const
    {
        return context.audio.getPlayback().isPlaying();
    }

    void Workspace::setCursor (juce::int64 frame)
    {
        if (document == nullptr)
            return;

        cursor = juce::jlimit ((juce::int64) 0, document->getInfo().numFrames, frame);

        if (isPlaying())
            play();

        sendChangeMessage();
    }

    juce::int64 Workspace::getPlayheadPosition() const
    {
        return isPlaying() ? context.audio.getPlayback().getPosition() : cursor;
    }

    void Workspace::goToStart()
    {
        setCursor (0);
        auto v = view;
        v.startFrame = 0;
        setView (v);
    }

    void Workspace::goToEnd()
    {
        if (document == nullptr)
            return;

        stop();
        setCursor (document->getInfo().numFrames);
        auto v = view;
        v.startFrame = (double) document->getInfo().numFrames - v.framesPerPixel * viewWidth * 0.9;
        setView (v);
    }

    void Workspace::timerCallback()
    {
        auto& playback = context.audio.getPlayback();

        if (playback.takeFinishedFlag() || (wasPlaying && ! playback.isPlaying()))
            stop(); // reached the end, or the device changed

        // Follow the playhead page by page, like Audition and Audacity.
        if (isPlaying())
        {
            const auto pos = (double) playback.getPosition();
            const auto right = view.startFrame + view.framesPerPixel * viewWidth;
            if (pos >= right || pos < view.startFrame)
            {
                auto v = view;
                v.startFrame = pos - view.framesPerPixel * viewWidth * 0.05;
                setView (v);
            }
        }
    }

    //==============================================================================
    double Workspace::getMaxFramesPerPixel() const
    {
        const auto frames = document != nullptr ? (double) document->getInfo().numFrames : 48000.0;
        return juce::jmax (1.0, frames / juce::jmax (1, viewWidth));
    }

    void Workspace::setView (View v)
    {
        v.framesPerPixel = juce::jlimit (minFramesPerPixel, getMaxFramesPerPixel(), v.framesPerPixel);

        const auto frames = document != nullptr ? (double) document->getInfo().numFrames : 0.0;
        const auto visible = v.framesPerPixel * viewWidth;
        v.startFrame = juce::jlimit (0.0, juce::jmax (0.0, frames - visible), v.startFrame);

        if (! juce::approximatelyEqual (v.startFrame, view.startFrame)
            || ! juce::approximatelyEqual (v.framesPerPixel, view.framesPerPixel))
        {
            view = v;
            sendChangeMessage();
        }
    }

    void Workspace::setViewWidth (int pixels)
    {
        viewWidth = juce::jmax (1, pixels);
        setView (view);
    }

    double Workspace::zoomAnchor() const
    {
        // Zoom around the playhead when it is visible, otherwise around the middle.
        const auto x = ((double) getPlayheadPosition() - view.startFrame) / view.framesPerPixel;
        return (x >= 0 && x <= viewWidth) ? x : viewWidth * 0.5;
    }

    void Workspace::zoomBy (double factor, double anchorX)
    {
        const auto anchorFrame = view.startFrame + anchorX * view.framesPerPixel;
        View v;
        v.framesPerPixel = juce::jlimit (minFramesPerPixel, getMaxFramesPerPixel(), view.framesPerPixel / factor);
        v.startFrame = anchorFrame - anchorX * v.framesPerPixel;
        setView (v);
    }

    void Workspace::zoomToFit()
    {
        setView ({ 0.0, getMaxFramesPerPixel() });
    }

    juce::String Workspace::formatTime (juce::int64 frame, double sampleRate)
    {
        if (sampleRate <= 0)
            return "0:00:00.000";

        const auto totalMs = (juce::int64) std::floor ((double) frame * 1000.0 / sampleRate);
        const auto ms = (int) (totalMs % 1000);
        const auto s = (int) ((totalMs / 1000) % 60);
        const auto m = (int) ((totalMs / 60000) % 60);
        const auto h = (int) (totalMs / 3600000);

        return juce::String (h) + ":" + juce::String (m).paddedLeft ('0', 2) + ":"
             + juce::String (s).paddedLeft ('0', 2) + "." + juce::String (ms).paddedLeft ('0', 3);
    }
}
