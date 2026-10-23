"""TGEO (static geometry) decoding and mesh building for Crash Bandicoot: Warped.

Ported from ``tools/reference/crashwarped_bin.ts`` (``parseTGEO``,
``parseUV``, ``parseUVAnim``, ``TriFlags``, ``SignedBitStream``,
``generateVertices`` and ``buildModel``; MIT) and checked against the retail
USA disc (SCUS-94244).

TGEO entry layout (items of the NSF entry)
------------------------------------------
    item 0  geometry header (0x4C bytes)
    item 1  triangle flags: u32 copy_count, then u32 flags ... 0xFFFFFFFF
    item 2  colors: u32 count * (RGB8 + pad), see 0x3C
    item 3  UVs: u32 count * 12 bytes, see 0x34
    item 4  animated-texture records: u32 count * u32 flags, see 0x48
    item 5  compressed-vertex instructions (optional, only if present)

Header (item 0) fields used here::

    0x00  i32 scale.x        \\
    0x04  i32 scale.y         > divide by 0x100 (256)
    0x08  i32 scale.z        /
    0x0C  u32 texture name[0..7] (5-char Naughty Dog ids, base64-ish)
    0x2C  u32 unknown (25 for BA10G)                      TODO/UNVERIFIED
    0x30  u32 unknown (1 for BA10G)                       TODO/UNVERIFIED
    0x34  u32 uv_count
    0x38  u32 vertex_count        (called compressCount in the reference)
    0x3C  u32 color_count
    0x40  u32 texture_name_count
    0x44  u32 triangle_count
    0x48  u32 texture_animation_count

The reference names 0x38 ``compressCount`` with the comment "not quite right,
but the same or four more when there is compression".  On 0x0B it equals the
SVTX frame vertex count for every model we checked (BA10G: 8 == BA10V frame
vertex_count 8), and equals the compressed vertex count when item 5 exists.

UV record (12 bytes)::

    0x0  u8 u0   0x1  u8 v0
    0x2  u16 clut                 (skipped by parseUV)
    0x4  u8 u1   0x5  u8 v1
    0x6  u16 tex_page             (skipped by parseUV)
    0x8  u8 u2   0x9  u8 v2
    0xA  u8 u3   0xB  u8 v3

``tex_page`` bits 10..12 (mask 0x1C00) select the texture name index.

Triangle flags (``TriFlags``)
-----------------------------
    0x000000FF UV_INDEX        1-based index into the UV list (0 = untextured)
    0x00000100 TEX_ANIM        UV_INDEX is an index into the UV animation list
    0x0000FE00 COLOR_INDEX     index into the color list (<<9)
    0x00FF0000 VERTEX_SOURCE   slot id for the vertex cache
    0x01000000 NO_FOG
    0x04000000 LOAD_VERTEX     reuse VERTEX_SOURCE instead of allocating
    0x08000000 STORED_COLORS
    0x10000000 ONE_SIDED
    0x20000000 FLIPPED         do not swap the last two indices
    0x40000000 USE_OLDEST
    0x80000000 NEW_TRIANGLE    this flag and the next two initialise a triangle

Compressed vertex instruction (4 bytes)::

    bit_counts = [1 + ((w >> 6) & 7), 1 + ((w >> 3) & 7), 1 + (w & 7)]
    offsets    = [(w >> 24) & 0xFE, (w >> 17) & 0xFF, (w >> 9) & 0xFF]

Scale formula for a mesh vertex::

    pos = (u8_coord + svtx_frame.origin) * tgeo.scale / 8

``origin`` is already divided by 32 by ``svtx.parse_svtx``.
"""
import struct

import svtx as svtx_mod
import tgeo_shading

TEX_ANIM_FLAG = 0x100000
TEXTURE_INDEX_MASK = 0x1C00

# TriFlags
UV_INDEX = 0x000000FF
TEX_ANIM = 0x00000100
COLOR_INDEX = 0x0000FE00
VERTEX_SOURCE = 0x00FF0000
NO_FOG = 0x01000000
LOAD_VERTEX = 0x04000000
STORED_COLORS = 0x08000000
ONE_SIDED = 0x10000000
FLIPPED = 0x20000000
USE_OLDEST = 0x40000000
NEW_TRIANGLE = 0x80000000

