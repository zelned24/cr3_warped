"""Validation for the WGEO/ZDAT/scene pipeline.

Run:  python tools/test_scene.py

Checks (control levels):
  * 0x0B Toad Village  : 10 WGEO, 32 ZDAT, 38 SLST, 15 GOOL, 198 entities
  * 0x13 Bye Bye Blimps: 0 WGEO, 1 ZDAT, 0 SLST, 1 RAWD, 49 entities

The test is plain Python (no pytest dependency) and exits non-zero on the
first failure.  It also imports `export_levels` and builds level 0x0B to prove
the extended `wgeo.Wgeo` API did not break the existing exporter.
"""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cb3
import scenegraph
import wgeo
import zdat

FAILS = []


def check(cond, message):
    print(("  ok   " if cond else "  FAIL ") + message)
    if not cond:
        FAILS.append(message)


def section(title):
    print("\n== %s ==" % title)


def _agg_resolution(zdats):
    agg = {"entities": 0, "gool_resolved": 0, "state_resolved": 0,
           "no_state": 0, "model_resolved": 0, "unresolved": 0}
    for z in zdats:
        for k, v in z.resolution_summary.items():
            agg[k] = agg.get(k, 0) + v
    return agg


def validate_level(level_id, expected, entity_count):
    arc = cb3.load_level(level_id)
    section("level 0x%02X %s" % (level_id, os.path.basename(arc.nsf_path)))
    counts = arc.counts()

    for etype, label in ((3, "WGEO"), (7, "ZDAT"), (4, "SLST"), (11, "GOOL"),
                         (17, "RAWD"), (2, "TGEO"), (1, "SVTX")):
        if label in expected:
            got = counts.get(etype, 0)
            check(got == expected[label],
                  "%s count %d == %d" % (label, got, expected[label]))

    # ---------------- WGEO -------------------------------------------------
    wgeos = [wgeo.Wgeo(e) for e in arc.of_type(3)]
    check(len(wgeos) == counts.get(3, 0), "every WGEO decodes")
    total_dc = 0
    problems = []
    for w in wgeos:
        mesh = w.build_mesh()
        prims = w.triangle_count + w.quad_count * 2
        if mesh["index_count"] != prims * 3:
            problems.append("%s index count" % w.name)
        if not mesh["indices"] or max(mesh["indices"]) >= mesh["vertex_count"]:
            problems.append("%s indices out of range" % w.name)
        if sum(dc["index_count"] for dc in mesh["draw_calls"]) != mesh["index_count"]:
            problems.append("%s draw call coverage" % w.name)
        if len(mesh["indices"]) % 3 != 0:
            problems.append("%s index list not triangles" % w.name)
        if not mesh["draw_calls"]:
            problems.append("%s no draw calls" % w.name)
        total_dc += len(mesh["draw_calls"])

        if len(w.uvs) != w.item4_count:
            problems.append("%s UV count" % w.name)
        if not all(0 <= c <= 255 for uv in w.uvs
                   for c in list(uv.us) + list(uv.vs)):
            problems.append("%s UV range" % w.name)
        if not all(uv.texture_index < len(w.texture_names) for uv in w.uvs):
            problems.append("%s UV texture index" % w.name)
        if not all(arc.texture(n) is not None for n in w.texture_names):
            problems.append("%s texture name -> TPAG" % w.name)
        if not w.tex_regions:
            problems.append("%s no texture regions" % w.name)
        for reg in w.tex_regions:
            if not (0 <= reg["min_u"] <= reg["max_u"] <= 255 and
                    0 <= reg["min_v"] <= reg["max_v"] <= 255):
                problems.append("%s texture region bounds" % w.name)
        if len(w.tex_anims) != w.item6_count:
            problems.append("%s animation count" % w.name)

        # legacy API used by export_levels.py
        if not (len(w.vertices) == w.vertex_count and
                len(w.triangles) == w.triangle_count and
                len(w.quads) == w.quad_count):
            problems.append("%s legacy arrays" % w.name)
        if not (all(len(v) == 5 for v in w.vertices) and
                all(len(t) == 5 for t in w.triangles) and
                all(len(q) == 6 for q in w.quads)):
            problems.append("%s legacy tuple shapes" % w.name)

    check(not problems, "WGEO mesh/UV/texture checks (%d models, %d draw calls)"
          % (len(wgeos), total_dc))
    for p in problems[:10]:
        print("       -", p)

    # ---------------- ZDAT -------------------------------------------------
    class_names = zdat.class_names_from_nsd(arc.nsd)
    check(len(class_names) == 0x80, "NSD classNameList has 128 entries")
    zdats = zdat.parse_zdat_archive(arc)
    check(len(zdats) == counts.get(7, 0), "every ZDAT decodes")

    entities = 0
    named = 0
    problems = []
    for z in zdats:
        if len(z.databases) != z.db_count:
            problems.append("%s db count" % z.name)
        if len(z.objects) != z.object_count:
            problems.append("%s entity count" % z.name)
        for db in z.databases:
            if not db.series:
                problems.append("%s db %d empty" % (z.name, db.index))
            for s in db.series:
                if s.count != len(s.values):
                    problems.append("%s db %d series %04X"
                                    % (z.name, db.index, s.key))

        for ent in z.objects:
            entities += 1
            if ent.name:
                named += 1
            if not ent.path:
                problems.append("%s entity %d no path" % (z.name, ent.index))
            if not all(math.isfinite(c) for p in ent.positions for c in p):
                problems.append("%s entity %d non-finite" % (z.name, ent.index))
            if not ent.gool_found:
                problems.append("%s entity %d GOOL" % (z.name, ent.index))
            if ent.state is None and ent.resolution != "no_state":
                problems.append("%s entity %d state" % (z.name, ent.index))
            if ent.model is not None:
                if arc.entry(ent.model.geo_name) is None:
                    problems.append("%s entity %d TGEO" % (z.name, ent.index))
                if arc.entry(ent.model.vtx_name) is None:
                    problems.append("%s entity %d SVTX" % (z.name, ent.index))

    check(entities == entity_count,
          "entity count %d == %d" % (entities, entity_count))
    check(not problems, "entity/DB checks (%d named, %d databases)"
          % (named, sum(z.db_count for z in zdats)))
    for p in problems[:10]:
        print("       -", p)

    agg = _agg_resolution(zdats)
    check(agg["entities"] == entities, "resolution summary covers all entities")
    check(agg["gool_resolved"] == entities,
          "GOOL resolved for %d/%d entities" % (agg["gool_resolved"], entities))
    check(agg["state_resolved"] + agg["no_state"] == entities,
          "state resolved for %d + %d no_state = %d"
          % (agg["state_resolved"], agg["no_state"], entities))
    check(agg["unresolved"] == 0, "0 unresolved entities")
    print("  info model resolved for %d/%d entities "
          "(rest are script-selected, reported as ambiguous)"
          % (agg["model_resolved"], entities))

    # ---------------- scene graph -----------------------------------------
    scene = scenegraph.build_scene(level_id)
    check(scene["counts"]["wgeo"] == counts.get(3, 0), "scene WGEO count")
    check(scene["counts"]["zdat"] == counts.get(7, 0), "scene ZDAT count")
    check(scene["counts"]["entities"] == entities, "scene entity count")
    check(len(scene["zdats"]) == len(zdats), "scene dumps every ZDAT")
    check(len(scene["texture_pages"]) == len(arc.texture_pages),
          "scene lists all texture pages")
    check(len(scene["sections"]) == counts.get(4, 0), "scene lists every SLST")

    refs_total = 0
    missing = 0
    problems = []
    for zd in scene["zdats"]:
        for ent in zd["entities"]:
            if "refs" not in ent:
                problems.append("%s entity %d refs" % (zd["name"], ent["index"]))
            if ent["position"] is not None and not all(
                    math.isfinite(c) for c in ent["position"]):
                problems.append("%s entity %d position"
                                % (zd["name"], ent["index"]))
            for r in ent.get("refs", []):
                refs_total += 1
                if not r["found"]:
                    missing += 1
    check(not problems, "scene entity refs/positions valid")
    for p in problems[:10]:
        print("       -", p)
    print("  info scene refs: %d classified, %d external/missing "
          "(names not stored in this level archive)" % (refs_total, missing))

    return scene, agg


