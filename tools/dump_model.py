"""Dump one SVTX+TGEO model to JSON + a flat-shaded PNG preview.

Usage::

    python tools/dump_model.py --level 0x0B --model BA10G --out tools/model_dump

Writes ``<EID>.json`` (frames, vertices, triangles, draw calls, bbox) and
``<EID>_f0.png`` (software-rasterised, flat shaded, no textures).  No external
dependencies: the PNG writer is the same minimal zlib writer used by
``tools/dump_rawd.py``.
"""
import argparse
import json
import math
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import cb3
import svtx
import tgeo


# ---------------------------------------------------------------------------
# PNG
# ---------------------------------------------------------------------------

def write_png(path, width, height, pixels, alpha=False):
    """Write row-major 8-bit RGB or RGBA pixels as a PNG."""
    channels = 4 if alpha else 3
    stride = width * channels

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw += pixels[y * stride:(y + 1) * stride]

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8,
                                      6 if alpha else 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


# ---------------------------------------------------------------------------
# tiny vector helpers
# ---------------------------------------------------------------------------

def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def _norm(a):
    n = math.sqrt(_dot(a, a))
    if n <= 1e-12:
        return (0.0, 0.0, 0.0)
    return (a[0] / n, a[1] / n, a[2] / n)


# ---------------------------------------------------------------------------
# renderer
# ---------------------------------------------------------------------------

def render_flat(mesh, frame=0, width=512, height=512):
    """Render triangles as flat-shaded, two-sided polygons with a z-buffer."""
    verts = mesh["vertices"]
    tris = mesh["triangles"]

    # bbox of the selected frame
    if frame == 0:
        bmin = mesh["bbox"]["min"]
        bmax = mesh["bbox"]["max"]
    else:
        pts = [v["frame_positions"][frame] for v in verts]
        bmin = [min(p[i] for p in pts) for i in range(3)]
        bmax = [max(p[i] for p in pts) for i in range(3)]

    centre = tuple((bmin[i] + bmax[i]) * 0.5 for i in range(3))
    radius = 0.5 * math.sqrt(sum((bmax[i] - bmin[i]) ** 2 for i in range(3)))
    if radius < 1e-3:
        radius = 1.0

    view_dir = _norm((1.0, 0.7, 1.0))
    eye = tuple(centre[i] + view_dir[i] * radius * 3.0 for i in range(3))
    fwd = _norm(_sub(centre, eye))
    up = (0.0, 1.0, 0.0)
    right = _cross(fwd, up)
    if _dot(right, right) < 1e-8:
        up = (0.0, 0.0, 1.0)
        right = _cross(fwd, up)
    right = _norm(right)
    up = _norm(_cross(right, fwd))

    fov = math.radians(40.0)
    focal = 1.0 / math.tan(fov * 0.5)
    light = _norm((0.35, 0.85, 0.45))

    zbuf = [float("inf")] * (width * height)
    colour = bytearray(b"\x18\x1c\x26" * (width * height))
    covered = 0

    def project(p):
        d = _sub(p, eye)
        xc = _dot(d, right)
        yc = _dot(d, up)
        zc = _dot(d, fwd)
        if zc <= 0.01:
            return None
        sx = width * 0.5 + focal * (xc / zc) * (width * 0.5)
        sy = height * 0.5 - focal * (yc / zc) * (height * 0.5)
        return (sx, sy, zc)

    for tri in tris:
        idx = tri["indices"]
        p = [verts[i]["frame_positions"][frame] for i in idx]
        s = [project(v) for v in p]
        if any(v is None for v in s):
            continue
        (ax, ay, az), (bx, by, bz), (cx, cy, cz) = s

        normal = _cross(_sub(p[1], p[0]), _sub(p[2], p[0]))
        nlen = math.sqrt(_dot(normal, normal))
        shade = 1.0 if nlen <= 1e-12 else abs(_dot(
            (normal[0] / nlen, normal[1] / nlen, normal[2] / nlen), light))
        shade = 0.28 + 0.72 * shade

        cr = 0.0
        cg = 0.0
        cb = 0.0
        for i in idx:
            c = verts[i]["color"]
            cr += c[0]
            cg += c[1]
            cb += c[2]
        cr = min(255, int(255 * (cr / 3.0) * shade))
        cg = min(255, int(255 * (cg / 3.0) * shade))
        cb = min(255, int(255 * (cb / 3.0) * shade))

        det = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax)
        if abs(det) < 1e-9:
            continue
        minx = max(0, int(math.floor(min(ax, bx, cx))))
        maxx = min(width - 1, int(math.ceil(max(ax, bx, cx))))
        miny = max(0, int(math.floor(min(ay, by, cy))))
        maxy = min(height - 1, int(math.ceil(max(ay, by, cy))))
        if minx > maxx or miny > maxy:
            continue

        for py in range(miny, maxy + 1):
            fy = py + 0.5
            for px in range(minx, maxx + 1):
                fx = px + 0.5
                w0 = ((bx - fx) * (cy - fy) - (by - fy) * (cx - fx)) / det
                w1 = ((cx - fx) * (ay - fy) - (cy - fy) * (ax - fx)) / det
                w2 = 1.0 - w0 - w1
                if w0 < -1e-6 or w1 < -1e-6 or w2 < -1e-6:
                    continue
                z = w0 * az + w1 * bz + w2 * cz
                o = py * width + px
                if z <= 0.01 or z >= zbuf[o]:
                    continue
                if zbuf[o] == float("inf"):
                    covered += 1
                zbuf[o] = z
                colour[o * 3 + 0] = cr
                colour[o * 3 + 1] = cg
                colour[o * 3 + 2] = cb

    return width, height, bytes(colour), covered