# UVFlags (parseUVAnim)
UV_MIPS = 0x80000000
UV_SPEED = 0x7C000000
UV_OFFSET = 0x03F80000
UV_COUNT = 0x0007F000
UV_ANIMATED = 0x00000800
UV_BASE = 0x000007FF


class UV:
    __slots__ = ("tex_page", "clut", "us", "vs", "min_u", "max_u",
                 "min_v", "max_v", "tex_index")

    def __init__(self, tex_page, clut, us, vs):
        self.tex_page = tex_page
        self.clut = clut
        self.us = us
        self.vs = vs
        self.min_u = min(us)
        self.max_u = max(us)
        self.min_v = min(vs)
        self.max_v = max(vs)
        self.tex_index = (tex_page & TEXTURE_INDEX_MASK) >> 10

    def as_dict(self):
        return {
            "tex_page": self.tex_page,
            "clut": self.clut,
            "us": list(self.us),
            "vs": list(self.vs),
            "min_u": self.min_u, "max_u": self.max_u,
            "min_v": self.min_v, "max_v": self.max_v,
            "tex_index": self.tex_index,
        }

    def __repr__(self):
        return "<UV page=%04X clut=%04X u=%s v=%s>" % (
            self.tex_page, self.clut, self.us, self.vs)


class TextureAnimation:
    __slots__ = ("mip_mapped", "frame_shift", "count_mask", "indirect",
                 "offset", "start", "flags")

    def __init__(self, flags):
        self.flags = flags
        self.mip_mapped = (flags & UV_MIPS) != 0
        self.frame_shift = (flags & UV_SPEED) >> 26
        self.count_mask = (flags & UV_COUNT) >> 12
        self.indirect = (flags & UV_ANIMATED) != 0
        self.offset = (flags & UV_OFFSET) >> 19
        self.start = flags & UV_BASE

    def as_dict(self):
        return {
            "flags": self.flags,
            "mip_mapped": self.mip_mapped,
            "frame_shift": self.frame_shift,
            "count_mask": self.count_mask,
            "indirect": self.indirect,
            "offset": self.offset,
            "start": self.start,
        }

    def __repr__(self):
        return "<TexAnim start=%d count=%d shift=%d>" % (
            self.start, self.count_mask, self.frame_shift)


class CompressedVertex:
    __slots__ = ("bit_counts", "offsets", "raw")

    def __init__(self, raw):
        self.raw = raw
        self.bit_counts = [1 + ((raw >> 6) & 7),
                           1 + ((raw >> 3) & 7),
                           1 + (raw & 7)]
        self.offsets = [(raw >> 0x18) & 0xFE,
                        (raw >> 0x11) & 0xFF,
                        (raw >> 0x09) & 0xFF]

    def __repr__(self):
        return "<CompressedVertex bits=%s offsets=%s>" % (
            self.bit_counts, self.offsets)


def parse_uv(data, offs):
    """12-byte UV record; the byte order matches the reference exactly."""
    clut = struct.unpack_from("<H", data, offs + 2)[0]
    tex_page = struct.unpack_from("<H", data, offs + 6)[0]
    us = []
    vs = []
    for i in range(0, 0xC, 2):
        if i == 2 or i == 6:
            continue
        us.append(data[offs + i])
        vs.append(data[offs + i + 1])
    return UV(tex_page, clut, us, vs)


def parse_uv_anim(flags):
    return TextureAnimation(flags)


def _u32(data, offs):
    return struct.unpack_from("<I", data, offs)[0]


