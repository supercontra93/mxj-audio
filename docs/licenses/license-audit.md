# Licence audit, phase 0 (2026-11-17)

Master plan rule: nothing may cost money, in development or at release, and every
component must allow free open source distribution. The master plan's library
table was written from memory, so phase 0 checks each licence against the
project's own published licence file.

**Result: every planned component is free and compatible with AGPLv3. Nothing had to be replaced.**
Two corrections to the plan: JUCE is now at major version 9 (the plan said 8), and
JUCE 9 already bundles the ASIO SDK headers and Opus support.

## How it was checked

Each licence file was downloaded from the project's official repository or site on
2026-11-17 (`raw.githubusercontent.com/<owner>/<repo>/<default branch>/<file>`)
and read. Versions are the latest tags at that date (`git ls-remote --tags`).

| Component | Plan said | Verified licence | Evidence (official file) | AGPLv3 compatible | Cost | Phase |
|---|---|---|---|---|---|---|
| JUCE | 8, AGPLv3 | **9.0.5**, dual: AGPL-3.0-only or paid JUCE 9 licence. We use AGPLv3: free. | `juce-framework/JUCE/LICENSE.md`, `JUCE.spdx.json` | yes (same licence) | free | 0 |
| ASIO SDK | GPLv3 option | dual: GPL-3.0-only or Steinberg licence. Headers are bundled in JUCE 9 (`JUCE_ASIO=1`). | `JUCE.spdx.json` entry "ASIO SDK: LicenseRef-Steinberg-ASIO OR GPL-3.0-only" | yes (AGPLv3 section 13 allows combining with GPLv3) | free | 0 |
| VST3 SDK | MIT | MIT (Steinberg, 2026) | `steinbergmedia/vst3sdk/LICENSE.txt` | yes | free | 6 |
| CLAP | MIT | MIT | `free-audio/clap/LICENSE` | yes | free | 6 |
| Opus (libopus) | BSD | BSD-3-Clause; also bundled in JUCE 9 | `xiph/opus/COPYING` | yes | free | 1 |
| LAME (MP3 encoder) | LGPL | LGPL-2.0 or later ("GNU Library GPL v2") | `lameproject/lame/COPYING` (GitHub mirror; official home is SourceForge, re-check when added) | yes | free | 2 |
| libebur128 | MIT | MIT | `jiixyj/libebur128/COPYING` | yes | free | 3 |
| Signalsmith Stretch | MIT | MIT | `Signalsmith-Audio/signalsmith-stretch/LICENSE.txt` | yes | free | 3 |
| r8brain-free-src | MIT | MIT | `avaneev/r8brain-free-src/LICENSE` | yes | free | 1 |
| PFFFT | BSD | BSD-style (FFTPACK/UCAR licence, 3 conditions) | `marton78/pffft/LICENSE.txt` | yes | free | 4 |
| RNNoise | BSD | BSD-3-Clause | `xiph/rnnoise/COPYING` | yes | free | 4 |
| Lua 5.4 | MIT | MIT | licence text in `lua/lua/lua.h` (lua.org page blocked from the audit machine) | yes | free | 6 |
| sol2 | MIT | MIT | `ThePhD/sol2/LICENSE.txt` | yes | free | 6 |
| nlohmann/json | MIT | MIT. Not needed in phase 0: JUCE's JSON is used (ADR 0003). | `nlohmann/json/LICENSE.MIT` | yes | free | maybe 5 |
| Monocypher | BSD | dual: BSD-2-Clause or CC0-1.0 | `LoupVaillant/Monocypher/LICENCE.md` | yes | free | 8 |
| Catch2 | BSL | BSL-1.0, tests only | `catchorg/Catch2/LICENSE.txt` | yes | free | 0 |
| pluginval | GPLv3 | GPL-3.0, separate test tool, not shipped | `Tracktion/pluginval/LICENSE` | n/a (run, not linked) | free | 6 |
| Inno Setup | free | Inno Setup Licence: free of charge, commercial use allowed | `jrsoftware/issrc/license.txt` | n/a (build tool) | free | 8 |
| CPM.cmake | MIT | MIT. Not used: CMake's built-in FetchContent does the job (ADR 0002). | `cpm-cmake/CPM.cmake/LICENSE` | yes | free | - |
| IBM Plex fonts | OFL | SIL OFL 1.1 | `IBM/plex/LICENSE.txt` | yes (fonts may be embedded) | free | 0 |
| Phosphor Icons | MIT | MIT | `phosphor-icons/core/LICENSE` | yes | free | 0 |
| GitHub Actions | free for public | free for public repositories | GitHub pricing | n/a | free | 0 |

## Things to keep in mind

- **JUCE modules we must not use:** `juce_analytics` (sends usage data, against the privacy rule) and
  `juce_product_unlocking` (paid unlocking, against the no-payment spirit). Neither is linked.
- **LAME:** LGPL. Linking it into an AGPLv3 program is allowed. Re-check the official SourceForge
  copy when it is added in phase 2.
- **FFmpeg (optional, user-installed):** not shipped by us; if added later it is loaded only from a copy the user installed.
- **Windows codecs (M4A/AAC/WMA):** part of Windows, no licence fee for us.
- **Code signing (phase 8):** only free programmes for open source projects; otherwise unsigned (master plan section 8).
