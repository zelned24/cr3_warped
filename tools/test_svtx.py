"""Executable checks for the SVTX/TGEO port.

Run from anywhere::

    python tools/test_svtx.py

Validates the control model (BA10G/BA10V, Toad Village 0x0B), a one-frame
model (BCH0G/BCH0V) and a compressed multi-frame model (Cr10G/Cr10V), then
sweeps every TGEO in level 0x0B that has a same-name SVTX.
"""
import math
import os
import sys
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import cb3
import svtx
import tgeo

FAILS = []


def check(cond, label, detail=""):
    if cond:
        print("  ok   %s%s" % (label, ("  [" + detail + "]") if detail else ""))
    else:
        print("  FAIL %s%s" % (label, ("  [" + detail + "]") if detail else ""))
        FAILS.append(label)


def check_indices(mesh, label):
    n = mesh["vertex_count"]
    inds = mesh["indices"]
    check(len(inds) == mesh["tri_count"] * 3,
          label + ": index count == tri_count*3", "%d" % len(inds))
    check(all(isinstance(i, int) and 0 <= i < n for i in inds),
          label + ": all indices in [0,%d)" % n)
    check(len(set(inds)) == len(inds),
          label + ": one output vertex per index (no sharing)")
    for tri in mesh["triangles"]:
        if len(tri["indices"]) != 3:
            check(False, label + ": triangle with 3 indices")
            break
    else:
        check(True, label + ": every triangle has 3 indices")

    lo = mesh["bbox"]["min"]
    hi = mesh["bbox"]["max"]
    check(all(math.isfinite(v) for v in lo + hi),
          label + ": bbox finite", "min=%s max=%s" % (lo, hi))
    check(all(lo[i] <= hi[i] for i in range(3)),
          label + ": bbox ordered")
    return lo, hi


