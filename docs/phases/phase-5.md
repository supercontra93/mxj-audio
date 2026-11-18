# Phase 5 - Multitrack session (version 0.5)

Goal (master plan, section 11): tracks, clips, mixer, automation, buses, mixdown, project file.

Acceptance: **a 10 track session with speech, music and a jingle is completed.**

## Tasks

| # | Task | Where | Status |
|---|---|---|---|
| 5.1 | Session model: audio tracks, buses, master, clips, markers, automation lanes; edits as pure functions (add, move, split, trim, fades, clip gain, delete, ripple delete, snap) | `src/core/session` | done |
| 5.2 | Mixer on the disk thread: clip gain and fades, automatic crossfades, track effects, volume, pan, mute, solo, sends, buses, master effects, level per strip | `src/engine/mix/SessionMixer` | done |
| 5.3 | Playback through a `PlaybackSource` (waveform or session), look-ahead, resampling to the device rate | `src/engine/playback` | done |
| 5.4 | Recording onto the armed track, aligned with what was heard (capture counter plus device latency) | `Recorder`, `AudioEngine`, `SessionWorkspace` | done |
| 5.5 | Project file `.mxj` plus `Name_data/` folder: save, save as, open, missing files play silent and are listed | `src/core/session/SessionFile`, `src/app/session/ProjectFile` | done |
| 5.6 | Time line: ruler, track headers (name, M, S, R, A, volume, pan, level), clips with waveform, fades, trimming, moving between tracks, snapping, automation lanes, file drop | `src/app/session/TimelineView` | done |
| 5.7 | Mixer panel (strip per track and bus plus master: output, send, FX, pan, mute, solo, fader, meter) and track effects panel | `MixerPanel`, `TrackEffectsPanel` | done |
| 5.8 | Commands and menus: File > New Multitrack Session, Open (.mxj), Export Multitrack Mixdown; Multitrack menu; View > Waveform Editor / Multitrack Session; Window > Mixer, Track Effects | `src/app/AppCommands.cpp` | done |
| 5.9 | Mixdown to a file and to a new file in the waveform editor | `SessionMixer::mixdown`, `SessionWorkspace` | done |
| 5.10 | Tests (edits, automation, mixer, crossfades, buses, sends, effects, mixdown, playback at another rate, JSON round trip, 10 track acceptance) and a smoke test (new session, import at another rate, bus, mixdown, save, close, open) | `tests/SessionTests.cpp`, `Main.cpp`, CI | done |

Decision record: [0009](../adr/0009-multitrack-session.md). Moved to later: see `docs/later.md`.

## Shortcuts

| Key | Command |
|---|---|
| Ctrl+Shift+N | New Multitrack Session |
| 9 / 0 | Waveform Editor / Multitrack Session |
| Ctrl+I | Import Files (into the session) |
| Shift+T | Add Audio Track |
| Shift+B | Add Bus |
| Ctrl+K | Split at Cursor |
| Delete / Shift+Delete | Delete / Ripple Delete |
| Shift+S | Snap on/off |
| Shift+X | Mixer on/off |
| Space, R, Home, End, Ctrl+Z, Ctrl+C, Ctrl+V, Ctrl+1/3/F | as in the waveform editor |

No punctuation keys and no Ctrl+Alt (AltGr on Turkish keyboards).

Mouse in the time line: click places the cursor, drag selects time; drag a clip to move it (also onto another
track), drag its left or right edge to trim, the small squares at its top corners to fade. Right-click a clip or
a track header for more. Ctrl + wheel zooms, Shift + wheel scrolls sideways. Audio files dropped on a track
become clips there.

## Manual test list for Baybars (Türkçe)

CI'nin ürettiği zip'i indir. Chrome engellerse Ctrl+J > dosyanın yanında "Yine de sakla".

1. Dosya > Yeni Çok Kanallı Oturum (Ctrl+Shift+N): 48000 Hz seç, Oluştur. 4 boş kanal ve altta mikser açılır.
2. Bir konuşma, bir müzik ve bir jingle dosyasını Windows Gezgini'nden kanalların üzerine sürükle bırak
   (ya da Çok Kanallı > Dosya İçe Aktar, Ctrl+I). Her biri bıraktığın yerde klip olur. Shift+T ile kanal ekleyerek
   10 kanala çıkar (kabul testi: 10 kanallı oturum).
3. Klipleri sürükleyerek taşı, başka kanala götür; kenarlarından kırp; üst köşelerdeki küçük karelerle fade in/out ver.
   İki klibi aynı kanalda üst üste getir: geçiş kendiliğinden yumuşak olur. Ctrl+K ile imleçten böl.
   Shift+Delete ile kaydırarak sil. Her adımı Ctrl+Z ile geri alabilmelisin.
4. Boşlukla çal. Mikserde fader, pan, M (sustur) ve S (solo) dene; çalarken hemen duyulmalı.
5. Bir kanalın A düğmesine bas: altında ses düzeyi ve pan şeritleri açılır. Tıklayarak noktalar ekle (müziği konuşma
   sırasında kıs), noktaları sürükle. Çalınca ses düzeyi eğriyi izlemeli.
6. Shift+B ile bus ekle. Mikserde konuşma kanallarının "Out" kutusunu bus'a çevir; bus'ın FX düğmesiyle bir efekt
   ekle (ör. kompresör). Ayrıca "Send" ile bir kanalın kopyasını bus'a gönder.
7. Mikrofonla kayıt: bir kanalın R düğmesine bas, imleci yerleştir, R tuşuyla kaydı başlat, tekrar R ile durdur.
   Kayıt o kanala, başladığın yere klip olarak gelmeli ve çalınan müzikle hizalı olmalı.
8. Dosya > Kaydet (Ctrl+S): `.mxj` dosyası ve yanında `_data` klasörü oluşur. Programı kapat, yeniden aç,
   Dosya > Aç ile `.mxj` dosyasını aç: her şey aynı yerde olmalı.
9. Dosya > Dışa Aktar (Ctrl+Shift+E) ile oturumu tek bir WAV/FLAC olarak dışa aktar ve başka bir oynatıcıda dinle.
   Çok Kanallı > Yeni Dosyaya Mixdown ile karışım dalga formu düzenleyicide açılır.
10. 9 ve 0 tuşlarıyla dalga formu düzenleyici ile çok kanallı oturum arasında geçiş yap.
