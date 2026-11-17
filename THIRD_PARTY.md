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
| JUCE | 9.0.5 | AGPL-3.0-only (chosen option of a dual licence) | https://github.com/juce-framework/JUCE | Application framework, UI, audio devices, file formats |
| ASIO SDK headers (bundled in JUCE) | as bundled in JUCE 9.0.5 | GPL-3.0-only (chosen option of a dual licence) | Steinberg, via JUCE `JUCE.spdx.json` | ASIO audio driver support on Windows |
| Third-party code bundled inside the JUCE modules we compile (FLAC, Ogg/Vorbis, Opus, zlib, libpng, jpeglib, HarfBuzz, SheenBidi, ...) | as bundled in JUCE 9.0.5 | BSD-3-Clause, Zlib, MIT, IJG, libpng, Apache-2.0 (see `JUCE.spdx.json`) | https://github.com/juce-framework/JUCE/blob/9.0.5/JUCE.spdx.json | Inside JUCE |
| IBM Plex Sans, IBM Plex Mono | 2017-2018 release in IBM/plex master | SIL Open Font License 1.1 | https://github.com/IBM/plex | Interface fonts (`resources/fonts/`) |
| Phosphor Icons (regular) | core main | MIT | https://github.com/phosphor-icons/core | Icons (`resources/icons/`) |

ASIO is a trademark and software of Steinberg Media Technologies GmbH.

## Used only to build or test (not shipped)

| Component | Version | Licence | Source |
|---|---|---|---|
| CMake | 3.22 or newer | BSD-3-Clause | https://cmake.org |
| Catch2 | 3.16.1 | BSL-1.0 | https://github.com/catchorg/Catch2 |
| GitHub Actions (public repository) | - | free for public repositories | https://github.com/features/actions |

## Planned for later phases (licence already verified, not yet in the code)

See `docs/licenses/license-audit.md`.
