// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_core/juce_core.h>

namespace mxj
{
    /** The program's log file.

        One file per run in the logs folder (mxj_YYYY-MM-DD_HH-MM-SS.log); only the
        newest files are kept. Logs never contain audio, file contents or personal
        data beyond file paths. Never log from the audio thread: writing takes a lock.
    */
    class Log
    {
    public:
        enum class Level { debug, info, warning, error };

        /** Opens a new log file and makes it the current JUCE logger.
            Safe to call again; the previous log is closed first. */
        static bool start (const juce::File& logsFolder, int filesToKeep = 10);

        /** Closes the log file. Call before the program quits. */
        static void stop();

        static void write (Level level, const juce::String& message);

        static void debug   (const juce::String& m) { write (Level::debug, m); }
        static void info    (const juce::String& m) { write (Level::info, m); }
        static void warning (const juce::String& m) { write (Level::warning, m); }
        static void error   (const juce::String& m) { write (Level::error, m); }

        /** The file of the current run, or an empty File if logging is off. */
        static juce::File getCurrentFile();

        /** Deletes the oldest log files so that at most `filesToKeep` remain. */
        static void pruneOldFiles (const juce::File& logsFolder, int filesToKeep);
    };
}
