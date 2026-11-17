# Phase 3 - Effects, meters and loudness (version 0.3)

Goal (master plan, section 11): effect framework, live preview, the first effect set, effect rack,
presets, meters, LUFS. Plus Baybars's request: larger interface text, especially the menu bar.

Acceptance: **a podcast episode is processed with EQ and a compressor and brought to a target loudness.**

## Tasks

| # | Task | Where | Status |
|---|---|---|---|
| 3.1 | Effect framework: parameters (number, choice, on/off, skewed ranges), processors, effect types with factory presets, catalogue, chains | `src/dsp/effects` | done |
| 3.2 | Effects: Amplify, Normalize, Match Loudness, Compressor, Limiter (look-ahead), Noise Gate, De-esser, Parametric EQ, Graphic EQ, Filter, Delay, Reverb, Chorus, Flanger, Phaser, Stereo Tools, Change Speed, Invert, Reverse; generators Tone and Noise | `src/dsp/effects`, `src/dsp/filters` | done |
| 3.3 | Offline rendering into new blocks (non-destructive, one undo step), latency compensation, cancel and progress | `src/dsp/process`, `src/app/Workspace.cpp` | done |
| 3.4 | Live chain in the playback engine, swapped without locks; live preview from the effect dialog | `src/engine/playback` | done |
| 3.5 | Effect dialog: automatic controls, EQ/filter response curve, gain reduction meter, presets (built-in and own), Add to Rack | `src/app/effects` | done |
| 3.6 | Effect rack: chain of effects, on/off, order, Listen Live, rack presets (3 built-in), Apply Rack | `src/app/effects/RackPanel`, `src/services/presets` | done |
| 3.7 | Loudness: EBU R128 / ITU BS.1770 momentary, short-term, integrated, loudness range, 4x oversampled true peak | `src/dsp/analysis` | done |
| 3.8 | Level meters (peak, RMS, hold, clip, true peak, phase correlation, live LUFS), spectrum analyser | `src/app/meters` | done |
| 3.9 | "Loudness and Levels" table for the selection or the file, with Copy | `src/app/effects/StatsDialog` | done |
| 3.10 | Text size: View > Text Size (4 steps, default one step larger than before); menu bar and menus larger | `src/app/theme/Fonts` | done |
| 3.11 | Tests (EBU 3341/3342 loudness cases, true peak, filters, dynamics, latency, presets, translations); smoke test applies a compressor and Match Loudness and undoes them | `tests/`, CI | done |

Decision record: [0007](../adr/0007-effects-and-loudness.md). Moved to later: see `docs/later.md`.

## Shortcuts

| Key | Command |
|---|---|
| Ctrl+R | Show or hide the effect rack |

Effects are in the Effects menu, grouped by kind, and in the command palette.

## Manual test list for Baybars (Türkçe)

CI'nin ürettiği zip'i indir. Chrome engellerse Ctrl+J > dosyanın yanında "Yine de sakla".

1. Görünüm > Yazı Boyutu: Küçük, Normal, Büyük, En Büyük'ü dene. Menü çubuğu ve paneller büyüyüp küçülmeli,
   seçim program kapanıp açılınca da kalmalı.
2. Bir konuşma kaydı (podcast) aç. Pencere > Seviye Ölçerleri açık olsun. Boşlukla çal: sağdaki çubuklar,
   "Anlık / Kısa süreli / Toplam" LUFS değerleri ve faz çizgisi oynamalı. Panele tıklayınca sıfırlanır.
3. Pencere > Spektrum Analizörü: çalarken frekans eğrisi görünmeli.
4. Efektler > Filtre ve EQ > Parametrik EQ: "Podcast sesi" hazır ayarını seç, Önizle'ye bas ve bir düğmeyi
   oynatırken sesin değiştiğini dinle. Uygula: seçim (ya da seçim yoksa tüm dosya) işlenir. Ctrl+Z geri alır.
5. Efektler > Genlik ve Dinamik > Kompresör: "Podcast sesi" hazır ayarı, önizlemede "Kazanç azaltma" çubuğu
   hareket etmeli. Uygula.
6. Efektler > Ses Yüksekliğini Eşle (LUFS): "Podcast (-16 LUFS)" ile uygula. Sonra Efektler > Ses Yüksekliği ve
   Seviyeler...: "Toplam ses yüksekliği" yaklaşık -16 LUFS, gerçek tepe -1 dBTP'nin altında olmalı.
   (Kabul testi budur.)
7. Ctrl+R ile efekt rafını aç. Hazır Ayarlar > Podcast sesi. Canlı Dinle'yi açıp çal: raf sesi canlı değiştirir,
   dosya değişmez. Bir efekti kutusundan kapat, sırasını oklarla değiştir. Rafı Uygula ile dosyaya işle.
8. Bir efekt penceresinde "Geçerli Ayarları Hazır Ayar Olarak Kaydet..." ile kendi ayarını kaydet; pencereyi
   kapatıp açınca Hazır Ayarlar > Benim ayarlarım altında görünmeli.
9. Efektler > Üret > Ton: imlecin olduğu yere bir test tonu eklenir (hazır ayar: "1 kHz referans"). Ters Çevir, Tersten Çal ve Hızı Değiştir'i de dene.
10. Programı kapatıp aç: raf içeriği ve panellerin açık/kapalı durumu kalmalı.
