// SPDX-License-Identifier: AGPL-3.0-only
#include "plugins/host/PluginScanner.h"

namespace mxj::plugins
{
    PluginScanner::PluginScanner (PluginHost& h, juce::File program)
        : juce::Thread ("MXJ plugin scan"), host (h), scanner (std::move (program))
    {
    }

    PluginScanner::~PluginScanner()
    {
        *alive = false;
        onFinished = nullptr;
        stopThread (timeoutSeconds * 1000 + 5000);
    }

    void PluginScanner::start (bool everything)
    {
        if (isThreadRunning())
            return;

        scanEverything = everything;
        progress = 0.0;
        found = failed = 0;
        startThread();
    }

    void PluginScanner::cancel()
    {
        signalThreadShouldExit();
    }

    juce::String PluginScanner::getCurrentFile() const
    {
        const std::lock_guard<std::mutex> l (lock);
        return current;
    }

    void PluginScanner::run()
    {
        if (scanEverything)
            host.clearList();

        host.removeMissing();
        const auto files = host.findFilesToScan (scanEverything);

        for (size_t i = 0; i < files.size() && ! threadShouldExit(); ++i)
        {
            const auto& [format, file] = files[i];
            {
                const std::lock_guard<std::mutex> l (lock);
                current = file;
            }

            juce::OwnedArray<juce::PluginDescription> results;
            juce::String reason;
            bool ok = false;

            if (scanner.existsAsFile())
            {
                ok = scanInChildProcess (format, file, results, reason);
            }
            else if (auto* f = host.findFormat (format))
            {
                juce::MessageManager::callSync ([&] { scanFile (*f, file, results); });
                ok = true;
            }

            if (threadShouldExit())
                break;

            if (ok)
            {
                host.addScanResult (format, file, results);
                found += results.size();
            }
            else
            {
                host.addBlocked (format, file, reason);
                ++failed;
            }

            progress = (double) (i + 1) / (double) files.size();
        }

        progress = 1.0;
        {
            const std::lock_guard<std::mutex> l (lock);
            current.clear();
        }

        host.save();

        juce::MessageManager::callAsync ([this, token = alive, foundNow = found, failedNow = failed]
        {
            if (! *token)
                return;

            host.registerEffects();
            if (host.onListChanged != nullptr)
                host.onListChanged();

            if (onFinished != nullptr)
                onFinished (foundNow, failedNow);
        });
    }

    bool PluginScanner::scanInChildProcess (const juce::String& format, const juce::String& file,
                                            juce::OwnedArray<juce::PluginDescription>& results, juce::String& reason)
    {
        const auto out = juce::File::createTempFile ("mxj-scan.xml");
        const juce::TemporaryFile cleanup (out);

        juce::ChildProcess child;
        if (! child.start (juce::StringArray { scanner.getFullPathName(), "--format", format, "--file", file,
                                               "--out", out.getFullPathName() }, 0))
        {
            reason = "The scanner program could not be started";
            return false;
        }

        // Wait in short steps so that cancelling is quick.
        const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutSeconds * 1000;
        while (child.isRunning())
        {
            if (threadShouldExit())
            {
                child.kill();
                return false;
            }

            if (juce::Time::getMillisecondCounter() > deadline)
            {
                child.kill();
                reason = "Did not answer within " + juce::String (timeoutSeconds) + " seconds";
                return false;
            }

            child.waitForProcessToFinish (100);
        }

        // The scanner writes its answer only when it finished normally. (Exit codes
        // are not reliable on every system once the process has gone.)
        const auto code = child.getExitCode();
        if (code != 0 || ! readResults (out, results))
        {
            reason = "Crashed while being scanned" + (code != 0 ? " (exit code " + juce::String ((juce::int64) code) + ")" : juce::String());
            return false;
        }

        return true;
    }

    void PluginScanner::scanFile (juce::AudioPluginFormat& format, const juce::String& file,
                                  juce::OwnedArray<juce::PluginDescription>& results)
    {
        format.findAllTypesForFile (results, file);
    }

    bool PluginScanner::writeResults (const juce::File& file, const juce::OwnedArray<juce::PluginDescription>& results)
    {
        juce::XmlElement root ("MXJSCAN");
        for (const auto* d : results)
            root.addChildElement (d->createXml().release());

        return root.writeTo (file);
    }

    bool PluginScanner::readResults (const juce::File& file, juce::OwnedArray<juce::PluginDescription>& results)
    {
        const auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr || ! xml->hasTagName ("MXJSCAN"))
            return false;

        for (auto* e : xml->getChildIterator())
        {
            auto d = std::make_unique<juce::PluginDescription>();
            if (d->loadFromXml (*e))
                results.add (d.release());
        }

        return true;
    }
}
