# mxj_engine

Real-time audio. Rules for the audio thread are in `CLAUDE.md` (rule 4).

- `devices/AudioEngine` - the audio device (WASAPI, ASIO), saved and restored through the settings.
- `playback/PlaybackEngine` - plays an `AudioSequence`: disk thread, lock-free FIFO, sample rate conversion.

Recording, tracks and the mixer arrive in phases 2 and 5.
