# Phase 2 - Editing and recording (version 0.2)

Goal (master plan, section 11): selection, cut, copy, paste, undo, fades, markers, recording, saving,
export, crash recovery.

Acceptance: **a voice-over is recorded from start to finish, edited and exported.**

## Tasks

| # | Task | Where | Status |
|---|---|---|---|
| 2.1 | Edit operations as pure functions on `AudioState` (audio + markers): erase, trim, paste, mix paste, silence, insert silence, fades, gain, markers | `src/core/edit` | done |
| 2.2 | Lazy derived sources (silence, gain ramp, mix, channel map) so edits never copy audio | `src/core/audio/DerivedSources` | done |
| 2.3 | Undo/redo of every edit through `History` (one step stores the state before and after) | `src/app/Workspace.cpp` | done |
| 2.4 | Selection with the mouse (drag, Shift+click, edge drag), cursor, clipboard, context menu | `src/app/editor/WaveformView` | done |
| 2.5 | Markers: add at the playhead (also while playing), rename, delete, jump; saved as WAV cue points | `src/core/edit`, `src/formats/metadata` | done |
| 2.6 | Save, Save As, Export (WAV 16/24/32 float, AIFF, FLAC, Ogg Vorbis, Opus), export selection; saving over an open WAV is safe | `src/formats/export`, `src/app/editor/ExportDialog` | done |
| 2.7 | Recording: input device and channels in the audio settings, lock-free recorder, 10 minute segments with live peaks, record into the file at the cursor or into a new file | `src/engine/recording`, `src/app/editor/RecordingPanel` | done |
| 2.8 | Crash recovery: a journal of the open file (also while recording), offered at the next start | `src/core/audio/SequenceSerializer`, `src/app/document/RecoveryJournal` | done |
| 2.9 | Paste between files with different sample rates or channel counts (converted in the background) | `src/dsp/convert` | done |
| 2.10 | Ask to save unsaved changes before open, close and quit | `src/app/Workspace.cpp` | done |
| 2.11 | Tests for edits, markers, export round trips, save over an open file, resampling, the recorder; smoke test cuts, undoes and exports | `tests/`, CI | done |

Decision record: [0006](../adr/0006-editing-recording-recovery.md). Moved to later: MP3 export,
fade handles on the waveform, input monitoring and punch-in recording, dither (`docs/later.md`).

## Shortcuts

Letter and digit keys only, so they work on the Turkish Q layout too.

| Key | Command |
|---|---|
| Ctrl+S / Ctrl+Shift+S | Save / Save As |
| Ctrl+Shift+E | Export |
| Ctrl+Z / Ctrl+Shift+Z | Undo / Redo |
| Ctrl+X / Ctrl+C / Ctrl+V | Cut / Copy / Paste |
| Ctrl+Shift+V | Mix Paste |
| Delete | Delete the selection |
| Ctrl+A / Escape | Select all / Select none |
| Ctrl+T | Trim to selection |
| Ctrl+L | Silence the selection |
| M | Add a marker at the playhead |
| Ctrl+Right / Ctrl+Left | Next / previous marker |
| Ctrl+E | Zoom to selection |
| R | Record (again to stop) |
| Space | Play the selection or from the cursor / stop (also stops recording) |

## Manual test list for Baybars (Türkçe)

CI'nin ürettiği zip'i indirip şunları dene (mikrofon takılı olsun):

1. Programı aç, Düzen > Ses Aygıtı Ayarları. "Kayıt girişi" altında mikrofonunu ve kanalı (mono) seç, pencereyi kapat.
2. Başlangıç sayfasında R tuşuna bas: kayıt başlar, dalga formu canlı çizilir, kırmızı panelde süre akar.
   20-30 saniye konuş, R (veya boşluk) ile durdur. "Kayıt 1" adında bir dosya açılmış olmalı.
3. Boşluk ile dinle. Dalga formunda fareyle sürükleyerek bir hatayı seç, Delete ile sil. Ctrl+Z geri getirir,
   Ctrl+Shift+Z tekrar siler.
4. Bir parçayı seç, Ctrl+C, başka bir yere tıkla, Ctrl+V: araya eklenir. Ctrl+X kesip taşımayı da dene.
5. Başta bir bölüm seç, Düzen > Yükselerek Gir (Fade In); sonda bir bölüm seç, Alçalarak Çık (Fade Out). Dinle.
6. Çalarken M tuşuna birkaç kez bas: dalga formunun üstünde işaretçi bayrakları belirir. Birine çift tıklayıp
   adını değiştir. Ctrl+Sağ/Sol ok ile işaretçiler arasında atla.
7. Dosya > Dışa Aktar (Ctrl+Shift+E): önce WAV 24 bit, sonra FLAC, sonra Ogg veya Opus olarak kaydet. Dosyaları
   başka bir programda (veya MXJ AUDIO'da) açıp dinle. WAV'da işaretçiler de görünmeli.
8. Var olan bir WAV dosyasını aç, bir parça sil, Ctrl+S ile üzerine kaydet. Kapatıp yeniden aç: değişiklik
   kalmış olmalı ve ses bozulmamış olmalı.
9. Değişiklik yapıp kaydetmeden pencereyi kapatmaya çalış: "Kaydedilsin mi?" diye sormalı.
10. Çökme kurtarma: kayıt yaparken programı Görev Yöneticisi'nden zorla kapat. Yeniden açınca kaydı geri
    yüklemeyi teklif etmeli; "Geri Yükle" de, kayıt (son birkaç saniye hariç) geri gelmeli.
11. Var olan bir dosyada imleci ortaya koyup R ile kayıt yap: yeni ses imlecin olduğu yere eklenmeli.
