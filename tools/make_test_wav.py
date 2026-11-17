#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Writes a stereo 16-bit WAV test file (a slowly pulsing tone) for the CI smoke test.

Usage: python3 tools/make_test_wav.py <output.wav> [seconds]
"""
import math
import struct
import sys
import wave

RATE = 48000

def main():
    path = sys.argv[1]
    seconds = int(sys.argv[2]) if len(sys.argv) > 2 else 60

    # One second, repeated: fast enough in plain Python for long files.
    frames = bytearray()
    for i in range(RATE):
        t = i / RATE
        envelope = abs(math.sin(math.pi * t))
        left = 0.5 * envelope * math.sin(2 * math.pi * 220 * t)
        right = 0.4 * envelope * math.sin(2 * math.pi * 330 * t)
        frames += struct.pack("<hh", int(left * 32767), int(right * 32767))

    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        for _ in range(seconds):
            w.writeframes(frames)

if __name__ == "__main__":
    main()