class Tgeo:
    """Decoded TGEO entry."""

    __slots__ = (
        "scale", "texture_names", "uvs", "colors", "tri_flags", "tri_count",
        "tex_anims", "compression", "vertex_count", "uv_count",
        "color_count", "tex_count", "tex_anim_count", "copy_count",
        "unknown_2c", "unknown_30", "warnings", "attribute_words",
    )

    def __init__(self):
        self.warnings = []

    def as_dict(self):
        return {
            "scale": list(self.scale),
            "texture_names": list(self.texture_names),
            "texture_count": self.tex_count,
            "uv_count": self.uv_count,
            "color_count": self.color_count,
            "triangle_count": self.tri_count,
            "tri_flag_count": len(self.tri_flags),
            "tex_anim_count": self.tex_anim_count,
            "compression_count": len(self.compression),
            "vertex_count": self.vertex_count,
            "copy_count": self.copy_count,
            "unknown_2c": self.unknown_2c,
            "unknown_30": self.unknown_30,
            "uvs": [u.as_dict() for u in self.uvs],
            "colors": [list(c) for c in self.colors],
            "attribute_words": list(self.attribute_words),
            "attribute_interpretation": "unresolved_object_render_mode; colors are legacy RGB candidates",
            "tri_flags": list(self.tri_flags),
            "tex_anims": [a.as_dict() for a in self.tex_anims],
            "compression": [{"raw": c.raw, "bit_counts": c.bit_counts,
                             "offsets": c.offsets} for c in self.compression],
            "warnings": list(self.warnings),
        }

    def __repr__(self):
        return ("<Tgeo scale=%s tex=%d uv=%d col=%d tri=%d flags=%d comp=%d>"
                % (self.scale, len(self.texture_names), len(self.uvs),
                   len(self.colors), self.tri_count, len(self.tri_flags),
                   len(self.compression)))


def parse_tgeo(entry):
    """Decode a TGEO ``nsf.Entry`` into a :class:`Tgeo`."""
    items = entry.items
    if not items or len(items[0]) < 0x4C:
        raise ValueError("TGEO entry too short")
    h = items[0]

    t = Tgeo()
    t.scale = (_u32s(h, 0x00) / 256.0,
               _u32s(h, 0x04) / 256.0,
               _u32s(h, 0x08) / 256.0)
    t.unknown_2c = _u32(h, 0x2C)
    t.unknown_30 = _u32(h, 0x30)
    t.uv_count = _u32(h, 0x34)
    t.vertex_count = _u32(h, 0x38)
    t.color_count = _u32(h, 0x3C)
    t.tex_count = _u32(h, 0x40)
    t.tri_count = _u32(h, 0x44)
    t.tex_anim_count = _u32(h, 0x48)

    # texture names: at most 8 fit before 0x2C
    t.texture_names = []
    max_names = (0x2C - 0x0C) // 4
    for i in range(min(t.tex_count, max_names)):
        t.texture_names.append(_decode_name(_u32(h, 0x0C + i * 4)))
    if t.tex_count > max_names:
        t.warnings.append("tex_count %d > %d header slots" %
                          (t.tex_count, max_names))

    # triangle flags
    t.tri_flags = []
    t.copy_count = 0
    if len(items) > 1:
        data = items[1]
        if len(data) >= 4:
            t.copy_count = _u32(data, 0)
            offs = 4
            while offs + 4 <= len(data):
                v = _u32(data, offs)
                offs += 4
                if v == 0xFFFFFFFF:
                    break
                t.tri_flags.append(v)
        else:
            t.warnings.append("triangle-flag item shorter than 4 bytes")
    if len(t.tri_flags) < t.tri_count:
        t.warnings.append("tri_flags %d < tri_count %d" %
                          (len(t.tri_flags), t.tri_count))

    # The viewer called these colors. Native draw modes 3/4 instead read
    # the same bytes as signed normals shifted left 5. Keep complete words
    # and the historical RGB candidates; only object draw context can choose.
    t.colors = []
    t.attribute_words = []
    if len(items) > 2:
        data = items[2]
        for i in range(t.color_count):
            o = i * 4
            t.attribute_words.append(_u32(data, o) if o + 4 <= len(data) else None)
            if o + 3 > len(data):
                t.warnings.append("color %d out of range" % i)
                t.colors.append((0.0, 0.0, 0.0))
                continue
            t.colors.append((data[o + 0] / 255.0,
                             data[o + 1] / 255.0,
                             data[o + 2] / 255.0))
    elif t.color_count:
        t.warnings.append("color_count %d but no color item" % t.color_count)

    # UVs
    t.uvs = []
    if len(items) > 3:
        data = items[3]
        for i in range(t.uv_count):
            o = i * 0xC
            if o + 0xC > len(data):
                t.warnings.append("uv %d out of range" % i)
                break
            t.uvs.append(parse_uv(data, o))
        if len(t.uvs) != t.uv_count:
            t.warnings.append("decoded %d of %d UVs" %
                              (len(t.uvs), t.uv_count))
    elif t.uv_count:
        t.warnings.append("uv_count %d but no UV item" % t.uv_count)

    # animated texture records
    t.tex_anims = []
    if len(items) > 4 and t.tex_anim_count:
        data = items[4]
        for i in range(t.tex_anim_count):
            o = i * 4
            if o + 4 > len(data):
                t.warnings.append("tex_anim %d out of range" % i)
                break
            t.tex_anims.append(parse_uv_anim(_u32(data, o)))

    # compressed vertex instructions (item 5 exists only when compressed)
    t.compression = []
    if len(items) >= 6:
        data = items[5]
        for o in range(0, len(data) - 3, 4):
            t.compression.append(CompressedVertex(_u32(data, o)))
        if t.vertex_count and len(t.compression) != t.vertex_count:
            t.warnings.append(
                "compressed instructions %d != header vertex_count %d" %
                (len(t.compression), t.vertex_count))

    return t


