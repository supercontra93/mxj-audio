# mxj_formats

Reading and writing audio files and metadata.

- `import/AudioFileFormats` - the formats we open (WAV, AIFF, FLAC, Ogg Vorbis, Opus, MP3) and file headers.
- `import/FileSampleSource` - reads WAV and AIFF in place, without copying.
- `import/AudioImporter` - turns a file into an `AudioSequence` plus its `PeakCache`, on a background thread.

Writing files (save, export) arrives in phase 2.
