# Third-party software

MXJ AUDIO is licensed under the GNU Affero General Public License v3 (see `LICENSE`).
It uses the components below. Every licence here allows free use and free
redistribution, and is compatible with AGPLv3. Nothing here costs money.

Licences were checked against each project's official source on 2026-11-17
(details: `docs/licenses/license-audit.md`). Adding a component requires a
decision record in `docs/adr/` and a new row here.

## Shipped with the program

| Component | Version | Licence | Source | Used for |
|---|---|---|---|---|
| JUCE | 9.0.5 | AGPL-3.0-only (chosen option of a dual licence) | https://github.com/juce-framework/JUCE | Application framework, UI, audio devices, file formats (WAV, AIFF and MP3 decoding are JUCE's own code) |
| ASIO SDK headers (bundled in JUCE) | as bundled in JUCE 9.0.5 | GPL-3.0-only (chosen option of a dual licence) | Steinberg, via JUCE `JUCE.spdx.json` | ASIO audio driver support on Windows |
| Third-party code bundled inside the JUCE modules we compile (FLAC, Ogg/Vorbis and Opus decoders used to open files since 0.1, zlib, libpng, jpeglib, HarfBuzz, SheenBidi, ...) | as bundled in JUCE 9.0.5 | BSD-3-Clause, Zlib, MIT, IJG, libpng, Apache-2.0 (see `JUCE.spdx.json`) | https://github.com/juce-framework/JUCE/blob/9.0.5/JUCE.spdx.json | Inside JUCE |
| IBM Plex Sans, IBM Plex Mono | 2017-2018 release in IBM/plex master | SIL Open Font License 1.1 | https://github.com/IBM/plex | Interface fonts (`resources/fonts/`) |
| Lua | 5.4.8 | MIT | https://github.com/lua/lua (official mirror of lua.org), tag v5.4.8 | MXJ plugins (Lua scripts), built as C++ in a sandbox; licence text in `licenses/LICENSE-Lua.txt` |
| CLAP headers | 1.2.7 | MIT | https://github.com/free-audio/clap | Hosting CLAP plugins (header only); `licenses/LICENSE-CLAP.txt` |
| VST 3 SDK (bundled in JUCE) | as bundled in JUCE 9.0.5 | MIT | https://github.com/steinbergmedia/vst3sdk | Hosting VST3 plugins through JUCE; `licenses/LICENSE-VST3-SDK.txt` |
| Phosphor Icons (regular) | core main | MIT | https://github.com/phosphor-icons/core | Icons (`resources/icons/`), incl. transport and zoom icons added in 0.1 and editing, recording and saving icons added in 0.2 |

ASIO is a trademark and software of Steinberg Media Technologies GmbH. VST is a trademark of Steinberg Media
Technologies GmbH. CLAP is an open standard by the free-audio project.

## Used only to build or test (not shipped)

| Component | Version | Licence | Source |
|---|---|---|---|
| CMake | 3.22 or newer | BSD-3-Clause | https://cmake.org |
| Catch2 | 3.16.1 | BSL-1.0 | https://github.com/catchorg/Catch2 |
| FFmpeg (developer tool, used once) | - | LGPL/GPL | https://ffmpeg.org | Made the 1 second test file `tests/data/sine440-stereo-1s.mp3`; not used by the build |
| GitHub Actions (public repository) | - | free for public repositories | https://github.com/features/actions |

## Planned for later phases (licence already verified, not yet in the code)

See `docs/licenses/license-audit.md`.
