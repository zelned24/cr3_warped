"""Build a scene graph for one Crash Bandicoot: Warped level.

Usage:
    python tools/scenegraph.py --level 0x0B --out tools/scene_dump

Output (``<out>/scene_<id>.json``)::

    {
      "schema": "cbwarped-scene/1",
      "source": {"disc": "SCUS-94244", "level": "0x0B", "archive": "..."},
      "counts": {...},
      "texture_pages": ["Md1bT", ...],
      "wgeos":   [ {name, origin, uv, tex_regions, draw_calls, mesh} ],
      "sections":[ {name, type: "SLST"} ],
      "zdats":   [ {name, zone_index, world_models, databases, entities} ],
      "resolution": {...}
    }

Every entity carries a ``refs`` list where each referenced Naughty Dog name is
classified as GOOL / TGEO / SVTX / WGEO / TPAG and marked ``found``.  The
COORDINATE note is important: WGEO ``build_mesh`` output is reference space
(Y up), while entity ``positions`` follow the reference ZDAT formula and the
mapping between the two spaces is still TODO (docs/assets/ZDAT.md).
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cb3
import wgeo
import zdat

DISC = "SCUS-94244"
SCHEMA = "cbwarped-scene/1"

TYPE_NAMES = {
    1: "SVTX", 2: "TGEO", 3: "WGEO", 4: "SLST", 5: "TPAG", 6: "LDAT",
    7: "ZDAT", 8: "CPAT", 9: "BINF", 10: "OPAT", 11: "GOOL", 12: "ADIO",
    13: "MIDI", 14: "INST", 15: "VCOL", 16: "LINK", 17: "RAWD", 18: "IPAL",
    19: "PBAK", 20: "SDIO", 21: "VIDO",
}


def _ref(archive, name, expected_type=None):
    """Classify a Naughty Dog name against the archive entries.

    Texture pages (chunk type 1) are not NSF *entries* in this pipeline, so a
    TPAG reference is looked up in ``archive.texture()`` instead.
    """
    entry = archive.entry(name) if name else None
    found = entry is not None
    page = None
    if not found and expected_type == 5 and name:
        page = archive.texture(name)
        found = page is not None
    ref = {
        "name": name,
        "type": (TYPE_NAMES.get(entry.type, "TYPE_%d" % entry.type)
                 if entry is not None else ("TPAG" if page is not None else None)),
        "eid": ("0x%08X" % entry.eid) if entry is not None else None,
        "found": found,
    }
    if page is not None:
        ref["page_index"] = page.index
    if expected_type is not None:
        ref["expected"] = TYPE_NAMES.get(expected_type, expected_type)
        ref["type_ok"] = bool(found and (
            entry.type == expected_type if entry is not None
            else expected_type == 5))
    return ref


def _wgeo_dict(archive, w):
    mesh = w.build_mesh()
    textures_found = []
    for name in w.texture_names:
        page = archive.texture(name)
        textures_found.append({"name": name,
                               "found": page is not None,
                               "page_index": page.index if page else None})
    return {
        "name": w.name,
        "eid": "0x%08X" % w.eid,
        "is_skybox": w.is_skybox,
        "origin": list(w.origin),
        "origin_raw": list(w.origin_raw),
        "origin_divisor": w.scale,
        "counts": {
            "vertices": w.vertex_count,
            "triangles": w.triangle_count,
            "quads": w.quad_count,
            "uvs": len(w.uvs),
            "colors": w.color_count,
            "tex_anims": len(w.tex_anims),
            "texture_names": len(w.texture_names),
        },
        "texture_names": list(w.texture_names),
        "textures": textures_found,
        "uvs": [uv.as_dict() for uv in w.uvs],
        "tex_regions": w.tex_regions,
        "tex_anims": [a.as_dict() for a in w.tex_anims],
        "draw_calls": mesh["draw_calls"],
        "texture_pages": mesh["texture_pages"],
        "mesh": {
            "vertex_count": mesh["vertex_count"],
            "index_count": mesh["index_count"],
            "triangle_count": mesh["triangle_count"],
            "draw_call_count": len(mesh["draw_calls"]),
            "bounds_ref": list(w.bounds_ref()),
            "bounds_legacy": list(w.bounds()),
        },
    }


def _entity_refs(archive, ent, wgeo_names):
    refs = []
    if ent.gool:
        refs.append(_ref(archive, ent.gool, 11))
    model = ent.model
    if model is not None:
        if model.kind == "MESH":
            refs.append(_ref(archive, model.geo_name, 2))
            refs.append(_ref(archive, model.vtx_name, 1))
        elif model.tex_name:
            refs.append(_ref(archive, model.tex_name, 5))
    else:
        # ambiguous: report the candidates, deduplicated, so the runtime knows
        # what the behaviour can display.
        seen = set()
        for m in ent.model_options:
            names = []
            if m.kind == "MESH":
                names = [(m.geo_name, 2), (m.vtx_name, 1)]
            elif m.tex_name:
                names = [(m.tex_name, 5)]
            for name, etype in names:
                if not name or name in seen:
                    continue
                seen.add(name)
                refs.append(_ref(archive, name, etype))
    # a few ZDATs place a level section as an object; report it too
    if ent.file_name in wgeo_names:
        refs.append(_ref(archive, ent.file_name, 3))
    return refs


def build_scene(level_id):
    archive = cb3.load_level(level_id)
    counts = archive.counts()
    class_names = zdat.class_names_from_nsd(archive.nsd)
    zdats = zdat.parse_zdat_archive(archive)
    wgeos = [wgeo.Wgeo(e) for e in archive.of_type(3)]
    wgeo_names = {w.name for w in wgeos}

    # texture pages actually referenced by the level geometry
    tex_names = set()
    for w in wgeos:
        tex_names.update(w.texture_names)
    for z in zdats:
        for ent in z.objects:
            for m in ent.model_options:
                if m.tex_name:
                    tex_names.add(m.tex_name)

    texture_pages = []
    for page in archive.texture_pages:
        texture_pages.append({
            "name": page.name,
            "cid": "0x%08X" % page.cid,
            "index": page.index,
            "size": len(page.data),
            "referenced": page.name in tex_names,
        })

    # zone-level WGEO references
    zdat_dicts = []
    zone_refs = []
    resolution = {
        "entities": 0, "gool_resolved": 0, "state_resolved": 0,
        "no_state": 0, "model_resolved": 0, "model_ambiguous": 0,
        "unresolved": 0, "refs_found": 0, "refs_missing": 0,
    }
    for z in zdats:
        d = z.as_dict()
        refs = {
            "sky_wgeo": _ref(archive, z.sky_wgeo, 3) if z.sky_wgeo else None,
            "world_models": [_ref(archive, name, 3) for name in z.world_models],
            "other_names": [_ref(archive, name, None) for name in z.other_names],
        }
        d["refs"] = refs
        for ent, ent_dict in zip(z.objects, d["entities"]):
            # keep the dump readable: a behaviour can carry 238 model records,
            # the count is preserved and the full table is in the GOOL entry.
            total_options = len(ent_dict["model_options"])
            ent_dict["model_options_total"] = total_options
            if total_options > 64:
                ent_dict["model_options"] = ent_dict["model_options"][:64]
                ent_dict["model_options_truncated"] = True
            erefs = _entity_refs(archive, ent, wgeo_names)
            ent_dict["refs"] = erefs
            for r in erefs:
                if r["found"]:
                    resolution["refs_found"] += 1
                else:
                    resolution["refs_missing"] += 1
        if z.resolution_summary:
            s = z.resolution_summary
            resolution["entities"] += s["entities"]
            resolution["gool_resolved"] += s["gool_resolved"]
            resolution["state_resolved"] += s["state_resolved"]
            resolution["no_state"] += s["no_state"]
            resolution["model_resolved"] += s["model_resolved"]
            resolution["unresolved"] += s["unresolved"]
            resolution["model_ambiguous"] += (s["state_resolved"] -
                                              s["model_resolved"])
        zdat_dicts.append(d)
        zone_refs.append({"zdat": z.name, **refs})

    slst = [{"name": e.name, "eid": "0x%08X" % e.eid,
             "item_sizes": [len(it) for it in e.items]}
            for e in archive.of_type(4)]
    rawd = [{"name": e.name, "eid": "0x%08X" % e.eid,
             "item_sizes": [len(it) for it in e.items]}
            for e in archive.of_type(17)]

    gools = [{"name": e.name, "eid": "0x%08X" % e.eid,
              "category": zdat.parse_gool_table(e).category,
              "state_count": len(zdat.parse_gool_table(e).states),
              "model_count": len(zdat.parse_gool_table(e).models),
              "object_index_count": len(zdat.parse_gool_table(e).object_indices)}
             for e in archive.of_type(11)]

    scene = {
        "schema": SCHEMA,
        "source": {
            "disc": DISC,
            "level": "0x%02X" % level_id,
            "archive": os.path.basename(archive.nsf_path),
            "nsd": os.path.basename(archive.nsd_path) if archive.nsd_path else None,
            "reference": "tools/reference/crashwarped_bin.ts (noclip.website, MIT)",
        },
        "counts": {
            "wgeo": len(wgeos),
            "zdat": len(zdats),
            "slst": len(slst),
            "gool": len(gools),
            "tgeo": len(archive.of_type(2)),
            "svtx": len(archive.of_type(1)),
            "rawd": len(rawd),
            "entities": resolution["entities"],
            "texture_pages": len(archive.texture_pages),
            "entry_types": {TYPE_NAMES.get(k, "TYPE_%d" % k): v
                            for k, v in sorted(counts.items())},
        },
        "coordinate_space": {
            "wgeo": "reference space: origin = raw/16, vertices 12-bit unsigned, Y up",
            "entity": "reference ZDAT formula: (offset_vector + path * 2^shift) * 0x100",
            "note": "mapping entity positions to WGEO space is TODO/UNVERIFIED (ZDAT.md)",
        },
        "texture_pages": texture_pages,
        "wgeos": [_wgeo_dict(archive, w) for w in wgeos],
        "sections": slst,
        "rawd": rawd,
        "checkpoints": zdat.parse_checkpoints(archive.nsd),
        "gools": gools,
        "zones": zone_refs,
        "zdats": zdat_dicts,
        "resolution": resolution,
    }
    return scene


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--level", default="0x0B",
                    help="level id (hex or decimal, default 0x0B)")
    ap.add_argument("--out", default=os.path.join("tools", "scene_dump"),
                    help="output directory (default tools/scene_dump)")
    ap.add_argument("--indent", type=int, default=1)
    args = ap.parse_args(argv)

    level_id = int(args.level, 0)
    scene = build_scene(level_id)
    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, "scene_%02X.json" % level_id)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(scene, f, indent=args.indent, sort_keys=False)
        f.write("\n")
    c = scene["counts"]
    r = scene["resolution"]
    print("%s  WGEO=%d ZDAT=%d SLST=%d GOOL=%d entities=%d"
          % (path, c["wgeo"], c["zdat"], c["slst"], c["gool"], c["entities"]))
    print("  entities: gool=%d state=%d model=%d ambiguous=%d no_state=%d unresolved=%d"
          % (r["gool_resolved"], r["state_resolved"], r["model_resolved"],
             r["model_ambiguous"], r["no_state"], r["unresolved"]))
    print("  refs: found=%d missing=%d ; %d bytes"
          % (r["refs_found"], r["refs_missing"], os.path.getsize(path)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
