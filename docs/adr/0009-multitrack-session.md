# 0009 - Multitrack session

Date: 2026-11-18. Status: accepted.

## Context
Phase 5 adds the multitrack session: tracks, clips, the mixer, automation, buses, the mixdown and a project
file. Acceptance: a 10 track session with speech, music and a jingle is completed. The real-time rules
(CLAUDE.md, rule 4) and the non-destructive model (rule 6) must hold, and nothing new may cost money.

## Decision
- **No new dependency.** Everything is built on JUCE 9 and the audio model of phase 1. No new rows in
  `THIRD_PARTY.md`; the three new icons (magnet, faders, music-notes-plus) come from Phosphor, which is
  already covered.
- **The session is a value** (`core/session/Session`): tracks (audio, bus), the master, clips that point into
  immutable `AudioSequence`s (block lists, never audio copies), markers and automation lanes. Edits are pure
  functions in `sessionEdits` and are unit tested. An undo step stores the session before and after; that is
  cheap because clips share their blocks. Drags (moving a clip, a fade, a fader, an automation point) change the
  session while the mouse moves and become one undo step when it is released (`beginGesture` / `endGesture`).
  The session has its own undo history, separate from the waveform editor's.
- **The mixer runs on the playback engine's disk thread, not the audio thread.** `SessionMixer` is a
  `PlaybackSource`: it renders 1024 frame chunks ahead (8192 frames of look-ahead, about 0.17 s) into the same
  lock-free FIFO the waveform editor uses. So the mixer may read files, run effects and rebuild its graph without
  ever touching the audio thread. Faders, pan and send levels move through atomics and are heard within the
  look-ahead; a structural change (new track, new effect) builds a new graph that the disk thread adopts at
  the next chunk, keeping the state of effect chains that did not change. Level meters per strip carry the
  session frame they belong to, so the UI shows the level of what is heard now, not of what is being rendered.
- **Mixing rules:** clip gain and fades (equal power sine curve; overlapping clips on one track cross-fade
  automatically), track effects, volume (fader or automation, evaluated per chunk and ramped), pan (balance
  law; mono clips play on both sides first), mute and solo (solo on a track keeps the buses it feeds audible),
  post-fader sends, then buses (each with effects, volume and pan) into the master and its effects.
  Buses always output to the master, so the graph never has cycles.
- **Recording in the session** records onto the armed track (else the selected one) at the cursor while the
  session plays. The recorder counts captured frames; the first callback of the session playback stores that
  count, and the take is shifted by it plus the input and output latency of the device, so the take lines up
  with what was heard.
- **Project file** `Name.mxj`: JSON with `format`, `version` (schema 1), `app`, the session and its sources.
  Audio recorded or created in MXJ AUDIO is copied once into the folder `Name_data/` (raw float plus its
  peak file, so it opens instantly); WAV and AIFF files are referenced where they are (absolute and relative
  path). Files that are missing when the project opens play as silence and are listed to the user. A newer
  schema version is refused with a clear message. Saving writes a temporary file and renames it.
- **Mixdown** renders the session (or the time selection) on a background job into new blocks, then the
  normal exporter writes WAV, FLAC, AIFF, Ogg or Opus; "Mixdown to New File" opens it in the waveform editor.

## Consequences
- Live control has about 0.17 s latency, fine for mixing, not for playing an instrument through effects.
  Plugin delay compensation, freezing and clip effects are listed in `docs/later.md`.
- The project folder can hold audio no clip uses any more after editing; a "consolidate" command is in later.md.
