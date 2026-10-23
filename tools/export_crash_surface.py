"""Export original Cr10G UV/materials as expanded-corner CWT1 for char_build."""
import json
from pathlib import Path
import re
import struct

import cb3
import tgeo
import tpag
import tgeo_shading
from export_world_textures import align, pot, tile5551, untile5551
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def export_actor(geo_name="Cr10G", vtx_name="Cr10V", output_name="crash", actor_id=0xCB3,
                 normalize=None, sprite=False, output_directory=None, render_mode=None):
    # The model header cannot establish RGB versus packed normals. CWT1
    # stores RGB only, so fail before writing any asset for an unknown or
    # normal-consuming object mode. GOOL/runtime must supply this context.
    if not sprite:
        tgeo_shading.require_rgb(render_mode)
    arc = cb3.load_level(0x0B)
    if sprite:
        from types import SimpleNamespace
        raw = arc.entry("FruiC").items[5]
        if raw[0] != 2 or struct.unpack_from("<H", raw, 2)[0] != 14:
            raise ValueError("Original fruit sprite declaration changed")
        import nsf
        page_name = nsf.eid_str(struct.unpack_from("<I", raw, 4)[0])
        uvs = [tgeo.parse_uv(raw, 12 + frame * 16) for frame in range(14)]
        geo = SimpleNamespace(uvs=uvs, texture_names=[page_name], tex_anims=[])
        positions = [(-.5,.5,0),(.5,.5,0),(-.5,-.5,0),(.5,-.5,0)]
        order = [0,1,2,2,1,3]
        vertices,triangles,calls=[],[],[]
        for frame,uv in enumerate(uvs):
            uv.tex_index = 0
            color=[channel/255 for channel in raw[8+frame*16:11+frame*16]]
            vertices.extend({"pos":list(positions[index]),"color":color,
                             "uv":[uv.us[index],uv.vs[index]],"vtx_index":index} for index in order)
            triangles.extend({"uv_index":frame,"tex_anim":False,"corner_order":part}
                             for part in (order[:3],order[3:]))
            calls.append({"start_index":frame*6,"index_count":6})
        mesh = {"vertices":vertices,"vertex_count":84,"tri_count":28,
                "triangles":triangles,"draw_calls":calls,
                "bbox":{"min":[-.5,-.5,0],"max":[.5,.5,0]}}
    else:
        geo = tgeo.parse_tgeo(arc.entry(geo_name))
        mesh = tgeo.build_mesh(geo, arc.entry(vtx_name), render_mode=render_mode)
    if output_name == "crash":
        source = (ROOT / "source/crash_model.h").read_text(encoding="utf-8")
        indices = [int(value) for value in re.search(r"crash_indices\[CRASH_CORNER_COUNT\]\s*=\s*\{(.*?)\};", source, re.S).group(1).replace("\n", "").split(",") if value.strip()]
        if indices != [vertex["vtx_index"] for vertex in mesh["vertices"]]:
            raise ValueError("Cr10G corner order differs from compiled character mesh")
    if normalize:
        lo, hi = mesh["bbox"]["min"], mesh["bbox"]["max"]
        for vertex in mesh["vertices"]:
            vertex["pos"] = [(vertex["pos"][axis] - (lo[axis] if axis == 1 else (lo[axis]+hi[axis])/2)) /
                             max(hi[axis]-lo[axis], 1e-6) for axis in range(3)]
        mesh["bbox"] = {"min":[-.5,0,-.5],"max":[.5,1,.5]}
    count = mesh["vertex_count"]
    materials = {None: {"bounds": [0, 0, 0, 0]}}
    corners = []
    calls = []
    for call in mesh["draw_calls"]:
        start, length = call["start_index"], call["index_count"]
        key = None
        batch_uv = []
        for index in range(start, start + length, 3):
            triangle = mesh["triangles"][index // 3]
            uv = None
            if triangle["tex_anim"]:
                animation = geo.tex_anims[triangle["uv_index"]]
                uv = geo.uvs[animation.start - 1]
            elif triangle["uv_index"] >= 0:
                uv = geo.uvs[triangle["uv_index"]]
            for k, corner in enumerate(triangle["corner_order"]):
                vertex = mesh["vertices"][index + k]
                if uv is not None:
                    key = (geo.texture_names[uv.tex_index], uv.tex_page & ~0x1C00, uv.clut)
                    u, v = uv.us[corner], uv.vs[corner]
                else:
                    u = v = 0
                batch_uv.append((vertex, u, v))
        if key is not None:
            bounds = [min(v[1] for v in batch_uv), max(v[1] for v in batch_uv),
                      min(v[2] for v in batch_uv), max(v[2] for v in batch_uv)]
            if key in materials:
                old = materials[key]["bounds"]
                bounds = [min(old[0], bounds[0]), max(old[1], bounds[1]),
                          min(old[2], bounds[2]), max(old[3], bounds[3])]
            materials[key] = {"bounds": bounds}
        corners.extend((key, vertex, u, v) for vertex, u, v in batch_uv)
        calls.append((key, start, length))
    output = Path(output_directory) if output_directory is not None else ROOT / "game_data/characters"
    output.mkdir(parents=True, exist_ok=True)
    payloads, metadata, ids = [], [], {}
    for key, material in materials.items():
        if key is None:
            rgba, width, height, flags, blend = bytes((255, 255, 255, 255)), 1, 1, 0, 0
        else:
            name, page, clut = key
            image = tpag.decode_texture(arc.texture(name).data, tpag.make_uv(clut, page, material["bounds"]))
            rgba, width, height = image.rgba, image.width, image.height
            flags = bool(image.transparent_pixels) | (bool(image.xlu_pixels) << 1)
            blend = image.blend
            Image.frombytes("RGBA", (width, height), rgba).save(output / ("%s_%04x_%04x.png" % key))
        pw, ph = pot(width), pot(height)
        material.update({"pw": pw, "ph": ph})
        tiled = tile5551(rgba, width, height, pw, ph)
        inverse = untile5551(tiled, pw, ph)
        for y in range(height):
            for x in range(width):
                a, b = 4 * (y * width + x), 4 * (y * pw + x)
                expected = bytes(((c >> 3) << 3) | (c >> 5) for c in rgba[a:a+3]) + bytes((255 if rgba[a+3] else 0,))
                if inverse[b:b+4] != expected:
                    raise AssertionError("Crash pixel roundtrip failed")
        ids[key] = len(metadata)
        metadata.append([pw, ph, 0, len(tiled), flags, blend, 0, 0])
        payloads.append(tiled)
    vbo = bytearray()
    for key, vertex, u, v in corners:
        material = materials[key]
        bounds = material["bounds"]
        uu, vv = (u - bounds[0] + .5) / material["pw"], 1 - (v - bounds[2] + .5) / material["ph"]
        color = vertex["color"] if key is not None else [c * .5 for c in vertex["color"]]
        vbo += struct.pack("<8f", *vertex["pos"], *color, uu, vv)
    ibo = struct.pack("<%dH" % count, *range(count))
    bounds = mesh["bbox"]["min"] + mesh["bbox"]["max"]
    batches = [struct.pack("<8I6f", 0, count, start, length, ids[key], 0, 0, 0, *bounds) for key, start, length in calls]
    batch_offset = 64
    texture_offset = align(batch_offset + 56 * len(batches))
    vertex_offset = align(texture_offset + 32 * len(metadata))
    index_offset = align(vertex_offset + len(vbo))
    pixel_offset = align(index_offset + len(ibo))
    total = pixel_offset + sum(map(len, payloads))
    data = bytearray(total)
    data[:64] = struct.pack("<4s15I", b"CWT1", 1, actor_id, count, count, len(batches), len(metadata),
                           vertex_offset, index_offset, batch_offset, texture_offset, pixel_offset, total, 0, 0, 0)
    data[batch_offset:batch_offset + len(batches) * 56] = b"".join(batches)
    data[vertex_offset:vertex_offset + len(vbo)] = vbo
    data[index_offset:index_offset + len(ibo)] = ibo
    cursor = pixel_offset
    for index, (meta, payload) in enumerate(zip(metadata, payloads)):
        meta[2] = cursor
        struct.pack_into("<8I", data, texture_offset + index * 32, *meta)
        data[cursor:cursor + len(payload)] = payload
        cursor += len(payload)
    if sprite:
        struct.pack_into("<I",data,52,14) # one batch per original sprite frame
    (output / (output_name + ".cwt")).write_bytes(data)
    report = {"asset":output_name,"source_geo":geo_name,"source_svtx":vtx_name,
              "shading": "GOOL sprite RGB8" if sprite else "native object RGB8 mode %d" % render_mode,
              "native_draw_effects_integrated": False,
              "corners": count, "triangle_count": mesh["tri_count"], "batches": len(batches),
              "textures": len(metadata), "bytes": len(data), "corner_order": "verified against crash_indices" if output_name == "crash" else "TGEO draw order",
              "animations": "14 original fruit sprite frames" if sprite else "texture frame zero; Crash vertices updated at runtime" if output_name=="crash" else "mesh and texture frame zero; original animation controller pending",
              "pixel_roundtrip": "every texel verified"}
    (output / (output_name + "_surface.json")).write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))


def main():
    export_actor()
    export_actor("BN10G", "BN10V", "wood", 0xCB4, "crate")
    export_actor("BT10G", "BN10V", "tnt", 0xCB5, "crate")
    export_actor("Bn10G", "BN10V", "nitro", 0xCB6, "crate")
    export_actor("BC10G", "BC10V", "checkpoint", 0xCB7, "crate")
    export_actor("FruiC sprite0", "FruiC sprite0", "wumpa", 0xCB8, sprite=True)
    export_actor("BS10G", "BS10V", "spring", 0xCB9, "crate")
    export_actor("BF10G", "BS10V", "bounce", 0xCBA, "crate")
    export_actor("WD10G", "WD10V", "aku_mask", 0xCBB)


if __name__ == "__main__":
    main()
