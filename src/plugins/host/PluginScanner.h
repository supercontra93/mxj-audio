// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "plugins/host/PluginHost.h"

namespace mxj::plugins
{
    /** Scans plugin files for the PluginHost (master plan, section 6: "plugins
        are scanned in a separate process; one that crashes while being scanned
        is blocked, the program does not crash").

        Each file is opened by the small "MXJ Plugin Scanner" program next to
        MXJ AUDIO. If it crashes, hangs (60 seconds) or fails, the file goes on
        the block list. Without the scanner program (tests) files are scanned
        in this process.

        Runs on its own thread; start() and cancel() on the message thread.
        onFinished is called on the message thread.
    */
    class PluginScanner : private juce::Thread
    {
    public:
        PluginScanner (PluginHost& host, juce::File scannerProgram);
        ~PluginScanner() override;

        /** Starts scanning new and changed files (all files with `everything`). */
        void start (bool everything);
        void cancel();
        bool isScanning() const { return isThreadRunning(); }

        /** 0..1, and the file being scanned. */
        double getProgress() const noexcept { return progress.load(); }
        juce::String getCurrentFile() const;

        /** Called when a scan ends: how many plugins were found and files blocked in this scan. */
        std::function<void (int found, int blocked)> onFinished;

        /** Seconds before a file that does not answer is blocked. */
        static constexpr int timeoutSeconds = 60;

        /** Scans one file in this process. Used by the scanner program. */
        static void scanFile (juce::AudioPluginFormat& format, const juce::String& file,
                              juce::OwnedArray<juce::PluginDescription>& results);

        /** The file the scanner program writes. */
        static bool writeResults (const juce::File& file, const juce::OwnedArray<juce::PluginDescription>& results);
        static bool readResults (const juce::File& file, juce::OwnedArray<juce::PluginDescription>& results);

    private:
        void run() override;
        bool scanInChildProcess (const juce::String& format, const juce::String& file,
                                 juce::OwnedArray<juce::PluginDescription>& results, juce::String& reason);

        PluginHost& host;
        const juce::File scanner;
        bool scanEverything = false;
        std::atomic<double> progress { 0.0 };
        mutable std::mutex lock;
        juce::String current;
        int found = 0, failed = 0;
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    };
}
