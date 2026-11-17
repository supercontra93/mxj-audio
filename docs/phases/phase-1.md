# Phase 1 - Audio core (version 0.1)

Goal (master plan, section 11): block store, audio sequence, peak cache, opening WAV, FLAC and MP3,
waveform drawing, playback, audio device settings.

Acceptance: **a one hour file opens, zooms smoothly and plays.**
Quality targets (section 10): one hour WAV opens in under 3 s, instantly the second time.

## Tasks

| # | Task | Where | Status |
|---|---|---|---|
| 1.1 | Audio model: sample sources, blocks, sequences | `src/core/audio` | done |
| 1.2 | Block store for decoded audio, cleanup after crashes | `src/core/audio/BlockStore` | done |
| 1.3 | Multi-resolution peak cache, drawable while it builds, saved for WAV/AIFF | `src/core/audio/PeakCache` | done |
| 1.4 | Open WAV, AIFF (in place), FLAC, MP3, Ogg Vorbis, Opus (decoded) on a background thread | `src/formats/import` | done |
| 1.5 | Playback engine: disk thread, lock-free FIFO, sample rate conversion, test tone | `src/engine/playback` | done |
| 1.6 | Audio devices: WASAPI (shared, exclusive, low latency) and ASIO, saved in the settings | `src/engine/devices` | done |
| 1.7 | Editor page: transport bar, time ruler, waveform per channel, zoom to single samples, scroll bar | `src/app/editor` | done |
| 1.8 | Commands: open, close, play/stop, go to start/end, zoom in/out/full, audio device settings | `src/app/AppCommands.cpp` | done |
| 1.9 | Audio device settings window | `src/app/settings` | done |
| 1.10 | Open by drag and drop and from the command line; smoke test opens a generated WAV | `src/app`, CI | done |
| 1.11 | Tests for model, peaks, import (WAV, FLAC, MP3, errors, cancel) and playback | `tests/` | done |

Decision record: [0005](../adr/0005-audio-model-and-playback.md). Moved to later: M4A/AAC/WMA through
Windows decoders, recent files, more than two playback channels (`docs/later.md`).

## Shortcuts

| Key | Command |
|---|---|
| Ctrl+O | Open |
| Ctrl+W | Close |
| Space | Play / Stop |
| Home / End | Go to start / end |
| `=` / `-` | Zoom in / out |
| `\` | Zoom to whole file |
| Ctrl + mouse wheel | Zoom around the mouse |
| Mouse wheel | Scroll |

## Manual test list for Baybars (Türkçe)

CI'nin ürettiği zip'i indirip şunları dene:

1. Programı aç. Başlangıç sayfasının sağında "Ses aygıtı" altında hoparlörünün adı, örnekleme hızı ve
   tampon boyutu yazmalı.
2. "Ses Aygıtı Ayarları..." düğmesine bas. "Deneme Sesi Çal" ile hafif bir bip duymalısın. Sürücü türünü
   (Windows Audio, Exclusive, ASIO varsa ASIO) ve tampon boyutunu değiştirip tekrar dene, pencereyi kapat.
3. Dosya > Aç (Ctrl+O) ile bir WAV dosyası aç. Dalga formu görünmeli, üstte dosya adı, hız, bit ve süre yazmalı.
4. Boşluk tuşuna bas: ses çalmalı, mavi oynatma çizgisi ilerlemeli, saat sayacı akmalı. Tekrar boşluk: durur
   ve çizgi başladığı yere döner.
5. Dalga formunda bir yere tıkla, boşluk ile oradan çal. Çalarken başka bir yere tıkla: oradan devam etmeli.
6. Ctrl + fare tekerleği ile yakınlaş: en yakında tek tek örnek noktaları görünmeli. Tekerlek ile sağa sola kay,
   `\` tuşu ile tüm dosyaya dön.
7. Bir MP3 ve bir FLAC dosyası aç (dosyayı pencereye sürükleyip bırakmak da olur). Uzun dosyada açılırken
   dalga formu soldan sağa dolar, yüzde yazar.
8. Mümkünse 1 saatlik bir WAV aç: birkaç saniyeden kısa sürede açılmalı. Kapatıp yeniden açınca anında açılmalı.
   Baştan sona birkaç yere tıklayıp çal, takılma veya cızırtı olmamalı.
9. Dosya > Kapat (Ctrl+W): başlangıç sayfasına döner. Programı kapatıp açınca seçtiğin ses aygıtı hatırlanmalı.