def main():
    arc = cb3.load_level(0x0B)

    # ------------------------------------------------------------------
    print("BA10V (SVTX, control):")
    v_entry = arc.entry("BA10V")
    check(v_entry is not None, "entry exists")
    frames = svtx.parse_svtx(v_entry)
    check(len(frames) == 10, "10 frames", "%d" % len(frames))
    check(all(f.vertex_count == 8 for f in frames),
          "8 vertices in every frame",
          str([f.vertex_count for f in frames]))
    o = frames[0].origin
    check(o == (-107.0, 0.0, -102.0), "frame 0 origin == (-107,0,-102)",
          str(o))
    check(all(len(f.points) == f.vertex_count for f in frames),
          "points == vertex_count in every frame")
    check(all(f.start == f.item_start + 0x44 for f in frames),
          "frame vertex data at item+0x44",
          " ".join("0x%X" % (f.start - f.item_start) for f in frames[:3]))
    check(frames[0].view_space_offset == (0, 0, 0),
          "frame 0 view_space_offset == (0,0,0)",
          str(frames[0].view_space_offset))
    check(len(frames[0].hitboxes) == 1, "frame 0 has 1 hitbox")

    # ------------------------------------------------------------------
    print("BA10G (TGEO, control):")
    g_entry = arc.entry("BA10G")
    check(g_entry is not None, "entry exists")
    geo = tgeo.parse_tgeo(g_entry)
    check(geo.tri_count == 12, "tri_count == 12", str(geo.tri_count))
    check(len(geo.tri_flags) == 23, "23 triangle flag words",
          str(len(geo.tri_flags)))
    check(len(geo.colors) == 5, "5 colors", str(len(geo.colors)))
    check(len(geo.uvs) == 11, "11 UVs", str(len(geo.uvs)))
    check(geo.texture_names[:1] == ["Cr10T"], "texture name 0 == Cr10T",
          str(geo.texture_names))
    check(geo.scale == (0.9296875, 1.06640625, 0.9765625), "scale",
          str(geo.scale))
    check(geo.tex_anim_count == 0 and not geo.tex_anims,
          "no texture animations")
    check(not geo.compression, "no compression item")

    mesh = tgeo.build_mesh(geo, frames)
    check(mesh["tri_count"] == 12, "build_mesh: 12 triangles",
          str(mesh["tri_count"]))
    check(mesh["vertex_count"] == 36, "build_mesh: 36 corners",
          str(mesh["vertex_count"]))
    lo, hi = check_indices(mesh, "BA10G")
    size = [hi[i] - lo[i] for i in range(3)]
    check(all(5.0 < s < 100.0 for s in size), "bbox size is crate-like",
          "size=%s" % [round(s, 3) for s in size])
    check(len(mesh["draw_calls"]) == 2,
          "2 draw calls (CLUT 322 and 325)", str(len(mesh["draw_calls"])))
    dc_total = sum(d["index_count"] for d in mesh["draw_calls"])
    check(dc_total == 36, "draw calls cover all 36 indices", str(dc_total))
    check(mesh["draw_calls"][0]["start_index"] == 0,
          "first draw call starts at 0")
    check(all(d["texture_name"] == "Cr10T" for d in mesh["draw_calls"]),
          "both draw calls use Cr10T",
          str([d["texture_name"] for d in mesh["draw_calls"]]))
    check(len(mesh["warnings"]) == 0, "no mesh warnings",
          str(mesh["warnings"]))

    # ------------------------------------------------------------------
    print("BCH0G/BCH0V (single frame):")
    one_geo = tgeo.parse_tgeo(arc.entry("BCH0G"))
    one_frames = svtx.parse_svtx(arc.entry("BCH0V"))
    check(len(one_frames) == 1, "1 frame")
    one_mesh = tgeo.build_mesh(one_geo, one_frames)
    check(one_mesh["tri_count"] == 192, "192 triangles",
          str(one_mesh["tri_count"]))
    check_indices(one_mesh, "BCH0G")

    # ------------------------------------------------------------------
    print("Cr10G/Cr10V (compressed, many frames):")
    c_geo = tgeo.parse_tgeo(arc.entry("Cr10G"))
    c_frames = svtx.parse_svtx(arc.entry("Cr10V"))
    check(len(c_frames) == 11, "11 frames", str(len(c_frames)))
    check(len(c_geo.compression) == 305, "305 compression instructions",
          str(len(c_geo.compression)))
    c_mesh = tgeo.build_mesh(c_geo, c_frames)
    check(c_mesh["tri_count"] == 574, "574 triangles",
          str(c_mesh["tri_count"]))
    check(c_mesh["frame_count"] == 11, "mesh carries 11 frames")
    check_indices(c_mesh, "Cr10G")
    check(all(len(v["frame_positions"]) == 11 for v in c_mesh["vertices"][:20]),
          "vertices carry 11 frame positions")

    # ------------------------------------------------------------------
    print("Sweep: every TGEO in 0x0B with a same-name SVTX:")
    ok = 0
    missing = 0
    failed = []
    under = []
    for entry in sorted(arc.of_type(2), key=lambda e: e.name):
        vname = entry.name[:-1] + "V"
        ventry = arc.entry(vname)
        if ventry is None:
            missing += 1
            continue
        try:
            g = tgeo.parse_tgeo(entry)
            v = svtx.parse_svtx(ventry)
            m = tgeo.build_mesh(g, v)
            # The reference allocates for header tri_count but writes only the
            # triangles described by the flag stream, so <= is expected; a
            # few entries over-count by 1..5 (documented in docs/assets/TGEO.md).
            if m["tri_count"] > g.tri_count:
                raise ValueError("tri_count %d > header %d" %
                                 (m["tri_count"], g.tri_count))
            if m["tri_count"] != g.tri_count:
                under.append((entry.name, g.tri_count, m["tri_count"]))
            if not m["indices"] or max(m["indices"]) >= m["vertex_count"]:
                raise ValueError("bad indices")
            ok += 1
        except Exception as exc:
            failed.append((entry.name, exc))
            traceback.print_exc()
    print("  decoded %d model(s); %d TGEO have no same-name SVTX; %d failed"
          % (ok, missing, len(failed)))
    if under:
        print("  header tri_count > decoded (reference also tolerates this):")
        for name, header, decoded in under:
            print("    %-6s header=%d decoded=%d" % (name, header, decoded))
    check(ok >= 50, "at least 50 models decode", "%d" % ok)
    check(not failed, "no paired model raised",
          ", ".join(n for n, _ in failed))
    check(len(arc.of_type(2)) == ok + missing + len(failed),
          "sweep accounts for every TGEO",
          "%d = %d + %d + %d" % (len(arc.of_type(2)), ok, missing,
                                 len(failed)))

    print()
    if FAILS:
        print("FAILED (%d):" % len(FAILS))
        for f in FAILS:
            print("  -", f)
        return 1
    print("ALL CHECKS PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
