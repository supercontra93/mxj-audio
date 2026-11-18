// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/audio/SequenceSerializer.h"
#include "core/session/Session.h"

namespace mxj::sessionFile
{
    /** The session part of a .mxj project file (master plan, section 6:
        "Proje.mxj: JSON, carries a schema version; older projects are upgraded
        when opened").

        Only the structure is written here: tracks, clips, mixer settings,
        automation and markers. Clip audio is written through a SequenceWriter,
        whose source table the caller stores next to the session (and whose
        temporary block files the caller copies into the project's data folder).
    */
    inline constexpr int schemaVersion = 1;

    /** Returns the session as JSON, or void if some clip audio cannot be described. */
    juce::var toVar (const Session&, SequenceWriter& audioWriter);

    /** Rebuilds a session written by toVar(). `version` is the schema version
        the file was written with (older ones are upgraded). Returns false with
        an error message (English, for the log) if it cannot be read. */
    bool fromVar (const juce::var& json, int version, SequenceReader& audioReader, Session& out, juce::String& error);
}
