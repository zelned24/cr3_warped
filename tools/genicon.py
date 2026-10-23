"""Generate the 64x64 SMDH icon (a stylised Crash head) as a PNG."""
import os
import struct
import zlib

W = H = 48
BG = (0x18, 0x1C, 0x28, 255)
BODY = (0xC8, 0x62, 0x22, 255)
BELLY = (0xF2, 0xDC, 0xB0, 255)
DARK = (0x7A, 0x38, 0x10, 255)
EYE = (0xF8, 0xF8, 0xF8, 255)
PUPIL = (0x10, 0x10, 0x10, 255)


def blend(dst, src):
    sa = src[3] / 255.0
    return tuple(int(src[i] * sa + dst[i] * (1 - sa)) for i in range(3)) + (255,)


def circle(px, py, cx, cy, r):
    return (px - cx) ** 2 + (py - cy) ** 2 <= r * r


def ellipse(px, py, cx, cy, rx, ry):
    return ((px - cx) / rx) ** 2 + ((py - cy) / ry) ** 2 <= 1.0


def main():
    img = [[BG for _ in range(W)] for _ in range(H)]
    for y in range(H):
        for x in range(W):
            c = img[y][x]
            # ears
            if ellipse(x, y, 15, 12, 5, 9) or ellipse(x, y, 33, 12, 5, 9):
                c = blend(c, BODY)
            # head
            if ellipse(x, y, 24, 27, 16, 14):
                c = blend(c, BODY)
            # muzzle
            if ellipse(x, y, 24, 32, 8, 6):
                c = blend(c, BELLY)
            # nose
            if ellipse(x, y, 24, 29, 3, 2):
                c = blend(c, DARK)
            # eyes
            if ellipse(x, y, 18, 23, 4, 5) or ellipse(x, y, 30, 23, 4, 5):
                c = blend(c, EYE)
            if ellipse(x, y, 19, 23, 2, 3) or ellipse(x, y, 29, 23, 2, 3):
                c = blend(c, PUPIL)
            img[y][x] = c

    raw = bytearray()
    for y in range(H):
        raw.append(0)
        for x in range(W):
            raw += bytes(img[y][x])

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")

    path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                        "icon.png")
    with open(path, "wb") as f:
        f.write(png)
    print("wrote", path, len(png), "bytes")


if __name__ == "__main__":
    main()
