#!/usr/bin/env python3
"""Make a 512x512 PS5 app icon: a yellow '@' drawn from PDCursesMod's
built-in 8x16 CP437 font (font437.h).  Standard library only.

usage: mkicon.py path/to/font437.h out.png
"""
import re
import struct
import sys
import zlib

SIZE = 512
FG = (255, 215, 0)
BG = (16, 16, 24)


def load_font(path):
    text = open(path, encoding="latin-1").read()
    body = text[text.index("{") + 1:text.rindex("}")]
    return bytes(int(tok, 16) for tok in re.findall(r"0x[0-9a-fA-F]{1,2}", body))


def glyph(font, ch):
    """Return 16 rows x 8 cols of 0/1 for character code ch."""
    offset = struct.unpack_from("<I", font, 10)[0]
    width, height = struct.unpack_from("<ii", font, 18)
    stride = ((width + 31) // 32) * 4
    col, row = ch % 32, ch // 32
    rows = []
    for y in range(16):
        img_y = row * 16 + y
        bmp_y = height - 1 - img_y if height > 0 else img_y
        line = font[offset + bmp_y * stride:offset + (bmp_y + 1) * stride]
        bits = []
        for x in range(8):
            px = col * 8 + x
            bits.append((line[px >> 3] >> (7 - (px & 7))) & 1)
        rows.append(bits)
    return rows


def png(path, pixels):
    raw = b"".join(b"\x00" + bytes(c for px in row for c in px) for row in pixels)

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def main():
    font = load_font(sys.argv[1])
    g = glyph(font, ord("@"))
    # Palette index 1 is the lit pixel unless most of the cell is lit.
    lit = sum(map(sum, g))
    if lit > 64:
        g = [[1 - b for b in r] for r in g]
    scale = 24                       # 8x16 glyph -> 192x384
    ox = (SIZE - 8 * scale) // 2
    oy = (SIZE - 16 * scale) // 2
    pixels = [[BG] * SIZE for _ in range(SIZE)]
    for y in range(16):
        for x in range(8):
            if g[y][x]:
                for yy in range(oy + y * scale, oy + (y + 1) * scale):
                    for xx in range(ox + x * scale, ox + (x + 1) * scale):
                        pixels[yy][xx] = FG
    png(sys.argv[2], pixels)


if __name__ == "__main__":
    main()