def validate_export_level():
    section("export_levels.py compatibility")
    import export_levels
    target = [row for row in export_levels.LEVELS if row[0] == 0x0B]
    check(bool(target), "level 0x0B listed by export_levels")
    if not target:
        return
    level_id, folder, stem = target[0]
    res = export_levels.build_level(level_id, folder, stem)
    check(res is not None, "build_level(0x0B) returns data")
    if res is None:
        return
    data, models, nsd = res
    check(data[:4] == b"CBW3", "CBW3 magic")
    check(len(models) == 10, "10 exported world models")
    lv, nmodels, palette, _ = struct.unpack_from("<4I", data, 4)
    check(lv == 0x0B and nmodels == 10, "exported header level/models")
    check(palette > 0, "exported palette non-empty")
    check(len(data) > 1024, "exported blob non-trivial (%d bytes)" % len(data))


def main():
    _, agg = validate_level(0x0B, {
        "WGEO": 10, "ZDAT": 32, "SLST": 38, "GOOL": 15,
        "TGEO": 106, "SVTX": 120}, 198)
    validate_level(0x13, {
        "WGEO": 0, "ZDAT": 1, "SLST": 0, "RAWD": 1}, 49)
    validate_export_level()

    section("summary")
    print("  0x0B: 10 WGEO / 32 ZDAT / 38 SLST / %d entities; "
          "GOOL=%d state=%d no_state=%d model=%d unresolved=%d"
          % (agg["entities"], agg["gool_resolved"], agg["state_resolved"],
             agg["no_state"], agg["model_resolved"], agg["unresolved"]))

    if FAILS:
        print("\n%d FAILURE(S)" % len(FAILS))
        for f in FAILS[:20]:
            print("  -", f)
        return 1
    print("\nALL CHECKS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
