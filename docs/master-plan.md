# Master plan

The full master plan (product requirements and technical design, in Turkish) is a living
document: https://claude.ai/artifact/5VvHgoyL6qNP886JUJ27b4

Short summary, so the repository can be understood without it:

- **Product:** free, open source (AGPLv3) audio editor for Windows 10/11 x64. Waveform editor,
  multitrack session and spectral repair in one program. macOS and Linux after v1.0.
- **Stack:** C++20, JUCE 9, CMake. Built and tested on GitHub Actions.
- **Architecture:** seven layers (see `CLAUDE.md`), non-destructive audio model, one command
  system, one observable project tree, background caches, platform code behind `platform/`.
- **Plugins:** VST3 and CLAP hosting, Lua 5.4 "MXJ plugins", in-app plugin page (phase 6). The
  community catalogue becomes a GitHub repository with the release; phase 6 ships a local test catalogue.
- **Updates:** GitHub Releases plus Ed25519-signed manifest, built in phase 8, off until release.
- **Project file:** `.mxj` (JSON) plus a data folder.
- **Roadmap:** phase 0 foundation, 1 audio core (0.1), 2 editing and recording (0.2),
  3 effects and metering (0.3), 4 spectral and repair (0.4), 5 multitrack (0.5),
  6 plugins (0.6), 7 productivity (0.7), 8 release infrastructure (0.9 beta), 9 beta and v1.0.
- **Rule:** nothing costs money, in development or at release.

Phase details: `docs/phases/`. Decisions: `docs/adr/`.
