// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace mxj
{
    /** Undo history of one document.

        Thin wrapper over juce::UndoManager so the rest of the program depends on
        our interface, not JUCE's. Every editing command records an UndoableAction
        here. Message thread only.
    */
    class History
    {
    public:
        History();

        /** Starts a new undo step with a name shown in the History panel. */
        void beginStep (const juce::String& name);

        /** Performs the action and records it in the current step.
            Takes ownership. Returns false if the action failed. */
        bool perform (juce::UndoableAction* action);

        bool canUndo() const;
        bool canRedo() const;
        bool undo();
        bool redo();

        juce::String getUndoName() const;
        juce::String getRedoName() const;

        void clear();

        /** For JUCE components such as ValueTree that take an UndoManager. */
        juce::UndoManager& getUndoManager() noexcept { return undoManager; }

    private:
        juce::UndoManager undoManager;

        JUCE_DECLARE_NON_COPYABLE (History)
    };
}
