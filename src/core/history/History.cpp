// SPDX-License-Identifier: AGPL-3.0-only
#include "core/history/History.h"

namespace mxj
{
    // No limit on undo steps: the audio model is non-destructive, so steps are cheap.
    History::History() : undoManager (std::numeric_limits<int>::max(), 1) {}

    void History::beginStep (const juce::String& name)  { undoManager.beginNewTransaction (name); }
    bool History::perform (juce::UndoableAction* action) { return undoManager.perform (action); }
    bool History::canUndo() const                        { return undoManager.canUndo(); }
    bool History::canRedo() const                        { return undoManager.canRedo(); }
    bool History::undo()                                 { return undoManager.undo(); }
    bool History::redo()                                 { return undoManager.redo(); }
    juce::String History::getUndoName() const            { return undoManager.getUndoDescription(); }
    juce::String History::getRedoName() const            { return undoManager.getRedoDescription(); }
    void History::clear()                                { undoManager.clearUndoHistory(); }
}
