#!/usr/bin/env python3
import binascii
import struct
import zlib
from pathlib import Path

HERE = Path(__file__).parent
WIDTH = 64
HEIGHT = 64


def chunk(kind: bytes, data: bytes) -> bytes:
    checksum = binascii.crc32(kind + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", checksum)


rows = []
for y in range(HEIGHT):
    row = bytearray([0])
    for x in range(WIDTH):
        r = 48 + (x * 86 // (WIDTH - 1))
        g = 64 + (y * 112 // (HEIGHT - 1))
        b = 134 + ((x + y) * 74 // (WIDTH + HEIGHT - 2))
        if (x - 43) ** 2 + (y - 20) ** 2 < 95:
            r, g, b = 247, 181, 70
        row.extend((r, g, b, 255))
    rows.append(bytes(row))

png = b"\x89PNG\r\n\x1a\n"
png += chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 6, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(b"".join(rows), 9))
png += chunk(b"IEND", b"")
(HERE / "sample.png").write_bytes(png)

stream = b"BT /F1 12 Tf 20 50 Td (Hanabi fixture) Tj ET\n"
objects = [
    b"<< /Type /Catalog /Pages 2 0 R >>",
    b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
    b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] "
    b"/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
    b"<< /Length " + str(len(stream)).encode() + b" >>\nstream\n" + stream + b"endstream",
    b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
]
pdf = bytearray(b"%PDF-1.4\n%\x00\xe2\xe3\xcf\xd3\n")
offsets = [0]
for index, obj in enumerate(objects, 1):
    offsets.append(len(pdf))
    pdf.extend(f"{index} 0 obj\n".encode())
    pdf.extend(obj)
    pdf.extend(b"\nendobj\n")
xref = len(pdf)
pdf.extend(f"xref\n0 {len(objects) + 1}\n".encode())
pdf.extend(b"0000000000 65535 f\r\n")
for offset in offsets[1:]:
    pdf.extend(f"{offset:010d} 00000 n\r\n".encode())
pdf.extend(
    f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R >>\n"
    f"startxref\n{xref}\n%%EOF\n".encode()
)
(HERE / "sample.pdf").write_bytes(pdf)
