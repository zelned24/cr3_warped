"""Export every resolvable TGEO/SVTX pair from all supplied level archives.

Exports original graphics only, not object placement, scripts or gameplay.
The manifest records missing references and decoder warnings explicitly.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw

import cb3
import svtx
import tgeo
import tpag
import zdat
from graphics_texture_pool import build_pool, resolve as resolve_texture

ROOT = Path(__file__).resolve().parents[1]
TEXTURE_CACHE = {}


def decoder_fingerprint():
    """Do not silently reuse model files written by an older decoder."""
    digest = hashlib.sha256()
    for name in ("export_object_graphics.py", "tgeo.py", "svtx.py", "tpag.py",
                 "graphics_texture_pool.py"):
        raw = Path(__file__).with_name(name).read_bytes()
        digest.update(name.encode("ascii") + struct.pack("<I", len(raw)) + raw)
    return digest.hexdigest()


def build_entry_pool():
    pool = {}
    for level_id in cb3.all_level_ids():
        archive = cb3.load_level(level_id, cache=False)
        for entry in archive.entries:
            if entry.type not in (1,2):
                continue
            identity = digest_items(entry)
            item = pool.setdefault((entry.name,entry.type),{}).setdefault(identity,
                {"entry":entry,"source_levels":[]})
            item["source_levels"].append(level_id)
    return pool


def resolve_entry(pool, archive, name, expected_type):
    local = archive.entry(name)
    if local is not None:
        if local.type != expected_type:
            return None,{"resolution":"wrong_local_type","source_level":archive.level_id}
        return local,{"resolution":"local_archive","source_level":archive.level_id,
                      "source_levels":[archive.level_id],"sha256":digest_items(local)}
    variants = pool.get((name,expected_type),{})
    if len(variants) == 1:
        identity,item = next(iter(variants.items()))
        return item["entry"],{"resolution":"unique_global_payload","source_level":item["source_levels"][0],
                              "source_levels":item["source_levels"],"sha256":identity}
    return None,{"resolution":"absent_named_entry" if not variants else "ambiguous_global_name",
                 "source_level":None,"distinct_payloads":len(variants)}


def digest_items(entry):
    digest = hashlib.sha256()
    for item in entry.items:
        digest.update(struct.pack("<I", len(item)))
        digest.update(item)
    return digest.hexdigest()


def discover_pairs(archive):
    """Return explicit GOOL pairs plus conventional same-stem pairs."""
    pairs = {}
    issues = []
    declarations = []
    for entry in archive.of_type(11):
        try:
            # Read only the bounded model item. In particular QUAD_LIST rects
            # is a u32 at +8, not the texture-EID halfword at +4.
            raw = entry.items[5] if len(entry.items) > 5 else b""
            offset = 0
            import nsf
            while offset < len(raw):
                kind = raw[offset]
                if kind == 1:
                    size = 20
                    if offset + size > len(raw):
                        raise ValueError("truncated MESH at %d" % offset)
                    geo, vtx = struct.unpack_from("<II", raw, offset + 12)
                    pairs.setdefault((nsf.eid_str(geo), nsf.eid_str(vtx)), set()).add(entry.name)
                elif kind in (2, 3, 5):
                    if offset + (12 if kind == 5 else 8) > len(raw):
                        raise ValueError("truncated graphics record at %d" % offset)
                    count = struct.unpack_from("<H", raw, offset + 2)[0]
                    rects = struct.unpack_from("<I", raw, offset + 8)[0] if kind == 5 else 1
                    size = (12 + 24 * count * rects) if kind == 5 else 8 + (16 if kind == 2 else 20) * count
                    if offset + size > len(raw):
                        raise ValueError("graphics record type%d at%d exceeds item: count%d rects%d" % (kind, offset, count, rects))
                    declarations.append({"gool":entry.name,"offset":offset,"kind":kind,
                                         "frames":count,"rectangles":rects,
                                         "texture":nsf.eid_str(struct.unpack_from("<I",raw,offset+4)[0]),
                                         "status":"declared; not a TGEO mesh"})
                else:
                    raise ValueError("unknown model kind %d at %d" % (kind, offset))
                offset += size
        except (ValueError, IndexError, struct.error) as error:
            issues.append({"gool": entry.name, "error": str(error)})
    for geo in archive.of_type(2):
        candidate = geo.name[:-1] + "V"
        if archive.entry(candidate) is not None:
            pairs.setdefault((geo.name, candidate), set()).add("same-stem")
    return pairs, issues, declarations


def preview(mesh, textures, size=112):
    """Two-sided orthographic frame-zero rasterizer with original UV/RGB."""
    positions = np.array([v["pos"] for v in mesh["vertices"]], dtype=np.float64)
    # A fixed three-quarter camera, fit independently to each model.
    basis = np.array([[.70710678, 0, -.70710678],
                      [-.30151134, .90453403, -.30151134],
                      [.63960215, .42640143, .63960215]])
    projected = positions @ basis.T
    lo, hi = projected.min(axis=0), projected.max(axis=0)
    scale = (size - 12) / max(float(np.max(hi[:2] - lo[:2])), 1e-6)
    projected[:, :2] = (projected[:, :2] - (lo[:2] + hi[:2]) / 2) * scale
    projected[:, 0] += size / 2
    projected[:, 1] = size / 2 - projected[:, 1]
    pixels = np.full((size, size, 4), (20, 25, 35, 255), dtype=np.uint8)
    depth = np.full((size, size), -np.inf)
    for tri in mesh["triangles"]:
        ids = tri["indices"]
        p = projected[ids]
        x0, y0 = np.maximum(np.floor(p[:, :2].min(axis=0)).astype(int), 0)
        x1, y1 = np.minimum(np.ceil(p[:, :2].max(axis=0)).astype(int), size - 1)
        if x0 > x1 or y0 > y1:
            continue
        a, b, c = p
        det = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(det) < 1e-9:
            continue
        yy, xx = np.mgrid[y0:y1 + 1, x0:x1 + 1]
        w0 = ((b[1] - c[1]) * (xx + .5 - c[0]) + (c[0] - b[0]) * (yy + .5 - c[1])) / det
        w1 = ((c[1] - a[1]) * (xx + .5 - c[0]) + (a[0] - c[0]) * (yy + .5 - c[1])) / det
        w2 = 1 - w0 - w1
        z = w0 * a[2] + w1 * b[2] + w2 * c[2]
        target = depth[y0:y1 + 1, x0:x1 + 1]
        valid = (w0 >= -1e-6) & (w1 >= -1e-6) & (w2 >= -1e-6) & (z > target)
        weights = np.stack((w0, w1, w2), axis=-1)
        colors = weights @ np.array([mesh["vertices"][i]["color"] for i in ids])
        rgba = np.empty((*z.shape, 4), dtype=np.uint8)
        rgba[:, :, :3] = np.clip(colors * 255, 0, 255).astype(np.uint8)
        rgba[:, :, 3] = 255
        texture = textures.get(tri["uv_index"]) if not tri["tex_anim"] else None
        if texture is not None:
            image, bounds = texture
            uv = weights @ np.array([mesh["vertices"][i]["uv"] for i in ids])
            u = np.clip(np.nan_to_num(np.rint(uv[:, :, 0] - bounds[0])), 0, image.shape[1] - 1).astype(int)
            v = np.clip(np.nan_to_num(np.rint(uv[:, :, 1] - bounds[2])), 0, image.shape[0] - 1).astype(int)
            texels = image[v, u]
            rgba[:, :, :3] = np.clip(texels[:, :, :3] * colors * 2, 0, 255).astype(np.uint8)
            valid &= texels[:, :, 3] != 0
        target[valid] = z[valid]
        pixels[y0:y1 + 1, x0:x1 + 1][valid] = rgba[valid]
    return Image.fromarray(pixels)


def export_pair(archive, geo_entry, vtx_entry, output, key, page_pool=None):
    folder = output / "models" / key
    folder.mkdir(parents=True, exist_ok=True)
    geo = tgeo.parse_tgeo(geo_entry)
    mesh = tgeo.build_mesh(geo, svtx.parse_svtx(vtx_entry))
    regions = []
    texture_pixels = {}
    for index, uv in enumerate(geo.uvs):
        record = {"uv_index": index, **uv.as_dict()}
        page_name = geo.texture_names[uv.tex_index] if uv.tex_index < len(geo.texture_names) else None
        record["page"] = page_name
        page_data, provenance = resolve_texture(page_pool or {},archive,page_name)
        record["texture_provenance"] = provenance
        if page_data is None:
            record["error"] = provenance["resolution"]
        else:
            try:
                # Animation clips share hundreds of UV regions. Decode their
                # identical page/crop once instead of once per mesh clip.
                texture_key = (str(output.resolve()), provenance["sha256"],uv.tex_page & ~0x1C00,uv.clut,
                               uv.min_u,uv.max_u,uv.min_v,uv.max_v)
                cached = TEXTURE_CACHE.get(texture_key)
                if cached is None or not cached[1].is_file():
                    image = tpag.decode_texture(page_data, uv.as_dict())
                    content_hash = hashlib.sha256(struct.pack("<II", image.width, image.height) + image.rgba).hexdigest()
                    texture_path = output / "textures" / (content_hash + ".png")
                    if not texture_path.exists():
                        Image.frombytes("RGBA", (image.width, image.height), image.rgba).save(texture_path)
                    TEXTURE_CACHE[texture_key] = image, texture_path
                else:
                    image, texture_path = cached
                record.update({"file": "textures/" + texture_path.name, "width": image.width,
                               "height": image.height, "blend": image.blend})
                texture_pixels[index] = (np.frombuffer(image.rgba, dtype=np.uint8).reshape(image.height, image.width, 4), image.bounds)
            except (ValueError, IndexError, struct.error) as error:
                record["error"] = str(error)
        regions.append(record)
    vertices = mesh["vertices"]
    with (folder / "frames.bin").open("wb") as stream:
        stream.write(struct.pack("<4sIII", b"CBOF", 1, mesh["frame_count"], mesh["vertex_count"]))
        for frame in range(mesh["frame_count"]):
            for vertex in vertices:
                stream.write(struct.pack("<3f", *vertex["frame_positions"][frame]))
    preview(mesh, texture_pixels).save(folder / "preview.png")
    for vertex in vertices:
        del vertex["frame_positions"]
    mesh.update({"schema": "cbwarped-object/1", "geo": geo_entry.name, "svtx": vtx_entry.name,
                 "frames_file": "frames.bin", "texture_regions": regions,
                 "texture_animations": [anim.as_dict() for anim in geo.tex_anims],
                 "original_triangle_count": geo.tri_count})
    (folder / "mesh.json").write_text(json.dumps(mesh, separators=(",", ":")), encoding="utf-8")
    return {"path": "models/" + key, "geo": geo_entry.name, "svtx": vtx_entry.name,
            "frames": mesh["frame_count"], "corners": mesh["vertex_count"],
            "triangles": mesh["tri_count"], "original_triangle_count": geo.tri_count,
            "warnings": sorted(set(mesh["warnings"])),
            "texture_errors": [region for region in regions if "error" in region],
            "animated_texture_triangles": sum(tri["tex_anim"] for tri in mesh["triangles"])}


def export_all(output, contacts):
    output.mkdir(parents=True, exist_ok=True)
    (output / "textures").mkdir(exist_ok=True)
    contacts.mkdir(parents=True, exist_ok=True)
    fingerprint = decoder_fingerprint()
    manifest = {"schema": "cbwarped-object-inventory/1", "disc": "SCUS-94244",
                "decoder_fingerprint": fingerprint,
                "runtime_integrated": False, "levels": [], "models": {}}
    entry_pool = build_entry_pool()
    page_pool = build_pool()
    # Resume completed byte-identical models after interruption. Every archive
    # is rescanned, so new pairing fixes and coverage records are never skipped.
    previous = output / "manifest.json"
    if previous.exists():
        stored = json.loads(previous.read_text(encoding="utf-8"))
        if stored.get("decoder_fingerprint") == fingerprint:
            manifest["models"] = {identity: model for identity, model in stored.get("models", {}).items()
                if (output / model["path"] / "mesh.json").exists() and
                   (output / model["path"] / "frames.bin").exists()}
    for level_id in cb3.all_level_ids():
        archive = cb3.load_level(level_id, cache=False)
        pairs, issues, declarations = discover_pairs(archive)
        level = {"id": level_id, "tgeo_count": len(archive.of_type(2)), "svtx_count": len(archive.of_type(1)),
                 "gool_count": len(archive.of_type(11)), "pairs": [], "unresolved": [], "gool_errors": issues,
                 "non_mesh_declarations": declarations}
        rendered_geos = set()
        thumbnail_geos = set()
        thumbs = []
        for (geo_name, vtx_name), sources in sorted(pairs.items()):
            reference = {"geo": geo_name, "svtx": vtx_name, "sources": sorted(sources)}
            geo_entry, geo_provenance = resolve_entry(entry_pool,archive,geo_name,2)
            vtx_entry, vtx_provenance = resolve_entry(entry_pool,archive,vtx_name,1)
            reference.update({"geo_provenance":geo_provenance,"svtx_provenance":vtx_provenance})
            if geo_entry is None or vtx_entry is None:
                reference["reason"] = "TGEO/SVTX reference absent or wrong type"
                level["unresolved"].append(reference)
                continue
            # Only actual TGEO page dependencies belong in the identity. World
            # pages elsewhere in an archive must not duplicate a shared object.
            geo_header = tgeo.parse_tgeo(geo_entry)
            dependency_hashes = []
            for page_name in geo_header.texture_names:
                page_data, provenance = resolve_texture(page_pool,archive,page_name)
                dependency_hashes.append(page_name + ":" +
                    (hashlib.sha256(page_data).hexdigest() if page_data else "missing"))
            identity = hashlib.sha256((digest_items(geo_entry) + digest_items(vtx_entry) +
                "".join(dependency_hashes)).encode()).hexdigest()
            try:
                if identity not in manifest["models"]:
                    manifest["models"][identity] = export_pair(archive, geo_entry, vtx_entry, output, identity,page_pool)
                reference["asset"] = identity
                level["pairs"].append(reference)
                if geo_name not in thumbnail_geos:
                    thumbs.append((geo_name, vtx_name, output / manifest["models"][identity]["path"] / "preview.png"))
                    thumbnail_geos.add(geo_name)
                if archive.entry(geo_name) is not None:
                    rendered_geos.add(geo_name)
            except (ValueError, IndexError, struct.error) as error:
                reference["reason"] = str(error)
                level["unresolved"].append(reference)
        level["tgeo_without_pair"] = sorted(entry.name for entry in archive.of_type(2) if entry.name not in rendered_geos)
        if thumbs:
            columns = 8
            sheet = Image.new("RGB", (columns * 128, ((len(thumbs) + columns - 1) // columns) * 140), (12, 16, 24))
            draw = ImageDraw.Draw(sheet)
            for i, (geo_name, vtx_name, path) in enumerate(thumbs):
                x, y = (i % columns) * 128, (i // columns) * 140
                with Image.open(path) as image:
                    sheet.paste(image.convert("RGB"), (x + 8, y))
                draw.text((x + 4, y + 113), geo_name + "/" + vtx_name, fill=(225, 225, 225))
            sheet.save(contacts / ("objects_%02X.png" % level_id))
        manifest["levels"].append(level)
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
        print("level %02X: %d pairs exported, %d unresolved, %d TGEO without pair" %
              (level_id, len(level["pairs"]), len(level["unresolved"]), len(level["tgeo_without_pair"])), flush=True)
    manifest["totals"] = {"archives": len(manifest["levels"]), "unique_assets": len(manifest["models"]),
        "pair_occurrences": sum(len(level["pairs"]) for level in manifest["levels"]),
        "unresolved_pair_occurrences": sum(len(level["unresolved"]) for level in manifest["levels"]),
        "tgeo_occurrences": sum(level["tgeo_count"] for level in manifest["levels"]),
        "tgeo_without_pair_occurrences": sum(len(level["tgeo_without_pair"]) for level in manifest["levels"]),
        "texture_pngs": len(list((output / "textures").glob("*.png")))}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(json.dumps(manifest["totals"], indent=2))
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "game_data/objects")
    parser.add_argument("--contacts", type=Path, default=ROOT / "capturas/objects")
    args = parser.parse_args()
    export_all(args.out, args.contacts)


if __name__ == "__main__":
    main()
