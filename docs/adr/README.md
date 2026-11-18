# Architecture decision records

Short notes on decisions that shape the code. One file per decision, numbered, never rewritten:
a changed decision gets a new record that supersedes the old one.

| No. | Decision | Status |
|---|---|---|
| [0001](0001-juce-9.md) | Use JUCE 9.0.5 (not 8) under AGPLv3 | Accepted |
| [0002](0002-fetchcontent.md) | Fetch dependencies with CMake FetchContent instead of CPM | Accepted |
| [0003](0003-juce-builtins.md) | Use JUCE's own JSON, logger and translations in phase 0 | Accepted |
| [0004](0004-command-system.md) | String command ids, bridged to JUCE's command manager | Accepted |
| [0005](0005-audio-model-and-playback.md) | Audio blocks, WAV read in place, peak cache, playback through a disk thread and lock-free FIFO | Accepted |
| [0006](0006-editing-recording-recovery.md) | Edits as new sequences, derived sources, safe saving over open files, recorder and crash journal | Accepted |
| [0007](0007-effects-and-loudness.md) | Effect framework, lock-free live chain, EBU R128 loudness; no new dependency | Accepted |
| [0008](0008-spectral-and-repair.md) | Own FFT and STFT, spectral heal and band effects, spectral-gate noise reduction, AR interpolation for clicks and clipping; no new dependency | Accepted |
| [0009](0009-multitrack-session.md) | Session as a value with snapshot undo, mixer on the disk thread, buses always to the master, aligned session recording, `.mxj` project with a data folder; no new dependency | Accepted |
| [0010](0010-plugins.md) | VST3 through JUCE, own CLAP host (headers), scanner in a separate process, Lua 5.4.8 sandbox with a versioned API, dynamic effect types, local test catalogue with SHA-256 | Accepted |
