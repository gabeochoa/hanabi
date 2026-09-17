#!/usr/bin/env python3
import binascii
import math
import struct
import zlib
from pathlib import Path

HERE = Path(__file__).parent
WIDTH = 640
HEIGHT = 400


def chunk(kind: bytes, data: bytes) -> bytes:
    checksum = binascii.crc32(kind + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", checksum)


def bar_height(i: int) -> int:
    return [120, 190, 260, 210, 300, 330, 280][i]


pixels = bytearray()
for y in range(HEIGHT):
    row = bytearray([0])
    for x in range(WIDTH):
        r, g, b = 250, 250, 247
        if y == HEIGHT - 40 or x == 48:
            r, g, b = 60, 60, 60
        col = (x - 64) // 80
        if 0 <= col < 7 and 64 + col * 80 <= x < 64 + col * 80 + 56:
            top = HEIGHT - 40 - bar_height(col)
            if top <= y < HEIGHT - 40:
                r, g, b = (66, 133, 244) if col != 5 else (247, 181, 70)
        if 30 <= y < 34 and 48 <= x < 48 + 300:
            r, g, b = 33, 33, 33
        row.extend((r, g, b, 255))
    pixels.extend(row)

png = b"\x89PNG\r\n\x1a\n"
png += chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 6, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(bytes(pixels), 9))
png += chunk(b"IEND", b"")
(HERE / "chart.png").write_bytes(png)

RATE = 8000
SECONDS = 1.5
frames = bytearray()
count = int(RATE * SECONDS)
for i in range(count):
    t = i / RATE
    env = math.sin(math.pi * i / count)
    tone = math.sin(2 * math.pi * (330 + 220 * t) * t)
    frames.extend(struct.pack("<h", int(12000 * env * tone)))
data = bytes(frames)
wav = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE"
wav += b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, RATE, RATE * 2, 2, 16)
wav += b"data" + struct.pack("<I", len(data)) + data
(HERE / "clip.wav").write_bytes(wav)
