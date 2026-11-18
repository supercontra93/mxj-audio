# Later

Ideas that came up during a phase but are outside its scope. Reviewed at the start of each phase.

- M4A, AAC and WMA import through the decoders built into Windows (Media Foundation), no bundled FFmpeg.
  Master plan, section 5. Needs a small `platform/` reader; JUCE has none on Windows.
- Keep the decoded audio of compressed files (FLAC, MP3) between runs, so their second open is instant too
  (WAV already is, through the peak file cache).
- Recent files list on the start page and in the File menu.
- Playback of more than two channels (5.1 and up) mixed to stereo; today the first two channels play.
- Higher quality sample rate conversion with r8brain (planned in the master plan); phase 1 uses JUCE's windowed sinc.
- MP3 (and M4A) export. Needs an encoder such as LAME (LGPL): own decision record and licence check first.
- Fade handles on the waveform (drag to set the fade length and curve), fade curve choice in a dialog.
- Input monitoring and a level meter before recording starts; punch-in / punch-and-roll recording.
- Dither when exporting to 16 bit.
- "Consolidate": render a heavily edited file into new blocks so reading stays cheap.
- Export regions between markers as separate files (batch export).
- Time stretch and pitch shift without changing the other (needs a library and its own decision record).
- Convolution reverb with impulse responses; multiband compressor; noise reduction from a noise profile.
- Loop the selection while previewing an effect; favourite effects at the top of the menu.
- Loudness weighting for surround channels (today every channel counts equally, right for mono and stereo).
- Anti-aliasing filter before Change Speed speeds up (today plain interpolation).
- Effect dialogs that stay open while editing (non-modal) and A/B comparison of settings.
- Spectral selection with lasso and brush (phase 4 has the rectangle); a time-only selection tool in the spectrogram.
- Save and load noise prints; noise reduction with RNNoise as a "voice" mode (own decision record).
- Heal: a second pass or shorter windows for very sharp onsets (a little of a cough's first click can remain).
- Spectrogram: zoom the frequency axis, a level range setting, and a cache so scrolling back is instant.
- Click removal and clip repair with live preview (today they work on the selection and are undone with Ctrl+Z).
- Session: plugin delay compensation; effect parameter automation; effects on single clips; ducking (sidechain);
  freeze a track; crash recovery for sessions (the waveform editor has it); recording several inputs at once.
- Session: open a clip in the waveform editor and bring the edit back; clip time stretch; markers in the session
  ruler (the model has them); colour choice per track; track height and a zoom for the track list.
- Project: "Consolidate" removes audio in `Name_data/` that no clip uses any more; "Save a copy with all files"
  that copies referenced WAV/AIFF files into the project folder.
- Lower look-ahead for the session mixer (today about 0.17 s between moving a fader and hearing it).