# ---------------------------------------------------------------------------

def dump(level_id, model, out_dir, vtx_name=None, frame=0, size=512):
    arc = cb3.load_level(level_id)
    geo_entry = arc.entry(model)
    if geo_entry is None:
        raise SystemExit("model %s not found in level 0x%02X" % (model, level_id))
    if vtx_name is None:
        if not model.endswith("G"):
            raise SystemExit("cannot infer SVTX name from %s; use --vtx" % model)
        vtx_name = model[:-1] + "V"
    vtx_entry = arc.entry(vtx_name)
    if vtx_entry is None:
        raise SystemExit("SVTX %s not found in level 0x%02X" %
                         (vtx_name, level_id))

    geo = tgeo.parse_tgeo(geo_entry)
    frames = svtx.parse_svtx(vtx_entry)
    mesh = tgeo.build_mesh(geo, frames)

    os.makedirs(out_dir, exist_ok=True)
    json_path = os.path.join(out_dir, "%s.json" % model)
    png_path = os.path.join(out_dir, "%s_f%d.png" % (model, frame))

    doc = {
        "source": {
            "disc": "SCUS-94244",
            "level": "0x%02X" % level_id,
            "geo": model,
            "vtx": vtx_name,
        },
        "tgeo": geo.as_dict(),
        "svtx": {
            "frame_count": len(frames),
            "frames": [f.as_dict(with_points=True) for f in frames],
        },
        "mesh": mesh,
    }
    with open(json_path, "w") as f:
        json.dump(doc, f, indent=1)

    w, h, pixels, covered = render_flat(mesh, frame=frame, width=size,
                                        height=size)
    write_png(png_path, w, h, pixels)

    # numeric sanity report
    bmin = mesh["bbox"]["min"]
    bmax = mesh["bbox"]["max"]
    print("%s / %s: %d frames, %d triangles, %d corners, %d draw call(s)" %
          (model, vtx_name, len(frames), mesh["tri_count"],
           mesh["vertex_count"], len(mesh["draw_calls"])))
    print("  bbox frame %d: min=%s max=%s" % (
        frame, [round(v, 3) for v in bmin], [round(v, 3) for v in bmax]))
    print("  png: %s (%dx%d, %d covered px)" % (png_path, w, h, covered))
    print("  json: %s" % json_path)
    if mesh["warnings"]:
        print("  warnings:")
        for wmsg in mesh["warnings"][:12]:
            print("    -", wmsg)
    if covered == 0:
        print("  ! preview is empty")
    return json_path, png_path, covered


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--level", default="0x0B",
                    help="level id, e.g. 0x0B (default)")
    ap.add_argument("--model", required=True, help="TGEO entry id, e.g. BA10G")
    ap.add_argument("--vtx", default=None,
                    help="SVTX entry id (default: model name with V)")
    ap.add_argument("--out", default=os.path.join(HERE, "model_dump"),
                    help="output directory")
    ap.add_argument("--frame", type=int, default=0, help="frame for the preview")
    ap.add_argument("--size", type=int, default=512, help="preview size (px)")
    args = ap.parse_args(argv)

    dump(int(args.level, 0), args.model, args.out, args.vtx,
         args.frame, args.size)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
