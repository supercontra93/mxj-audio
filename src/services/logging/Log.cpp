// SPDX-License-Identifier: AGPL-3.0-only
#include "services/logging/Log.h"
#include "core/Version.h"

#include <algorithm>
#include <memory>

namespace mxj
{
    namespace
    {
        std::unique_ptr<juce::FileLogger> logger;
        juce::CriticalSection loggerLock;

        const char* levelName (Log::Level level)
        {
            switch (level)
            {
                case Log::Level::debug:   return "DEBUG";
                case Log::Level::info:    return "INFO ";
                case Log::Level::warning: return "WARN ";
                case Log::Level::error:   return "ERROR";
            }

            return "?";
        }
    }

    bool Log::start (const juce::File& logsFolder, int filesToKeep)
    {
        stop();

        if (! logsFolder.createDirectory().wasOk())
            return false;

        pruneOldFiles (logsFolder, std::max (1, filesToKeep - 1));

        const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H-%M-%S");
        const auto file = logsFolder.getNonexistentChildFile ("mxj_" + stamp, ".log", false);

        const auto welcome = juce::String (version::name) + " " + version::string
                           + " | " + juce::SystemStats::getOperatingSystemName()
                           + " | " + juce::SystemStats::getCpuModel();

        {
            const juce::ScopedLock sl (loggerLock);
            logger = std::make_unique<juce::FileLogger> (file, welcome, 0);
            juce::Logger::setCurrentLogger (logger.get());
        }

        return file.existsAsFile();
    }

    void Log::stop()
    {
        const juce::ScopedLock sl (loggerLock);

        if (logger != nullptr)
        {
            juce::Logger::setCurrentLogger (nullptr);
            logger.reset();
        }
    }

    void Log::write (Level level, const juce::String& message)
    {
        const auto line = juce::Time::getCurrentTime().formatted ("%H:%M:%S") + " "
                        + levelName (level) + " " + message;

        const juce::ScopedLock sl (loggerLock);

        if (logger != nullptr)
            logger->logMessage (line);
        else
            juce::Logger::outputDebugString (line);
    }

    juce::File Log::getCurrentFile()
    {
        const juce::ScopedLock sl (loggerLock);
        return logger != nullptr ? logger->getLogFile() : juce::File();
    }

    void Log::pruneOldFiles (const juce::File& logsFolder, int filesToKeep)
    {
        auto files = logsFolder.findChildFiles (juce::File::findFiles, false, "mxj_*.log");

        // Names contain a sortable time stamp, so name order is age order.
        std::sort (files.begin(), files.end(),
                   [] (const juce::File& a, const juce::File& b) { return a.getFileName() < b.getFileName(); });

        for (int i = 0; i < files.size() - filesToKeep; ++i)
            files.getReference (i).deleteFile();
    }
}
