"""Offline software renderer for the ported level data.

Loads game_data/levels.bin (the exact blob embedded in the .3dsx) and renders
a level with a z-buffer so the extracted geometry can be verified without a
Nintendo 3DS.
"""
import os
import struct
import sys
import math

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DATA = os.path.join(ROOT, "game_data", "levels.bin")

W, H = 400, 240


def load_levels(path=DATA):
    blob = open(path, "rb").read()
    assert blob[:4] == b"CBWB"
    count = struct.unpack_from("<I", blob, 8)[0]
    out = []
    for i in range(count):
        lid, off, size = struct.unpack_from("<III", blob, 16 + i * 12)
        out.append((lid, blob[off:off + size]))
    return out


def parse_level(data):
    assert data[:4] == b"CBW3"
    model_count = struct.unpack_from("<I", data, 8)[0]
    palette_n = struct.unpack_from("<I", data, 12)[0]
    palette = np.frombuffer(data, dtype=np.uint8,
                            count=palette_n * 4,
                            offset=len(data) - palette_n * 4).reshape(-1, 4)

    allpos = []
    allcol = []
    tris = []
    vbase = 0
    p = 20
    for _ in range(model_count):
        ox, oy, oz = struct.unpack_from("<iii", data, p)
        vc, tc, qc = struct.unpack_from("<III", data, p + 12)
        p += 24
        v = np.frombuffer(data, dtype=np.dtype([("x", "<i2"), ("y", "<i2"),
                                                ("z", "<i2"), ("c", "<u2")]),
                          count=vc, offset=p)
        p += vc * 8
        tri_base = p
        p += tc * 8
        quad_base = p
        p += qc * 10

        pos = np.stack([v["x"], v["y"], v["z"]], axis=1).astype(np.float32)
        pos += np.array([ox, oy, oz], dtype=np.float32) / 16.0
        col = palette[np.clip(v["c"], 0, palette_n - 1)][:, :3].astype(np.float32)
        allpos.append(pos)
        allcol.append(col)

        if tc:
            t = np.frombuffer(data, dtype="<u2", count=tc * 4,
                              offset=tri_base).reshape(-1, 4)[:, 0:3]
            for k in range(tc):
                tris.append(t[k].astype(np.int32) + vbase)
        if qc:
            q = np.frombuffer(data, dtype="<u2", count=qc * 5,
                              offset=quad_base).reshape(-1, 5)[:, 0:4]
            for k in range(qc):
                a, b, c, d = q[k].astype(np.int32) + vbase
                tris.append(np.array([a, b, c]))
                tris.append(np.array([c, b, d]))
        vbase += vc

    pos = np.concatenate(allpos, axis=0)
    col = np.concatenate(allcol, axis=0)
    return pos, col, tris


def render(level_id, data, out_png, eye=None, target=None, fov=60.0):
    pos, col, tris = parse_level(data)
    print("  verts %d tris %d" % (len(pos), len(tris)))
    lo = pos.min(axis=0)
    hi = pos.max(axis=0)
    print("  bounds cm:", lo, hi)
    render_geometry(pos, col, tris, out_png, eye=eye, target=target, fov=fov)


def render_geometry(pos, col, tris, out_png, eye=None, target=None, fov=60.0):
    if eye is None:
        lo, hi = pos.min(axis=0), pos.max(axis=0)
        centre = (lo + hi) * 0.5
        radius = float(np.linalg.norm(hi - lo)) * 0.35 + 200.0
        eye = centre + np.array([radius * 0.9, radius * 0.7, radius * 0.9])
        target = centre
    eye = np.array(eye, dtype=np.float64)
    target = np.array(target, dtype=np.float64)

    fwd = target - eye
    fwd /= np.linalg.norm(fwd)
    up = np.array([0.0, 1.0, 0.0])
    right = np.cross(fwd, up)
    right /= np.linalg.norm(right)
    up = np.cross(right, fwd)

    f = 1.0 / math.tan(math.radians(fov) * 0.5)
    aspect = W / H
    zbuf = np.full((H, W), 1e30)
    img = np.zeros((H, W, 3), dtype=np.float32)
    img[:] = (26, 32, 48)

    for idx in tris:
        if len(idx) != 3:
            continue
        p = pos[idx].astype(np.float64)
        c = col[idx]
        # to camera space
        d = p - eye
        xc = d @ right
        yc = d @ up
        zc = d @ fwd
        if (zc <= 1.0).any():
            continue
        sx = (W * 0.5) + (f / aspect) * (xc / zc) * (W * 0.5)
        sy = (H * 0.5) - f * (yc / zc) * (H * 0.5)
        minx = max(int(math.floor(min(sx[0], sx[1], sx[2]))), 0)
        maxx = min(int(math.ceil(max(sx[0], sx[1], sx[2]))), W - 1)
        miny = max(int(math.floor(min(sy[0], sy[1], sy[2]))), 0)
        maxy = min(int(math.ceil(max(sy[0], sy[1], sy[2]))), H - 1)
        if minx > maxx or miny > maxy:
            continue
        ax, ay = sx[0], sy[0]
        bx, by = sx[1], sy[1]
        cx, cy = sx[2], sy[2]
        det = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax)
        if abs(det) < 1e-9:
            continue
        for py in range(miny, maxy + 1):
            for px in range(minx, maxx + 1):
                fx = px + 0.5
                fy = py + 0.5
                w0 = ((bx - ax) * (fy - ay) - (by - ay) * (fx - ax)) / det
                w1 = ((fx - ax) * (cy - ay) - (fy - ay) * (cx - ax)) / det
                w2 = 1.0 - w0 - w1
                if w0 < 0 or w1 < 0 or w2 < 0:
                    continue
                # barycentric: w0 -> C, w1 -> B, w2 -> A
                z = w2 * zc[0] + w1 * zc[1] + w0 * zc[2]
                if z <= 0.1 or z >= zbuf[py, px]:
                    continue
                zbuf[py, px] = z
                colv = (w2 * c[0] + w1 * c[1] + w0 * c[2])
                img[py, px] = np.clip(colv, 0, 255)

    write_png(out_png, img)
    print("  wrote", out_png)


def write_png(path, img):
    import zlib
    h, w, _ = img.shape
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            raw += bytes(img[y, x].astype(np.uint8))

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)


def main():
    levels = load_levels()
    wanted = int(sys.argv[1], 0) if len(sys.argv) > 1 else 0x0B
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "preview.png")
    for lid, data in levels:
        if lid == wanted:
            print("level 0x%02X" % lid)
            render(lid, data, out)
            return
    print("level not found")


if __name__ == "__main__":
    main()
