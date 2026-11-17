// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_events/juce_events.h>
#include <atomic>
#include <functional>

namespace mxj
{
    /** Long work (saving, converting a recording, restoring after a crash) on
        its own thread, with progress, so the window never freezes.

        `work` runs on the job's thread and returns true on success; `done` runs
        afterwards on the message thread (not if the job is deleted first, which
        cancels the work). Delete the job to cancel it.
    */
    class BackgroundJob : private juce::Thread
    {
    public:
        using Work = std::function<bool (BackgroundJob&)>;
        using Done = std::function<void (bool succeeded)>;

        BackgroundJob (const juce::String& title, Work work, Done done)
            : Thread ("MXJ " + title), name (title), workFunction (std::move (work)), doneFunction (std::move (done))
        {
            startThread (Priority::normal);
        }

        ~BackgroundJob() override
        {
            signalThreadShouldExit();
            stopThread (60000);
        }

        /** For the work function: false once the job should stop. */
        bool shouldContinue() const { return ! threadShouldExit(); }

        /** 0 to 1, set by the work function, read by the UI. */
        std::atomic<double> progress { 0.0 };

        /** Set by the work function on failure (translated). */
        juce::String error;

        juce::String getName() const { return name; }
        bool isFinished() const      { return finished.load(); }

    private:
        void run() override
        {
            const bool ok = workFunction (*this);
            finished = true;

            if (threadShouldExit())
                return;

            juce::MessageManager::callAsync ([weak = juce::WeakReference<BackgroundJob> (this), ok]
            {
                if (weak == nullptr || weak->doneFunction == nullptr)
                    return;

                auto done = weak->doneFunction; // the call may delete the job
                done (ok);
            });
        }

        juce::String name;
        Work workFunction;
        Done doneFunction;
        std::atomic<bool> finished { false };

        JUCE_DECLARE_WEAK_REFERENCEABLE (BackgroundJob)
        JUCE_DECLARE_NON_COPYABLE (BackgroundJob)
    };
}
