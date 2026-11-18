# 0010 - Plugins: VST3, CLAP, Lua and the catalogue

Date: 2026-11-18. Status: accepted.

## Context
Phase 6 (master plan, sections 6 and 7) adds plugins: VST3 and CLAP hosting with a scanner in a separate
process, "MXJ plugins" written in Lua with a versioned API, the in-app plugin page and a catalogue.
Acceptance: common free VST3 plugins work, and an example Lua plugin is installed from the page. Nothing may
cost money (rule 1), nothing may contact a server unless the user asked (rule 9), and the audio thread rules
(rule 4) must hold with plugins we did not write.

## Decision
- **New dependencies** (all MIT, checked in `docs/licenses/license-audit.md`, rows in `THIRD_PARTY.md`):
  - **Lua 5.4.8**, fetched from the official mirror `github.com/lua/lua` (tag v5.4.8) and built by us into
    `mxj_lua`. It is compiled as C++, so a Lua error unwinds C++ objects correctly. No binding library.
  - **CLAP 1.2.7 headers** (`free-audio/clap`), header only. JUCE 9 hosts VST3 but not CLAP, so
    `plugins/clap/ClapFormat` is our own small host that wraps a CLAP plugin as a `juce::AudioPluginInstance`.
    Everything above it treats VST3 and CLAP the same way.
  - **VST3 SDK** as bundled in JUCE 9.0.5 (MIT since SDK 3.8). JUCE's hosting is switched on
    (`JUCE_PLUGINHOST_VST3=1`); VST2, AU, LV2, LADSPA and ARA stay off.
- **Layering.** The `plugins` layer uses only `juce_audio_processors_headless` (no UI). The plugin windows are
  made in `app`: VST3 through JUCE's editor, CLAP through `ClapPluginInstance::Gui`, which hands the plugin the
  native window handle. `services` and `app` see plugins only through `dsp::EffectType`, so a plugin effect is
  used wherever a built-in effect is (Effects menu, effect rack, track effects, presets of the rack).
- **Effect types are dynamic.** `dsp::catalogue` keeps the built-in effects and adds plugin types at run time
  (`vst3:...`, `clap:...`, `lua:...`). A removed type is only hidden, so pointers held by open windows stay valid.
- **Hosted plugin state.** A VST3 or CLAP effect keeps its whole state (`EffectState`, saved as base64) next
  to its parameter values. Values made with the same instance name share one live plugin, so the rack of a
  track and its mixer strip play and show the same plugin; applying an effect and the mixdown use their own
  instances. Loading, preparing, saving and restoring run on the message thread. `process()` never waits:
  if the plugin is busy (being prepared, or already running on another thread) the block passes unchanged.
  Blocks are cut to at most 4096 frames, and mono or stereo audio is adapted to the plugin's channels.
- **Scanning in a separate process.** `mxj-plugin-scanner(.exe)`, a small program next to MXJ AUDIO, opens one
  plugin file at a time and writes what it found to an XML file. A file whose scan crashes, hangs (60 s) or
  fails goes on the block list and never stops the program. The list of plugins, the scanned files with their
  times, the block list and the user's folders are kept in `<data>/plugins/known-plugins.xml`, so the program
  starts without scanning. Scanning runs only when the user asks (plugin page or Plugins menu).
  Plugins are run in the program's own process; running them in a separate process is moved to after v1.0.
- **MXJ plugins (Lua).** A plugin is a folder (or a `.mxjplugin` zip) with `plugin.json` and `main.lua`.
  Four types: effect, generator, analysis, tool. Parameters are declared in `plugin.json` and MXJ AUDIO draws
  their controls with the same panel as built-in effects. The API carries a version (`apiVersion`, now 1);
  a plugin asking for a newer API or MXJ AUDIO version is refused with a clear message.
  Lua effects are applied offline, chunk by chunk on a worker thread (`Kind::script`): no preview and not in
  the real-time rack, because Lua's garbage collector cannot follow the audio thread rules.
- **Sandbox.** Only `base`, `table`, `string`, `math`, `utf8`, `coroutine` and a few `os` time functions are
  opened. No `io`, `package`, `require`, `load`, `dofile`, `loadfile`, `debug` or `string.dump`. Memory is
  limited (512 MB) by our allocator and time by an instruction hook (30 s per effect call, 10 s for a tool),
  and a running plugin stops when the job is cancelled. Program commands are available only with the
  `commands` permission, which the user is shown before installing; plugins cannot run `plugins.*` commands
  or quit the program.
- **Catalogue.** An `index.json` with one record per plugin and the package's SHA-256 and size. A package is
  installed only if its size and checksum match. Until the community catalogue repository opens (phase 8),
  the program reads a **local test catalogue** that the build makes from `resources/lua-plugins/` and ships in
  the `catalog` folder; web addresses in the catalogue are refused, so nothing is downloaded.

## Consequences
- A plugin that crashes while it runs (not while it is scanned) still takes the program down; the recovery
  journal of phase 2 keeps the user's work. Out-of-process running is in `docs/later.md`.
- A plugin effect whose plugin is missing on the next start is dropped from the rack.
- Plugin latency is reported but not compensated in the session yet (already in `docs/later.md`).
- Instruments are listed but not used before MIDI arrives (after v1.0).
