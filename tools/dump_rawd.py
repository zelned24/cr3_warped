"""Dump every RAWD (type 17) entry of Crash Bandicoot: Warped to disk.

For each level archive this writes::

    tools/rawd_dump/<level>_<entry>/
        tex_<i>_f<NN>.png     each 32x32 RGBA animation frame (8x zoom)
        mesh_<i>.json         triangles (colour + x/y/u/v)
        layer_<i>.png         the 2D layer rendered with its texture

Run from anywhere::

    python tools/dump_rawd.py [level ...]

"""
import json
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import nsf
import rawd

ISO = r"C:\Users\operacion02\AppData\Local\Temp\opencode\cbwarped_iso"
OUT = os.path.join(HERE, "rawd_dump")
ZOOM = 8


# ---------------------------------------------------------------------------
# minimal PNG writer (no PIL dependency)
# ---------------------------------------------------------------------------

def write_png(path, width, height, pixels, zoom=1):
    """Write 8-bit RGBA pixels (bytes, row-major) as a PNG."""
    if zoom > 1:
        row = bytearray()
        rows = []
        for y in range(height):
            src = pixels[y * width * 4:(y + 1) * width * 4]
            row = bytearray()
            for x in range(width):
                row += src[x * 4:x * 4 + 4] * zoom
            for _ in range(zoom):
                rows.append(bytes(row))
        pixels = b"".join(rows)
        width *= zoom
        height *= zoom

    def chunk(tag, data):
        out = struct.pack(">I", len(data)) + tag + data
        return out + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)
        raw += pixels[y * stride:(y + 1) * stride]
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


# ---------------------------------------------------------------------------
# layer rasteriser
# ---------------------------------------------------------------------------

def sample(rgba, u, v):
    x = int(u) & 31
    y = int(v) & 31
    o = (y * 32 + x) * 4
    return rgba[o], rgba[o + 1], rgba[o + 2], rgba[o + 3]


def render_layer(mesh, texture):
    minx, miny, maxx, maxy = mesh.bounds()
    w = maxx - minx + 1
    h = maxy - miny + 1
    scale = 3
    W, H = w * scale, h * scale
    buf = bytearray(b"\x20\x20\x28\xff" * (W * H))

    rgba = texture.frame(0) if texture is not None else None
    for colour, tri in mesh.triangles:
        (x0, y0, u0, v0), (x1, y1, u1, v1), (x2, y2, u2, v2) = tri
        x0 -= minx; x1 -= minx; x2 -= minx
        y0 -= miny; y1 -= miny; y2 -= miny
        det = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
        if det == 0:
            continue
        bx0 = max(0, int(min(x0, x1, x2) * scale))
        bx1 = min(W - 1, int(max(x0, x1, x2) * scale) + 1)
        by0 = max(0, int(min(y0, y1, y2) * scale))
        by1 = min(H - 1, int(max(y0, y1, y2) * scale) + 1)
        for py in range(by0, by1 + 1):
            fy = (py + 0.5) / scale
            for px in range(bx0, bx1 + 1):
                fx = (px + 0.5) / scale
                a = ((x1 - fx) * (y2 - fy) - (x2 - fx) * (y1 - fy)) / det
                b = ((x2 - fx) * (y0 - fy) - (x0 - fx) * (y2 - fy)) / det
                c = 1.0 - a - b
                if a < -0.001 or b < -0.001 or c < -0.001:
                    continue
                if rgba is not None:
                    u = (a * u0 + b * u1 + c * u2) / rawd.UV_SCALE
                    v = (a * v0 + b * v1 + c * v2) / rawd.UV_SCALE
                    r, g, bl, al = sample(rgba, u, v)
                else:
                    r, g, bl = colour
                    al = 255
                # straight alpha over the dark background
                o = (py * W + px) * 4
                a8 = al / 255.0
                buf[o] = int(r * a8 + buf[o] * (1 - a8))
                buf[o + 1] = int(g * a8 + buf[o + 1] * (1 - a8))
                buf[o + 2] = int(bl * a8 + buf[o + 2] * (1 - a8))
                buf[o + 3] = 255
    return W, H, bytes(buf)


# ---------------------------------------------------------------------------

def level_files():
    out = []
    for folder in ("S0", "S1", "S2", "S3"):
        fdir = os.path.join(ISO, folder)
        if not os.path.isdir(fdir):
            continue
        for name in sorted(os.listdir(fdir)):
            if name.endswith(".NSF"):
                out.append((int(name[1:-4], 16), os.path.join(fdir, name)))
    return out


def dump_level(level_id, path, only=None):
    try:
        chunks = nsf.parse_nsf(path)
    except Exception as exc:
        print("  ! %s: %s" % (path, exc))
        return 0

    rawds = list(rawd.entries_of(chunks))
    if not rawds:
        return 0
    if only and level_id not in only:
        return 0

    # entry name -> decoded RAWD, for texture lookup across entries
    by_name = {r.name: r for r in rawds}
    total = 0
    for r in rawds:
        root = os.path.join(OUT, "%02X_%s" % (level_id, r.name))
        os.makedirs(root, exist_ok=True)
        for tex in r.textures:
            for f in range(tex.frame_count):
                w, h, px = 32, 32, tex.frame(f)
                write_png(os.path.join(root, "tex_%d_f%02d.png" % (tex.index, f)),
                          w, h, px, ZOOM)
                total += 1
        for mesh in r.meshes:
            doc = {
                "level": level_id,
                "entry": r.name,
                "item": mesh.index,
                "texture": mesh.tex_name,
                "triangles": [
                    {"colour": list(colour), "vertices": [list(v) for v in tri]}
                    for colour, tri in mesh.triangles
                ],
            }
            with open(os.path.join(root, "mesh_%d.json" % mesh.index), "w") as f:
                json.dump(doc, f, indent=1)
            tex = by_name.get(mesh.tex_name[:-1] + "R")
            if tex is None or not tex.textures:
                tex = r if r.textures else None
            texture = tex.textures[0] if tex else None
            W, H, px = render_layer(mesh, texture)
            write_png(os.path.join(root, "layer_%d.png" % mesh.index), W, H, px)
            total += 2
        print("  0x%02X %-6s meshes=%d textures=%d blocks=%d" %
              (level_id, r.name, len(r.meshes), len(r.textures), len(r.blocks)))
    return total


def main():
    only = set(int(a, 0) for a in sys.argv[1:]) or None
    os.makedirs(OUT, exist_ok=True)
    files = 0
    for level_id, path in level_files():
        files += dump_level(level_id, path, only)
    print("wrote %d files under %s" % (files, OUT))


if __name__ == "__main__":
    main()
