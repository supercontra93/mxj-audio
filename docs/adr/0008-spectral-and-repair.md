# 0008 - Spectral editing and repair

Date: 2026-11-18. Status: accepted.

## Context
Phase 4 adds the spectrogram, spectral selection and healing, noise reduction, hum, click and clipping repair.
The master plan names PFFFT (BSD) for the FFT and RNNoise (BSD) for adaptive noise reduction. Every new library
costs a licence check and build work on Windows, so we first check whether we need them.

## Decision
- **Own real FFT, no PFFFT yet.** `dsp/spectral/Fft` is a radix-2 transform with precomputed tables that
  computes a real FFT through a half-size complex one. A spectrogram of the visible view (about 1500 windows)
  takes a fraction of a second on a background thread, and the repair tools run at many times real time.
  PFFFT can replace it behind the same interface if profiling ever asks for it.
- **One STFT engine** (`dsp/spectral/Stft`): Hann analysis and synthesis windows at 75 % overlap, which add up
  to a constant, so unchanged windows give the audio back exactly (tested). It processes in chunks and reads the
  audio around the range, so edits have no edges. Spectral edits rewrite the selection plus one window on each
  side and fade in and out over half a window in time and two bins in frequency.
- **Heal** brings every frequency of the selection to the level it has just before and after (interpolated
  in dB): louder sound is lowered (coughs, clicks, ringing), holes more than 20 dB down are filled with that
  level and a fixed pseudo random phase (same result when a window is analysed twice).
- **Effects on a band**: the effect runs on the edit range, then `spectral::blend` takes the band from the
  processed audio and everything else from the original. Effects that change the length cannot work on a band.
- **Noise reduction** is a spectral gate (as in Audacity's method): bins under noise plus a margin are turned
  down, with a release in time, a rule that ignores lone bins (against "musical noise") and frequency smoothing
  that may only raise gains (narrow tones are never turned down). The noise comes from a noise print, or for
  the adaptive version from minimum statistics (lowest smoothed level within the adaptation time, times 2).
  **No RNNoise for now**: it is trained for speech, ships model weights and needs its own build; it may come
  later as an extra "voice" mode with its own decision record.
- **Click removal and clip repair** use a linear prediction (AR) model and least squares interpolation
  (Janssen, Veldhuis and Vries 1986), the method Audacity's clip fix and many restoration tools use. Clicks are
  found where the prediction error stands out above its median; clipped samples are runs at the highest peak.
- **Hum removal** uses the existing biquad (peak filter with negative gain) at 50 or 60 Hz and its harmonics.
- **No new dependency.** THIRD_PARTY.md is unchanged.

## Consequences
- Lasso and brush selection (master plan, module F) need a mask instead of a rectangle; `SpectralRegion`
  and `spectral::weight` are the place to add it.
- The noise print lives in memory for the session (like Audition); saving it is listed in `docs/later.md`.
