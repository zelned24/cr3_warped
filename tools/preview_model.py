"""Render a single world model of a level, framed on its own bounding box."""
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import preview


def models_of(data):
    mc = struct.unpack_from("<I", data, 8)[0]
    pn = struct.unpack_from("<I", data, 12)[0]
    palette = np.frombuffer(data, dtype=np.uint8, count=pn * 4,
                            offset=len(data) - pn * 4).reshape(-1, 4)
    p = 20
    out = []
    for m in range(mc):
        ox, oy, oz = struct.unpack_from("<iii", data, p)
        vc, tc, qc = struct.unpack_from("<III", data, p + 12)
        p += 24
        v = np.frombuffer(data, dtype=np.dtype([("x", "<i2"), ("y", "<i2"),
                                                ("z", "<i2"), ("c", "<u2")]),
                          count=vc, offset=p)
        vbase = p
        p += vc * 8
        tri_base = p
        p += tc * 8
        quad_base = p
        p += qc * 10
        pos = np.stack([v["x"], v["y"], v["z"]], axis=1).astype(np.float32)
        pos += np.array([ox, oy, oz], dtype=np.float32)
        col = palette[np.clip(v["c"], 0, pn - 1)][:, :3].astype(np.float32)
        tris = []
        if tc:
            t = np.frombuffer(data, dtype="<u2", count=tc * 4,
                              offset=tri_base).reshape(-1, 4)[:, 0:3]
            for k in range(tc):
                tris.append(t[k].astype(np.int32))
        if qc:
            q = np.frombuffer(data, dtype="<u2", count=qc * 5,
                              offset=quad_base).reshape(-1, 5)[:, 0:4]
            for k in range(qc):
                a, b, c, d = q[k].astype(np.int32)
                tris.append(np.array([a, b, c]))
                tris.append(np.array([a, c, d]))
        out.append((pos, col, tris))
    return out


def main():
    level = int(sys.argv[1], 0)
    midx = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    out = sys.argv[3] if len(sys.argv) > 3 else "model.png"

    levels = preview.load_levels()
    data = None
    for lid, blob in levels:
        if lid == level:
            data = blob
    ms = models_of(data)
    pos, col, tris = ms[midx]
    print("model %d: %d verts %d tris" % (midx, len(pos), len(tris)))
    lo, hi = pos.min(axis=0), pos.max(axis=0)
    print("  bbox", lo, hi)

    # centre + a viewpoint looking down at the model from a 3/4 angle
    centre = (lo + hi) * 0.5
    radius = float(np.linalg.norm(hi - lo)) * 0.6 + 50.0
    eye = centre + np.array([radius * 0.8, radius * 0.6, radius * 0.8])
    preview.render_geometry(pos, col, tris, out, eye=eye, target=centre)


if __name__ == "__main__":
    main()
