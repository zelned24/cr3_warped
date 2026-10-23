"""Export retail WGEO materials into bounded, GPU-ready CWT1 level files.

Colors, UVs, CLUT pixels and frame-zero texture animation come from the disc.
RGBA5551 uses PICA channel order (R15..11,G10..6,B5..1,A0), not PSX word order.
Run --level 0x0B --preview to also write source-crop PNGs and a CPU render.
"""
import argparse
import json
import math
import struct
from pathlib import Path

import cb3
import tpag
import wgeo
from dump_rawd import write_png

ROOT = Path(__file__).resolve().parents[1]
MAX_FILE = 16 * 1024 * 1024
MAX_GEOMETRY = 8 * 1024 * 1024
MAX_TEXTURES = 4 * 1024 * 1024


def align(n, unit=16):
    return (n + unit - 1) & ~(unit - 1)


def pot(n):
    return max(8, 1 << (n - 1).bit_length())


def morton(x, y):
    return ((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) |
            ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3))


def tile5551(rgba, width, height, padded_width, padded_height):
    """Top-down RGBA -> bottom-up PICA tiled 5551; edge-pad the POT rectangle."""
    out = bytearray(padded_width * padded_height * 2)
    for y in range(padded_height):
        source_y = min(padded_height - 1 - y, height - 1)
        for x in range(padded_width):
            source_x = min(x, width - 1)
            p = 4 * (source_y * width + source_x)
            r, g, b, a = rgba[p:p + 4]
            word = ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | bool(a)
            index = ((y >> 3) * (padded_width >> 3) + (x >> 3)) * 64 + morton(x & 7, y & 7)
            struct.pack_into("<H", out, index * 2, word)
    return bytes(out)


