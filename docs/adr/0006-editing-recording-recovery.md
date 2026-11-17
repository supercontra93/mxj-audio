# 0006 - Edits as new sequences, derived sources, safe saving, recorder and crash journal

Date: 2026-11-17. Status: accepted.

## Context
Phase 2 adds editing, recording, saving and crash recovery on top of the block model of
[0005](0005-audio-model-and-playback.md). Edits must stay cheap on hours of audio, every edit must be
undoable (CLAUDE.md, rules 5 and 6), the audio thread must stay free of locks and allocation, and a crash
must not lose a recording.

## Decision
- **Edits are pure functions (core/edit).** Each takes an `AudioState` (sequence + markers) and returns a new
  one. Undo stores the whole state before and after: both are short block lists, so a step costs a few
  kilobytes whatever the file length.
- **Derived sources instead of processing.** Silence, fades (gain ramp), mix paste and channel conversion
  are `SampleSource`s that compute their samples on read from their inputs. Nothing is rendered when the
  edit happens; peaks come from the inputs' peaks (exact for silence, bounds for the rest).
- **Markers** move with the audio on erase and insert. WAV export writes them as `cue`/`labl` chunks and
  WAV import reads them back.
- **Saving.** The exporter always writes a temporary file next to the target and moves it into place at the
  end. When the target is a file that blocks still read in place (0005), all readers of it are first moved
  to a copy in the block store (`FileSampleSource::relocateAllReading`), so overwriting never changes the
  audio being edited.
- **Formats** come from JUCE: WAV, AIFF, FLAC, Ogg Vorbis, Opus. MP3 export needs an encoder
  (LAME, LGPL) and gets its own decision record later.
- **Recorder (engine).** The audio callback copies input into a lock-free `AbstractFifo`; a writer thread
  drains it into block files of 10 minutes with live peaks. Inputs are opened only while recording.
- **Sample rate conversion (dsp).** Pasting audio of another rate is converted on a background thread with
  JUCE's windowed sinc interpolator.
- **Crash journal.** While a file is open and changed (and every few seconds while recording), a JSON
  journal of its sequence (`recovery.json`) is written into the run folder. The run folder of a crashed
  run is kept at the next start, and the user is offered to restore it; its block files are adopted.

## Consequences
Every edit is instant and undo is unlimited in practice. Reading a heavily edited file stacks derived
sources, which costs some CPU on read; a later "consolidate" can render them. A crash loses at most the
last seconds of a recording.
