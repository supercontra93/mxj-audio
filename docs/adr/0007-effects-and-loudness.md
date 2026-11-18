# 0007 - Effect framework, live chain and loudness measurement

Date: 2026-11-17. Status: accepted.

## Context
Phase 3 brings effects, a rack, presets, meters and LUFS. Effects must work both live (preview, rack
while playing) and offline (applied to a selection as an undoable edit), stay free of locks and
allocation on the audio thread, and be describable without UI code so the dsp layer stays below `app`.

## Decision
- **Effects are data plus a processor (dsp/effects).** An `EffectType` lists its parameters
  (`ParameterInfo`: number, choice, on/off, with a skew around a middle value), factory presets and a
  factory for an `EffectProcessor`. Parameter values live in atomics (`ParameterValues`), so the dialog
  writes them while the audio thread reads them. The app builds every control from this list; there is
  no per-effect UI code. Names are English and translated in the app with `TRANS`.
- **Three kinds:** real-time effects (live and offline), offline effects that measure first (Normalize,
  Match Loudness, Change Speed), and generators (Tone, Noise).
- **Settings are stored by parameter id** (`EffectSettings::toVar`), so adding parameters later keeps old
  presets and racks working. Presets: `presets/<effect id>.json`, rack presets: `presets/racks.json`.
- **Offline rendering** writes new blocks through `SequenceWriter` and swaps them in with
  `edits::replaceRange`: one undo step, markers kept. Look-ahead latency (limiter) is removed by feeding the
  audio that follows the range.
- **Live chain:** the message thread builds and prepares a `ProcessorChain` and hands it to the engine
  through an atomic pointer; the audio thread adopts it only when the "retired" slot is free and the
  message thread deletes retired chains. No locks, no allocation, no deletes on the audio thread.
- **Loudness** follows ITU-R BS.1770-4 / EBU R128 (K-weighting, 400 ms momentary, 3 s short-term,
  absolute and relative gating through a 0.1 LU histogram, loudness range per EBU Tech 3342) and is
  checked against the EBU Tech 3341/3342 test cases. True peak uses 4x polyphase oversampling.
- **Reverb** uses `juce::Reverb` (part of JUCE, already approved in 0001/0003). FFT for the spectrum is a
  small radix-2 implementation in the dsp layer. **No new dependency.**

## Consequences
- Plugins (phase 5) can reuse `EffectProcessor` and the rack.
- Time stretching without pitch change needs a library (for example Rubber Band is GPL, SoundTouch is LGPL):
  a separate decision record when it comes.