def _u32s(data, offs):
    """Signed 32-bit read (the reference uses getInt32 for scale)."""
    return struct.unpack_from("<i", data, offs)[0]


def _decode_name(value):
    import cb3
    return cb3.decode_chunk_name(value)


# ---------------------------------------------------------------------------
# mesh building (buildModel)
# ---------------------------------------------------------------------------

def _advance_vertex(flags, buffer_index, stored_verts, last_verts,
                    last_colors, last_attribute_flags, state, warnings):
    store_index = (flags & VERTEX_SOURCE) >> 16
    if flags & LOAD_VERTEX:
        if store_index in stored_verts:
            next_index = stored_verts[store_index]
        else:
            warnings.append("LOAD_VERTEX of unset slot %d" % store_index)
            next_index = 0
    else:
        next_index = state["vtx_index"]
        state["vtx_index"] += 1
        stored_verts[store_index] = next_index
    last_verts[buffer_index] = next_index
    last_colors[buffer_index] = (flags & COLOR_INDEX) >> 9
    last_attribute_flags[buffer_index] = flags


def _push_triangle(flags, last_verts, last_colors, last_attribute_flags, tri_lists, textures,
                   tgeo, warnings):
    uv_index = flags & UV_INDEX
    tex_anim = (flags & TEX_ANIM) != 0
    if tex_anim:
        key = TEX_ANIM_FLAG | uv_index
    elif uv_index >= 1:
        key = _texture_id_for_uv(textures, uv_index - 1, tgeo, warnings)
    else:
        key = -1

    info = {
        "flags": flags,
        "uv_index": uv_index if tex_anim else uv_index - 1,
        "tex_anim": tex_anim,
        "vs": tuple(last_verts),
        "cs": tuple(last_colors),
        "attribute_flags": tuple(last_attribute_flags),
    }
    tri_lists.setdefault(key, []).append(info)


def _texture_id_for_uv(textures, uv_index, tgeo, warnings):
    if 0 <= uv_index < len(textures):
        return textures[uv_index]
    warnings.append("UV index %d out of range" % uv_index)
    return -1


def _local_texture_ids(tgeo, warnings):
    """Approximate ``TextureCache.findOrParse`` for one model.

    The reference keys textures by ``(chunk name, tex_page & ~0x1C00, clut)``,
    not just by the chunk name, so two UVs that use the same page with
    different CLUTs/blend modes are separate draw calls.  We assign ids in
    first-use order and keep the name for reporting.
    """
    ids = []
    by_key = {}
    names = {}
    for i, uv in enumerate(tgeo.uvs):
        name = None
        if 0 <= uv.tex_index < len(tgeo.texture_names):
            name = tgeo.texture_names[uv.tex_index]
        else:
            warnings.append("UV %d texture index %d out of range" %
                            (i, uv.tex_index))
        key = (name, uv.tex_page & ~TEXTURE_INDEX_MASK, uv.clut)
        tex_id = by_key.get(key)
        if tex_id is None:
            tex_id = len(by_key)
            by_key[key] = tex_id
            names[tex_id] = name
        ids.append(tex_id)
    return ids, names


