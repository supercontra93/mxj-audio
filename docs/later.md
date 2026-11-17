# Later

Ideas that came up during a phase but are outside its scope. Reviewed at the start of each phase.

- M4A, AAC and WMA import through the decoders built into Windows (Media Foundation), no bundled FFmpeg.
  Master plan, section 5. Needs a small `platform/` reader; JUCE has none on Windows.
- Keep the decoded audio of compressed files (FLAC, MP3) between runs, so their second open is instant too
  (WAV already is, through the peak file cache).
- Recent files list on the start page and in the File menu.
- Playback of more than two channels (5.1 and up) mixed to stereo; today the first two channels play.
- A level meter in the transport bar (phase 3 brings metering).
- Higher quality sample rate conversion with r8brain (planned in the master plan); phase 1 uses JUCE's windowed sinc.
- MP3 (and M4A) export. Needs an encoder such as LAME (LGPL): own decision record and licence check first.
- Fade handles on the waveform (drag to set the fade length and curve), fade curve choice in a dialog.
- Input monitoring and a level meter before recording starts; punch-in / punch-and-roll recording.
- Dither when exporting to 16 bit.
- "Consolidate": render a heavily edited file into new blocks so reading stays cheap.
- Export regions between markers as separate files (batch export).
