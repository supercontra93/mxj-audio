# CLAUDE.md - rules for working on MXJ AUDIO

Read this before every coding session. The master plan is the source of truth:
https://claude.ai/artifact/5VvHgoyL6qNP886JUJ27b4 (summary and links in `docs/master-plan.md`).

## What this is

MXJ AUDIO is a free, open source (AGPLv3) desktop audio editor for Windows 10/11 x64,
in the class of Adobe Audition and Audacity. C++20 + JUCE 9. macOS comes only after v1.0
from the same code, so keep everything portable.

## Hard rules

1. **No money, ever.** Every dependency, tool, service and asset must be free and allow free
   open source distribution. No paid licences, certificates or services.
2. **New dependency = decision record.** Add `docs/adr/NNNN-*.md`, verify the licence from the
   official source, add a row to `THIRD_PARTY.md`. Never use `juce_analytics` or `juce_product_unlocking`.
3. **Layers.** A layer may only include headers from itself or the layers below it:

   ```
   app        windows, panels, drawing, theme              (top)
   services   settings, logging, paths, i18n, updates, plugin catalogue
   plugins    VST3/CLAP hosting, scanner, Lua runner, plugin API
   engine     real-time playback, recording, tracks, mixer, devices
   formats    audio file reading/writing, metadata
   dsp        effects and analysis
   core       audio data model, project model, commands, undo   (bottom)
   ```

   Only `app` may use JUCE UI modules (`juce_gui_*`, `juce_graphics`). `tools/check_layers.py`
   enforces both rules in CI.
4. **Real-time rules (audio thread):** no memory allocation, no locks, no file access, no logging.
   Talk to the UI through lock-free queues.
5. **Every user action is a command** in `CommandRegistry` (`src/core/commands`), with a stable
   string id like `file.open`. Menus, shortcuts, command palette, macros and Lua plugins all call
   commands; never duplicate behaviour in a button handler.
6. **Non-destructive editing.** Audio lives in immutable blocks; edits change sequence lists.
   Every edit records an undo step in `History`.
7. **Text:** English source strings wrapped in `TRANS()`. Add the Turkish line to
   `resources/i18n/tr.txt`; the test `Every TRANS string in the code has a Turkish translation` fails otherwise.
8. **Theme:** colours only from `src/app/theme/Palette.h`, fonts only from `Fonts.h`
   (IBM Plex), icons from Phosphor (`resources/icons`). Direction "A Grafit".
9. **Updates stay off** in development builds (`MXJ_ENABLE_UPDATES=OFF`). Nothing may contact a
   server unless the user asked. No telemetry.
10. **Settings** keys live in `SettingKeys.h`. Never rename a key without a migration in `Settings::migrate`.

## Build and test

```
cmake -S . -B build -DMXJ_BUILD_TESTS=ON          # Windows: add -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
python3 tools/check_layers.py
```

The program is `build/src/app/MXJAudio_artefacts/<Config>/MXJ AUDIO(.exe)`.
Options: `--data-dir <folder>` (settings and logs go there instead of `%APPDATA%\MXJ AUDIO`),
`--smoke-test` (start, open the window, quit after 2 s with exit code 0; with a file argument it waits
until the file has loaded instead), `<file>` (open this audio file).

On Linux (portability check only) install the packages listed in `.github/workflows/ci.yml`.

## How work is done

- One phase at a time, from `docs/phases/`. Do not grow scope mid-phase; write new ideas in
  `docs/later.md`.
- Small tasks: one goal, its own tests, one pull request. "Done" = builds, tests pass, the
  acceptance check is shown, docs are updated.
- Each phase ends with a working build and a manual test list for Baybars (in the phase file).
- Important architecture decisions go in `docs/adr/`.
- Commits are signed off (`git commit -s`, DCO).
