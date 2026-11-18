<p align="center"><img src="resources/logo-512.png" width="96" alt="MXJ AUDIO logo"></p>

<h1 align="center">MXJ AUDIO</h1>

<p align="center">Free and open source audio editor for Windows · Windows için ücretsiz ve açık kaynak ses editörü</p>

---

**Status: version 0.5 (phase 5, multitrack session).** Opens WAV, AIFF, FLAC, MP3, Ogg Vorbis and Opus,
creates new empty files, records from the microphone, edits without touching the original audio (cut, copy,
paste, trim, fades, markers, unlimited undo), has 24 effects with live preview and an effect rack, measures
loudness (LUFS), shows a spectrogram where you can select and heal frequencies, and removes background noise,
hum, clicks and clipping. Multitrack sessions: tracks and buses with clips, fades, a mixer, volume and pan
automation, track effects, recording onto a track, mixdown and `.mxj` project files. Saves and exports to WAV, AIFF, FLAC, Ogg Vorbis and Opus, and restores your work
after a crash. See the [roadmap](docs/master-plan.md).

**Durum: Sürüm 0.5 (Faz 5, çok kanallı oturum).** WAV, AIFF, FLAC, MP3, Ogg Vorbis ve Opus açar, yeni boş
dosya oluşturur, mikrofondan kayıt yapar, orijinal sese dokunmadan düzenler, canlı dinlemeli 24 efekti ve efekt rafı
vardır, ses yüksekliğini (LUFS) ölçer, frekansları seçip iyileştirebileceğin bir spektrogram gösterir; dip gürültüsü,
uğultu, tık ve kırpılmayı temizler. Çok kanallı oturum: klipli kanallar ve buslar, geçişler, mikser, ses düzeyi ve pan
otomasyonu, kanal efektleri, kanala kayıt, mixdown ve `.mxj` proje dosyaları. WAV, AIFF, FLAC, Ogg Vorbis ve Opus olarak kaydeder ve çökmeden sonra
çalışmanı geri getirir.

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
