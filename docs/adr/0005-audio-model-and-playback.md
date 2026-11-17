# 0005 - Audio blocks, WAV read in place, peak cache and the playback path

Date: 2026-11-17. Status: accepted.

## Context
Phase 1 must open a one hour file in under 3 seconds (instantly the second time), zoom smoothly
and play without dropouts, while keeping the non-destructive model of the master plan (section 6)
and the real-time rules of `CLAUDE.md`. One hour of stereo audio at 48 kHz is 1.4 GB as 32-bit float.

## Decision
- **Model (core).** A `SampleSource` is immutable audio. An `AudioBlock` is a range inside a source.
  An `AudioSequence` is an ordered list of blocks (1 Mi frames each by default). Phase 2 edits
  rearrange this list; nothing ever writes into a source.
- **Block store (core).** Decoded and processed audio is written as raw interleaved 32-bit float files
  in `cache/blocks/run-*`. A source deletes its file when its last user goes away; each run folder is
  guarded by an inter-process lock, and folders of crashed runs are deleted at the next start.
- **WAV and AIFF are read in place (formats).** Uncompressed files are already random access, so their
  blocks point straight into the original file (`FileSampleSource`) instead of being copied. Opening
  only computes the peaks. **Rule for phase 2:** before a save overwrites a file that blocks still point
  into, those blocks must first be copied into the block store.
- **Compressed files (FLAC, MP3, Ogg, Opus)** are decoded once into the block store, so later reads are
  exact and fast, and MP3 seeking never matters.
- **Peak cache (core).** Min/max pairs per 128 frames, plus coarser levels (each 8x). About 25 MB for an
  hour of stereo audio. It is built while the file loads and can be drawn while it grows. For WAV/AIFF it
  is saved in `cache/peaks/`, keyed by path, size and modification time: the second open is instant.
- **Playback (engine).** A disk thread reads the sequence ahead, converts the sample rate with JUCE's
  windowed sinc interpolator when file and device differ, and writes stereo into a lock-free
  `AbstractFifo` (64 Ki frames). The audio callback only copies from the FIFO: no allocation, lock or
  file access. Start and stop use atomics; a stop waits (on the message thread) until a running
  callback has returned, so the FIFO can be reset safely.
- No new dependency: everything above uses JUCE and the C++ standard library.

## Consequences
Measured on the Linux CI machine: a one hour 16-bit stereo WAV opens in 0.5 s, and in under 10 ms
the second time; a 10 minute MP3 decodes in 1.5 s. Temporary disk use for compressed files is
4 bytes per sample per channel. Files with more than two channels play their first two channels
for now (`docs/later.md`). r8brain (master plan) can replace the interpolator later without touching
the callback.
