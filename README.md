<p align="center"><img src="resources/logo-512.png" width="96" alt="MXJ AUDIO logo"></p>

<h1 align="center">MXJ AUDIO</h1>

<p align="center">Free and open source audio editor for Windows · Windows için ücretsiz ve açık kaynak ses editörü</p>

---

**Status: version 0.2 (phase 2, editing and recording).** Opens WAV, AIFF, FLAC, MP3, Ogg Vorbis and Opus,
records from the microphone, edits without touching the original audio (cut, copy, paste, mix paste, trim,
silence, fades, markers, unlimited undo), saves and exports to WAV, AIFF, FLAC, Ogg Vorbis and Opus,
and restores your work after a crash. Effects arrive in 0.3.
See the [roadmap](docs/master-plan.md).

**Durum: Sürüm 0.2 (Faz 2, düzenleme ve kayıt).** WAV, AIFF, FLAC, MP3, Ogg Vorbis ve Opus açar, mikrofondan
kayıt yapar, orijinal sese dokunmadan düzenler (kes, kopyala, yapıştır, karıştırarak yapıştır, kırp, sessizleştir,
fade, işaretçiler, sınırsız geri alma), WAV, AIFF, FLAC, Ogg Vorbis ve Opus olarak kaydeder ve çökmeden sonra
çalışmanı geri getirir. Efektler 0.3 sürümünde geliyor.

## Goals / Hedefler

Waveform editor, multitrack session and spectral repair in one free program, with VST3/CLAP
plugins, Lua plugins, an in-app plugin page and built-in updates.

## Download / İndir

Until the first release, every successful build on the `main` branch produces a portable zip:
**Actions → CI → latest run → Artifacts → `MXJ-AUDIO-windows-x64`**. Unzip and run `MXJ AUDIO.exe`.

İlk sürüme kadar: **Actions → CI → son çalışma → Artifacts → `MXJ-AUDIO-windows-x64`** zip'ini indir,
bir klasöre aç ve `MXJ AUDIO.exe` dosyasını çalıştır.

## Build / Derleme

Needs Visual Studio 2022 (workload "Desktop development with C++", includes CMake) and Git.
The first configure downloads JUCE and Catch2 (a few minutes).

```
git clone https://github.com/supercontra93/mxj-audio.git
cd mxj-audio
cmake --preset vs2022
cmake --build --preset vs2022-release
ctest --preset vs2022-release
```

The program: `build\vs2022\src\app\MXJAudio_artefacts\Release\MXJ AUDIO.exe`.
In Visual Studio you can also use *File → Open → Folder…* on the repository and pick the
`vs2022` preset.

Developer notes and rules: [CLAUDE.md](CLAUDE.md). Contributing: [CONTRIBUTING.md](CONTRIBUTING.md).

## Licence / Lisans

MXJ AUDIO is free software under the [GNU Affero General Public License v3](LICENSE).
Third-party components and their licences: [THIRD_PARTY.md](THIRD_PARTY.md).
The name "MXJ AUDIO" and the logo are not covered by the code licence.
