// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/audio/AudioSequence.h"

#include <map>

namespace mxj
{
    /** Turns sequences into JSON-ready values and back, for the crash recovery
        journal (docs/adr/0006). The audio itself is not stored: only where it
        lives (files, block files) and how the edits combine it.

        Sources are written once into a shared table, so one source used by
        many blocks, or by several sequences, is restored as one object. That
        matters: a BlockFileSource deletes its file when it goes away.
    */
    class SequenceWriter
    {
    public:
        /** Returns the sequence as a value that refers to the source table.
            Returns void if a source cannot be described. */
        juce::var write (const AudioSequence&);

        /** The source table, to be stored next to the written sequences. */
        juce::var getSources() const { return sources; }

    private:
        int addSource (const SampleSourcePtr&);

        juce::var sources { juce::Array<juce::var>() };
        std::map<const SampleSource*, int> indices;
        bool failed = false;
    };

    class SequenceReader
    {
    public:
        /** Creates a leaf source (file, block file) from its description, or
            returns nullptr and sets the error. Must return the same object for
            the same description if it is asked twice. */
        using LeafFactory = std::function<SampleSourcePtr (const juce::var& description, juce::String& error)>;

        /** Rebuilds the source table. Check getError() afterwards. */
        SequenceReader (const juce::var& sources, const LeafFactory&);

        /** Rebuilds a sequence written by SequenceWriter::write(). */
        AudioSequence read (const juce::var& sequence);

        bool failed() const { return error.isNotEmpty(); }
        juce::String getError() const { return error; }

    private:
        std::vector<SampleSourcePtr> table;
        juce::String error;
    };
}