def build_mesh(tgeo, svtx_frames, texture_indices=None, render_mode=None):
    """Build a renderable mesh from a TGEO and its SVTX frames.

    ``svtx_frames`` may be a list of :class:`svtx.SVTXFrame` or an SVTX
    ``nsf.Entry``.  ``texture_indices`` is the reference's per-UV texture id
    list (one entry per UV, mapping to a texture cache index).  When omitted,
    the UV's own ``tex_page`` texture-name index is used, which is enough for
    offline previews and JSON dumps.

    The returned dict is JSON-friendly.  Vertex order, triangle order, draw
    call grouping and the winding flip follow ``buildModel`` exactly; the
    addition is per-frame positions and bounding boxes.
    """
    if hasattr(svtx_frames, "items") and not isinstance(svtx_frames, list):
        svtx_frames = svtx_mod.parse_svtx(svtx_frames)
    frames = list(svtx_frames)
    if not frames:
        raise ValueError("build_mesh: no SVTX frames")

    warnings = list(tgeo.warnings)
    if texture_indices is None:
        texture_indices, texture_names_by_id = _local_texture_ids(tgeo, warnings)
    else:
        texture_names_by_id = {}
    stored_verts = {}
    last_verts = [-1, -1, -1]
    last_colors = [-1, -1, -1]
    last_attribute_flags = [0, 0, 0]
    shading_kind = tgeo_shading.interpretation(render_mode)
    state = {"vtx_index": 0}
    tri_lists = {}
    triangles = []

    flags = tgeo.tri_flags
    i = 0
    while i < len(flags):
        f = flags[i]
        if i == 0 or (f & NEW_TRIANGLE):
            if i + 2 >= len(flags):
                warnings.append("triangle runs past end of tri_flags")
                break
            for j in range(3):
                _advance_vertex(flags[i + j], j, stored_verts, last_verts,
                                last_colors, last_attribute_flags, state, warnings)
            i += 2
            _push_triangle(f, last_verts, last_colors, last_attribute_flags, tri_lists,
                           texture_indices, tgeo, warnings)
            i += 1
            continue

        if (f >> 16) == 0:
            last_colors[1] = (f >> 9) & 0x7F
            last_colors[2] = (f >> 2) & 0x7F
            last_colors[0] = last_colors[1]
            last_attribute_flags[:] = [f, f, f]
        else:
            if not (f & USE_OLDEST):
                last_colors[0] = last_colors[1]
                last_verts[0] = last_verts[1]
                last_attribute_flags[0] = last_attribute_flags[1]
            last_colors[1] = last_colors[2]
            last_verts[1] = last_verts[2]
            last_attribute_flags[1] = last_attribute_flags[2]
            _advance_vertex(f, 2, stored_verts, last_verts, last_colors,
                            last_attribute_flags, state, warnings)
            _push_triangle(f, last_verts, last_colors, last_attribute_flags, tri_lists,
                           texture_indices, tgeo, warnings)
        i += 1

    if len(tri_lists) == 0:
        raise ValueError("build_mesh: no triangles decoded")

    # per-frame byte vertices (compressed or raw)
    comp = tgeo.compression if tgeo.compression else None
    animation_vertex_count = len(comp) if comp else frames[0].vertex_count
    raw_frames = []
    for fi, frame in enumerate(frames):
        raw = svtx_mod.generate_vertices(frame, comp)
        if len(raw) < animation_vertex_count:
            warnings.append(
                "frame %d has %d vertices, expected %d" %
                (fi, len(raw), animation_vertex_count))
            raw = raw + [(0, 0, 0)] * (animation_vertex_count - len(raw))
        elif len(raw) > animation_vertex_count:
            raw = raw[:animation_vertex_count]
        raw_frames.append(raw)

    if comp and frames[0].vertex_count != animation_vertex_count:
        warnings.append(
            "SVTX vertex_count %d != compressed count %d" %
            (frames[0].vertex_count, animation_vertex_count))

    # output vertices: one per triangle corner, like buildModel's attrData
    vertices = []
    indices = []
    index = 0
    bbox_min = None
    bbox_max = None
    bbox_all_min = None
    bbox_all_max = None
    draw_calls = []

    for key, infos in tri_lists.items():
        start_index = index
        for tri_no in range(len(infos) - 1, -1, -1):
            info = infos[tri_no]
            uv = None
            if info["uv_index"] >= 0 and not info["tex_anim"]:
                if info["uv_index"] < len(tgeo.uvs):
                    uv = tgeo.uvs[info["uv_index"]]
                else:
                    warnings.append("triangle uses UV %d out of range" %
                                    info["uv_index"])
            corner = [0, 0, 0]
            for k in range(3):
                j = k
                if j > 0 and not (info["flags"] & FLIPPED):
                    j = 3 - j
                corner[k] = j

                cs = info["cs"][j]
                word = tgeo.attribute_words[cs] if 0 <= cs < len(tgeo.attribute_words) else None
                packed_attribute = tgeo_shading.attribute(word) if word is not None else None
                if 0 <= cs < len(tgeo.colors):
                    color = list(tgeo.colors[cs])
                else:
                    warnings.append("color index %d out of range" % cs)
                    color = [0.0, 0.0, 0.0]
                # Preserve the old RGB candidate only for compatibility with
                # diagnostic viewers. Production exporters require a native
                # RGB mode explicitly. A known normal is never emitted as RGB.
                if shading_kind != "rgb8" and render_mode is not None:
                    color = None

                u = v = 0
                if uv is not None:
                    u = uv.us[j]
                    v = uv.vs[j]
                elif info["tex_anim"]:
                    u = 1 if j == 1 else 0
                    v = 1 if j == 2 else 0

                vtx_index = info["vs"][j]
                frame_positions = []
                for fi in range(len(frames)):
                    raw = raw_frames[fi]
                    if 0 <= vtx_index < len(raw):
                        bx, by, bz = raw[vtx_index]
                    else:
                        warnings.append(
                            "vertex index %d out of range (frame %d)" %
                            (vtx_index, fi))
                        bx = by = bz = 0
                    org = frames[fi].origin
                    pos = (
                        (bx + org[0]) * tgeo.scale[0] / 8.0,
                        (by + org[1]) * tgeo.scale[1] / 8.0,
                        (bz + org[2]) * tgeo.scale[2] / 8.0,
                    )
                    frame_positions.append(pos)
                    if fi == 0:
                        if bbox_min is None:
                            bbox_min = list(pos)
                            bbox_max = list(pos)
                        else:
                            for a in range(3):
                                bbox_min[a] = min(bbox_min[a], pos[a])
                                bbox_max[a] = max(bbox_max[a], pos[a])
                    if bbox_all_min is None:
                        bbox_all_min = list(pos)
                        bbox_all_max = list(pos)
                    else:
                        for a in range(3):
                            bbox_all_min[a] = min(bbox_all_min[a], pos[a])
                            bbox_all_max[a] = max(bbox_all_max[a], pos[a])

                vertices.append({
                    "pos": list(frame_positions[0]),
                    "uv": [u, v],
                    "color": color,
                    "attribute_index": cs,
                    "attribute_source_flags": info["attribute_flags"][j],
                    "attribute": packed_attribute,
                    "vtx_index": vtx_index,
                    "frame_positions": [list(p) for p in frame_positions],
                })
                indices.append(index)
                index += 1

            triangles.append({
                "indices": [index - 3, index - 2, index - 1],
                "uv_index": info["uv_index"],
                "tex_anim": info["tex_anim"],
                "one_sided": bool(info["flags"] & ONE_SIDED),
                "flipped": bool(info["flags"] & FLIPPED),
                "flags": info["flags"],
                "corner_order": corner,
            })

        first_info = infos[0]
        draw_call = {
            "texture_index": key,
            "texture_name": None,
            "tex_anim_index": None,
            "start_index": start_index,
            "index_count": len(infos) * 3,
            "one_sided": bool(first_info["flags"] & ONE_SIDED),
        }
        if key >= 0 and (key & TEX_ANIM_FLAG):
            draw_call["texture_index"] = -1
            draw_call["tex_anim_index"] = key ^ TEX_ANIM_FLAG
        else:
            draw_call["texture_name"] = texture_names_by_id.get(key)
        draw_calls.append(draw_call)

    mesh = {
        "scale": list(tgeo.scale),
        "frame_count": len(frames),
        "vertex_count": len(vertices),
        "animation_vertex_count": animation_vertex_count,
        "tri_count": len(triangles),
        "vertices": vertices,
        "indices": indices,
        "triangles": triangles,
        "draw_calls": draw_calls,
        "bbox": {"min": bbox_min, "max": bbox_max},
        "bbox_all_frames": {"min": bbox_all_min, "max": bbox_all_max},
        "warnings": warnings,
        "shading": {"render_mode": render_mode, "interpretation": shading_kind,
                    "color_field": "legacy_rgb_candidate" if render_mode is None else shading_kind,
                    "native_lighting_integrated": False},
    }
    return mesh
