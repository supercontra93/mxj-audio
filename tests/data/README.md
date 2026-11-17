# Test data

| File | Made with | Licence |
|---|---|---|
| `sine440-stereo-1s.mp3` | `ffmpeg -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=1" -ac 2 -c:a libmp3lame -b:a 96k` (peak level 0.084) | generated for MXJ AUDIO, CC0 |

FFmpeg was used once, on a developer machine, to make this file. It is not part of the
build or of the program.
