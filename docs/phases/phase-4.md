# Phase 4 - Spectral view and repair (version 0.4)

Goal (master plan, section 11): spectrogram, spectral selection and healing, noise reduction, click, hum and
clipping repair. First task, asked for by Baybars: File > New Audio File.

Acceptance: **a cough and the background noise are cleaned from a recording.**

## Tasks

| # | Task | Where | Status |
|---|---|---|---|
| 4.1 | File > New Audio File: empty file, sample rate (8 to 192 kHz) and mono or stereo, last choice remembered | `src/app/editor/NewFileDialog`, `Workspace::newFile` | done |
| 4.2 | Real FFT and STFT engine (Hann, 75 % overlap, exact resynthesis, chunked for long files) | `src/dsp/spectral/Fft`, `Stft` | done |
| 4.3 | Spectrogram under the waveform: linear, logarithmic and mel scale, 3 resolutions, 3 colour maps, drawn on a background thread | `src/app/editor/SpectrogramRenderer`, `WaveformView` | done |
| 4.4 | Spectral selection: drag a rectangle (time and frequency) in the spectrogram | `WaveformView`, `Workspace::setSpectralSelection` | done |
| 4.5 | Spectral edits: heal from the surroundings (lowers coughs and clicks, fills holes), change level, delete frequencies | `src/dsp/spectral/SpectralEdit` | done |
| 4.6 | Any effect or the rack on the selected frequency band only | `Workspace::processRange`, `spectral::blend` | done |
| 4.7 | Noise reduction with a noise print (Capture Noise Print, Shift+P) and adaptive noise reduction (no print), with live preview | `src/dsp/repair/NoiseReduction` | done |
| 4.8 | Hum removal (50/60 Hz and harmonics), click removal and clip repair (AR model, least squares interpolation) | `src/dsp/repair/Restoration` | done |
| 4.9 | Tests (FFT, exact STFT, band delete, heal, band effect, noise reduction, hum, clicks, clipping, translations); smoke tests create a new file and heal, capture a noise print, reduce noise and remove clicks | `tests/RepairTests.cpp`, CI | done |

Decision record: [0008](../adr/0008-spectral-and-repair.md). Moved to later: see `docs/later.md`.

## Shortcuts

| Key | Command |
|---|---|
| Ctrl+N | New Audio File |
| Shift+D | Spectral Frequency Display on/off |
| Shift+P | Capture Noise Print |
| Ctrl+U | Heal Selection |
| Delete | With a frequency band selected: delete only those frequencies |

All letters, so they work on Turkish Q and F keyboards too.

## Manual test list for Baybars (Türkçe)

CI'nin ürettiği zip'i indir. Chrome engellerse Ctrl+J > dosyanın yanında "Yine de sakla".

1. Dosya > Yeni Ses Dosyası (Ctrl+N): 44100 Hz, Mono seç, Oluştur. Boş bir dosya açılır. Efektler > Üret > Ton ile
   ses ekle ya da R ile kayıt yap. Ctrl+N'i tekrar açınca son seçimin hatırlanmalı.
2. Öksürüklü ve arkasında hışırtı olan bir konuşma kaydı aç. Görünüm > Spektral Frekans Görünümü (Shift+D):
   dalga formunun altında renkli frekans resmi çıkar. Görünüm > Spektrogram altından ölçeği, çözünürlüğü ve
   renkleri değiştir.
3. Öksürük: spektrogramda öksürüğün üzerinde fareyle yukarıdan aşağı bir dikdörtgen çiz, Ctrl+U (Seçimi İyileştir).
   Öksürük çevresindeki ses seviyesine iner. Dinle (boşluk), Ctrl+Z ile geri alıp farkı karşılaştır.
4. Bir frekans bandı seç (örneğin yalnızca 2-4 kHz arası bir dikdörtgen) ve Delete'e bas: yalnızca o frekanslar silinir.
   Efektler > Gürültü Azaltma ve Onarım > Seçimin Seviyesini Değiştir... ile -12 dB de dene.
5. Dip gürültüsü: yalnızca hışırtı olan bir sessizlik seç (en az yarım saniye), Shift+P (Gürültü Örneği Al).
   Sonra hiçbir şey seçme (tüm dosya), Efektler > Gürültü Azaltma ve Onarım > Gürültü Azaltma (Gürültü Örneği)...
   Önizle'ye bas, "Azaltma" düğmesini oynat; "Yalnızca çıkarılan gürültüyü dinle" ile neyin silindiğini duy. Uygula.
   (Kabul testi 3. ve 5. maddedir.)
6. Gürültü örneği almadan Uyarlamalı Gürültü Azaltma'yı da dene.
7. Uğultu Giderme: "50 Hz uğultu" hazır ayarı. Tık Giderme ve Kırpılma Onarımı'nı bozuk bir kayıtta dene; durum
   çubuğunda kaç tane düzeltildiği yazar.
8. Bir frekans bandı seçiliyken bir efekt uygula (ör. Amplify +6 dB): yalnızca o bant değişmeli.