def untile5551(payload, width, height):
    """Independent inverse used by the CPU verifier and preview sampler."""
    out = bytearray(width * height * 4)
    for y in range(height):
        gy = height - 1 - y
        for x in range(width):
            address = ((gy // 8) * (width // 8) + x // 8) * 64
            for bit in range(3):
                address += ((x >> bit) & 1) << (2 * bit)
                address += ((gy >> bit) & 1) << (2 * bit + 1)
            raw = struct.unpack_from("<H", payload, 2 * address)[0]
            channels = ((raw >> 11) & 31, (raw >> 6) & 31, (raw >> 1) & 31)
            rgba = tuple((v << 3) | (v >> 2) for v in channels) + (255 if raw & 1 else 0,)
            out[4 * (y * width + x):4 * (y * width + x) + 4] = bytes(rgba)
    return bytes(out)


def build_level(level_id, preview_dir=None):
    arc = cb3.load_level(level_id, cache=False)
    worlds = [wgeo.Wgeo(e) for e in arc.of_type(3)]
    if not worlds:
        return None
    # A white material allows authored color-only primitives to share the shader.
    materials = {None: {"bounds": [0, 0, 0, 0], "image": None}}
    batches = []
    animation_count = 0
    for model_index, model in enumerate(worlds):
        mesh = model.build_mesh()
        for call in mesh["draw_calls"]:
            uv_frame = None
            key = None
            if call["tex_anim_index"] >= 0:
                animation_count += 1
                anim = model.tex_anims[call["tex_anim_index"]]
                frame = anim.start - 1
                if not 0 <= frame < len(model.uvs):
                    raise ValueError("%s invalid animation start %d" % (model.name, frame))
                uv_frame = model.uvs[frame]
                texture_name = model.texture_names[uv_frame.texture_index]
                key = (texture_name, uv_frame.tex_page & ~0x1C00, uv_frame.clut)
            elif call["texture_index"] >= 0:
                key = (call["texture_name"], call["page"] & ~0x1C00, call["clut"])
            indices = mesh["indices"][call["start_index"]:call["start_index"] + call["index_count"]]
            vertices = []
            for index in indices:
                v = mesh["vertices"][index]
                u, vv = v["u"], v["v"]
                if uv_frame is not None:
                    corner = int(u) + 2 * int(vv)
                    u, vv = uv_frame.us[corner], uv_frame.vs[corner]
                vertices.append((index, v, u, vv))
            if key is not None:
                us, vs = [v[2] for v in vertices], [v[3] for v in vertices]
                bounds = [min(us), max(us), min(vs), max(vs)]
                if key not in materials:
                    materials[key] = {"bounds": bounds}
                else:
                    old = materials[key]["bounds"]
                    materials[key]["bounds"] = [min(old[0], bounds[0]), max(old[1], bounds[1]),
                                                  min(old[2], bounds[2]), max(old[3], bounds[3])]
            batches.append((model_index, model, key, vertices))

    texture_meta = []
    texture_payloads = []
    texture_ids = {}
    for key, material in materials.items():
        if key is None:
            rgba, width, height, flags, blend = bytes((255, 255, 255, 255)), 1, 1, 0, 0
        else:
            name, page, clut = key
            source = arc.texture(name)
            if source is None:
                raise ValueError("missing referenced texture %s" % name)
            uv = tpag.make_uv(clut, page, material["bounds"])
            image = tpag.decode_texture(source.data, uv)
            rgba, width, height = image.rgba, image.width, image.height
            flags = bool(image.transparent_pixels) | (bool(image.xlu_pixels) << 1)
            blend = image.blend
        pw, ph = pot(width), pot(height)
        tiled = tile5551(rgba, width, height, pw, ph)
        material.update({"rgba": rgba, "width": width, "height": height,
                         "padded_width": pw, "padded_height": ph})
        texture_ids[key] = len(texture_meta)
        texture_meta.append([pw, ph, 0, len(tiled), flags, blend, 0, 0])
        texture_payloads.append(tiled)
        if preview_dir is not None and key is not None:
            name = "%s_%04x_%04x.png" % key
            write_png(str(preview_dir / name), width, height, rgba)
        # Byte-for-byte color/alpha verification after inverse GPU tiling.
        inverse = untile5551(tiled, pw, ph)
        for y in range(height):
            for x in range(width):
                a = 4 * (y * width + x)
                b = 4 * (y * pw + x)
                expected = bytes(((c >> 3) << 3) | (c >> 5) for c in rgba[a:a + 3])
                expected += bytes((255 if rgba[a + 3] else 0,))
                if inverse[b:b + 4] != expected:
                    raise AssertionError("GPU tile/CLUT mismatch %r %d,%d" % (key, x, y))

    vbo, ibo, batch_records = bytearray(), bytearray(), []
    for model_index, model, key, source_vertices in batches:
        material = materials[key]
        base, first = len(vbo) // 32, len(ibo) // 2
        remap = {}
        for index, vertex, u, v in source_vertices:
            identity = (index, u, v)
            if identity not in remap:
                remap[identity] = len(remap)
                min_u, _, min_v, _ = material["bounds"]
                uu = (u - min_u + 0.5) / material["padded_width"]
                vv = 1 - (v - min_v + 0.5) / material["padded_height"]
                position = [vertex[axis] + model.origin[i] for i, axis in enumerate(("x", "y", "z"))]
                # The shared shader doubles texture * vertex RGB, as required
                # by PSX textured modulation (0x80 is neutral). Color-only
                # triangles use a white dummy texture, so compensate there:
                # their authored RGB must survive without a second gain.
                color_gain = 1.0 if key is not None else 0.5
                color = [vertex[axis] / 255 * color_gain for axis in ("r", "g", "b")]
                vbo += struct.pack("<8f", *position, *color, uu, vv)
            if len(remap) > 65535:
                raise ValueError("one material batch exceeds 16-bit index range")
            ibo += struct.pack("<H", remap[identity])
        lo = model.bounds_ref()[:3]
        hi = model.bounds_ref()[3:]
        bounds = [lo[i] + model.origin[i] for i in range(3)] + [hi[i] + model.origin[i] for i in range(3)]
        batch_records.append(struct.pack("<8I6f", base, len(remap), first, len(source_vertices),
                                         texture_ids[key], int(model.is_skybox), model_index, 0, *bounds))
    if len(vbo) + len(ibo) > MAX_GEOMETRY or sum(map(len, texture_payloads)) > MAX_TEXTURES:
        raise ValueError("level exceeds 8 MiB geometry / 4 MiB texture budget")
    batch_offset = 64
    texture_offset = align(batch_offset + len(batch_records) * 56)
    vertex_offset = align(texture_offset + len(texture_meta) * 32)
    index_offset = align(vertex_offset + len(vbo))
    pixel_offset = align(index_offset + len(ibo))
    total = pixel_offset + sum(map(len, texture_payloads))
    if total > MAX_FILE:
        raise ValueError("level exceeds 16 MiB file budget")
    out = bytearray(total)
    out[:64] = struct.pack("<4s15I", b"CWT1", 1, level_id, len(vbo) // 32, len(ibo) // 2,
                           len(batch_records), len(texture_meta), vertex_offset, index_offset,
                           batch_offset, texture_offset, pixel_offset, total, 0, 0, 0)
    out[batch_offset:batch_offset + len(batch_records) * 56] = b"".join(batch_records)
    out[vertex_offset:vertex_offset + len(vbo)] = vbo
    out[index_offset:index_offset + len(ibo)] = ibo
    cursor = pixel_offset
    for i, (meta, pixels) in enumerate(zip(texture_meta, texture_payloads)):
        meta[2] = cursor
        struct.pack_into("<8I", out, texture_offset + i * 32, *meta)
        out[cursor:cursor + len(pixels)] = pixels
        cursor += len(pixels)
    report = {"level": level_id, "models": len(worlds), "batches": len(batches),
              "vertices": len(vbo) // 32, "indices": len(ibo) // 2, "textures": len(texture_meta),
              "geometry_bytes": len(vbo) + len(ibo), "texture_bytes": sum(map(len, texture_payloads)),
              "file_bytes": len(out), "animation_batches_frame0": animation_count,
              "semi_transparency": "nonzero texels opaque; ABR not implemented",
              "untextured_color": "half RGB with white material; shared TEV x2 preserves authored RGB",
              "pixel_roundtrip": "verified every crop texel after inverse PICA tiling"}
    return bytes(out), report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--level", type=lambda s: int(s, 0))
    parser.add_argument("--out", type=Path, default=ROOT / "game_data/world")
    parser.add_argument("--preview", action="store_true")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    reports = []
    levels = [args.level] if args.level is not None else cb3.all_level_ids()
    for level in levels:
        preview = args.out / ("preview_%02X" % level) if args.preview else None
        if preview is not None:
            preview.mkdir(exist_ok=True)
        result = build_level(level, preview)
        if result is None:
            continue
        data, report = result
        (args.out / ("lv_%02X.cwt" % level)).write_bytes(data)
        reports.append(report)
        print(json.dumps(report), flush=True)
    (args.out / "manifest.json").write_text(json.dumps(reports, indent=2) + "\n")


if __name__ == "__main__":
    main()
