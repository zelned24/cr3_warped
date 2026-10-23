"""World-geometry (WGEO) decoding for Crash Bandicoot PSX levels (CB3 format).

Layout follows cbhacks/drnsf `nsf::wgeo_v2` (7 items) and is re-checked against
the public viewer `tools/reference/crashwarped_bin.ts` (noclip.website, MIT):

  item 0 (+0x00, 19 words) header:
      0x00 i32 origin.x      0x04 i32 origin.y      0x08 i32 origin.z
      0x0C u32 isSkybox      0x10 u32 vertexCount   0x14 u32 triangleCount
      0x18 u32 quadCount     0x1C u32 uvCount       0x20 u32 colorCount
      0x24 u32 uvAnimCount   0x28 u32 textureCount  0x2C u32 textureNames[]
  item 1 : vertices  (N * 6 bytes; 4-byte part reversed, 2-byte part forward)
  item 2 : triangles (N * 6 bytes)
  item 3 : quads     (N * 8 bytes)
  item 4 : UVs       (N * 12 bytes)
  item 5 : colours   (N * 4 bytes, RGB888 + pad)
  item 6 : animated texture flags (N * 4 bytes)

The reference divides the origin by 16 (``scaleFactor``) and leaves the 12-bit
vertex components unscaled; ``build_mesh()`` follows the reference exactly and
therefore returns *reference space* coordinates (Y up). The compact exporter
uses the same axis order and units. ``world_x/y/z``
keep the signed raw origin; ``vertices`` keep the unsigned local positions.
There is no Y/Z swap.
"""
import struct

# Texture-mode bits of a PSX primitive/texpage word (reference TexModeFlags).
TEXMODE_TEXTURE_INDEX = 0x1C00
TEX_PAGE_MASK = 0xFFFF & ~TEXMODE_TEXTURE_INDEX

# UV animation attribute bits (reference UVFlags).
UVFLAG_MIPS = 0x80000000
UVFLAG_SPEED = 0x7C000000
UVFLAG_OFFSET = 0x03F80000
UVFLAG_COUNT = 0x0007F000
UVFLAG_ANIMATED = 0x00000800
UVFLAG_BASE = 0x000007FF

# Naughty Dog chunk-name alphabet, same table as nsf.CHARSET.
_CHARSET = ("0123456789"
            "abcdefghijklmnopqrstuvwxyz"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "_!")


def _sbits(value, count):
    """Sign-extend a `count`-bit two's complement value."""
    if value & (1 << (count - 1)):
        value -= 1 << count
    return value


def decode_name(h):
    """Decode a 32-bit Naughty Dog hash into its 5-character name."""
    h >>= 1
    out = ""
    for _ in range(5):
        out = _CHARSET[h & 0x3F] + out
        h >>= 6
    return out


class UV:
    """One 12-byte UV descriptor (reference ``parseUV``).

    us/vs are texel coordinates inside the PSX page (0..255), one pair per
    corner of the primitive.  ``texture_index`` is the index into
    ``Wgeo.texture_names`` carried by the texpage word (bits 10..12).
    """

    __slots__ = ("tex_page", "clut", "us", "vs",
                 "min_u", "max_u", "min_v", "max_v",
                 "texture_index", "page_key", "offset")

    def __init__(self, tex_page, clut, us, vs, offset=-1):
        self.tex_page = tex_page
        self.clut = clut
        self.us = us
        self.vs = vs
        self.min_u = min(us)
        self.max_u = max(us)
        self.min_v = min(vs)
        self.max_v = max(vs)
        self.texture_index = (tex_page & TEXMODE_TEXTURE_INDEX) >> 10
        # Identity used by the viewer's TextureCache: chunk + blend/mode + clut.
        self.page_key = (self.texture_index, tex_page & TEX_PAGE_MASK, clut)
        self.offset = offset

    def __repr__(self):
        return ("<UV tex=%d clut=%04X u=%d..%d v=%d..%d>"
                % (self.texture_index, self.clut, self.min_u, self.max_u,
                   self.min_v, self.max_v))

    def as_dict(self):
        return {
            "tex_page": self.tex_page,
            "clut": self.clut,
            "texture_index": self.texture_index,
            "us": list(self.us),
            "vs": list(self.vs),
            "min_u": self.min_u, "max_u": self.max_u,
            "min_v": self.min_v, "max_v": self.max_v,
        }


class TextureAnimation:
    """Decoded UV-animation word (reference ``parseUVAnim``).

    A primitive with ``uv_ind & 0x800`` is animated: ``start`` + ``count_mask``
    select the UV frames that are cycled every ``1 << frame_shift`` ticks.
    """

    __slots__ = ("flags", "mip_mapped", "frame_shift", "count_mask",
                 "indirect", "offset", "start")

    def __init__(self, flags):
        self.flags = flags
        self.mip_mapped = (flags & UVFLAG_MIPS) != 0
        self.frame_shift = (flags & UVFLAG_SPEED) >> 0x1A
        self.count_mask = (flags & UVFLAG_COUNT) >> 0xC
        self.indirect = (flags & UVFLAG_ANIMATED) != 0
        self.offset = (flags & UVFLAG_OFFSET) >> 0x13
        self.start = flags & UVFLAG_BASE

    def __repr__(self):
        return ("<TexAnim start=%d count=%d shift=%d mips=%d>"
                % (self.start, self.count_mask, self.frame_shift,
                   self.mip_mapped))

    def as_dict(self):
        return {
            "start": self.start,
            "count_mask": self.count_mask,
            "frame_shift": self.frame_shift,
            "mip_mapped": self.mip_mapped,
            "indirect": self.indirect,
            "offset": self.offset,
            "flags": self.flags,
        }


class Wgeo:
    def __init__(self, entry):
        items = entry.items
        if len(items) < 7:
            raise ValueError("not a wgeo_v2 entry")
        self.entry = entry
        self.eid = entry.eid
        self.name = entry.name
        hdr = items[0]
        w = list(struct.unpack_from("<%dI" % (len(hdr) // 4), hdr, 0))
        # -- compact-export fields, in the same Y-up coordinate space --
        self.world_x = _sbits(w[0], 32)
        self.world_y = _sbits(w[1], 32)
        self.world_z = _sbits(w[2], 32)
        self.info_unk0 = w[3]
        self.vertex_count = w[4]
        self.triangle_count = w[5]
        self.quad_count = w[6]
        self.item4_count = w[7]
        self.color_count = w[8]
        self.item6_count = w[9]
        self.tpag_ref_count = w[10]
        self.tpag_refs = [w[11 + i] for i in range(min(self.tpag_ref_count, 8))]

        self.item1 = items[1]
        self.vertices = self._read_vertices(items[1])
        self.triangles = self._read_triangles(items[2])
        self.quads = self._read_quads(items[3])
        self.item4 = items[4]
        self.colors = self._read_colors(items[5])
        self.item6 = items[6]

        # -- reference-faithful fields -------------------------------------
        self.is_skybox = w[3] != 0
        self.scale = 16
        # Reference divides the raw origin by 16; local vertices remain unscaled.
        self.origin_raw = (self.world_x, self.world_y, self.world_z)
        self.origin = tuple(v / 16.0 for v in self.origin_raw)
        self.texture_names = [decode_name(w[11 + i])
                              for i in range(min(self.tpag_ref_count, 8))]
        self.texture_count = self.tpag_ref_count
        self.uvs = self._read_uvs(items[4])
        self.tex_anims = self._read_tex_anims(items[6])
        self.tex_regions = self._build_tex_regions()
        self._mesh = None
        # Filled by build_mesh(); kept as attributes for convenience.
        self.draw_calls = []

    # -- vertices ---------------------------------------------------------
    def _read_vertices(self, data):
        n = self.vertex_count
        base = n * 4
        out = []
        for i in range(n):
            word = struct.unpack_from("<I", data, (n - 1 - i) * 4)[0]
            color_mid = word & 0xF
            x = (word >> 4) & 0xFFF
            color_high = (word >> 16) & 0x3
            fx = (word >> 18) & 0x3
            y = (word >> 20) & 0xFFF
            word2 = struct.unpack_from("<H", data, base + i * 2)[0]
            color_low = word2 & 0xF
            z = (word2 >> 4) & 0xFFF
            out.append((x, y, z, color_low | (color_mid << 4) | (color_high << 8), fx))
        return out

    # -- triangles --------------------------------------------------------
    def _read_triangles(self, data):
        n = self.triangle_count
        base = n * 4
        out = []
        for i in range(n):
            word = struct.unpack_from("<I", data, (n - 1 - i) * 4)[0]
            unk0 = word & 0xFF
            v0 = (word >> 20) & 0xFFF
            v1 = (word >> 8) & 0xFFF
            word2 = struct.unpack_from("<H", data, base + i * 2)[0]
            unk1 = word2 & 0xF
            v2 = (word2 >> 4) & 0xFFF
            out.append((v0, v1, v2, unk0, unk1))
        return out

    # -- quads ------------------------------------------------------------
    def _read_quads(self, data):
        n = self.quad_count
        out = []
        for i in range(n):
            off = i * 8
            word = struct.unpack_from("<I", data, off)[0]
            word2 = struct.unpack_from("<I", data, off + 4)[0]
            unk0 = word & 0xFF
            v0 = (word >> 20) & 0xFFF
            v1 = (word >> 8) & 0xFFF
            unk1 = word2 & 0xFF
            v2 = (word2 >> 20) & 0xFFF
            v3 = (word2 >> 8) & 0xFFF
            out.append((v0, v1, v2, v3, unk0, unk1))
        return out

    # -- colours ----------------------------------------------------------
    def _read_colors(self, data):
        out = []
        for i in range(self.color_count):
            r, g, b, x = struct.unpack_from("<4B", data, i * 4)
            out.append((r, g, b))
        return out

    # -- UVs / animations (reference parseUV / parseUVAnim) ---------------
    def _read_uvs(self, data):
        out = []
        n = min(self.item4_count, len(data) // 12)
        for i in range(n):
            off = i * 12
            clut = struct.unpack_from("<H", data, off + 2)[0]
            tex_page = struct.unpack_from("<H", data, off + 6)[0]
            us = []
            vs = []
            for k in (0, 4, 8, 10):
                us.append(data[off + k])
                vs.append(data[off + k + 1])
            out.append(UV(tex_page, clut, us, vs, off))
        return out

    def _read_tex_anims(self, data):
        out = []
        for i in range(self.item6_count):
            if 4 * i + 4 <= len(data):
                out.append(TextureAnimation(struct.unpack_from("<I", data, 4 * i)[0]))
        return out

    def _build_tex_regions(self):
        """Union of the UV rectangles used per (texture, page-mode, clut)."""
        regions = {}
        order = []
        for uv in self.uvs:
            key = uv.page_key
            reg = regions.get(key)
            if reg is None:
                reg = {
                    "texture_index": uv.texture_index,
                    "texture_name": (self.texture_names[uv.texture_index]
                                     if uv.texture_index < len(self.texture_names)
                                     else None),
                    "page": uv.tex_page,
                    "page_mode": uv.tex_page & TEX_PAGE_MASK,
                    "clut": uv.clut,
                    "min_u": uv.min_u, "max_u": uv.max_u,
                    "min_v": uv.min_v, "max_v": uv.max_v,
                    "uv_count": 0,
                }
                regions[key] = reg
                order.append(key)
            else:
                reg["min_u"] = min(reg["min_u"], uv.min_u)
                reg["max_u"] = max(reg["max_u"], uv.max_u)
                reg["min_v"] = min(reg["min_v"], uv.min_v)
                reg["max_v"] = max(reg["max_v"], uv.max_v)
            reg["uv_count"] += 1
        return [regions[k] for k in order]

    # -- reference-faithful vertex decode ---------------------------------
    def _ref_vertices(self):
        """Decode the vertex block exactly like the reference parser.

        Returns (x, y, z, color_index, pos_effect, color_effect) with 12-bit
        unsigned coordinates (Y up), matching the ``vertices`` attribute.
        """
        n = self.vertex_count
        data = self.item1
        out = []
        for i in range(n):
            x, y = struct.unpack_from("<HH", data, n * 4 - 4 * i - 4)
            z = struct.unpack_from("<H", data, n * 4 + 2 * i)[0]
            color_index = ((y & 0x3) << 8) | ((x & 0xF) << 4) | (z & 0xF)
            out.append((x >> 4, y >> 4, z >> 4, color_index,
                        (y & 4) != 0, (y & 8) == 0))
        return out

    # -- mesh building ----------------------------------------------------
    def build_mesh(self):
        """Build a renderable mesh following the reference ``parseWGEO``.

        Returns a dict:

          origin        reference-space origin (raw header / 16, Y up)
          is_skybox     header flag
          vertices      list of dicts: x, y, z, u, v, r, g, b, color,
                        pos_effect, color_effect
          indices       u16 triangle list
          draw_calls    list of dicts: start_index, index_count,
                        texture_index (into ``texture_pages`` below),
                        texture_name, page, clut, tex_anim_index, one_sided,
                        uv_index
          texture_pages per-draw-call texture identity (chunk + mode + clut)
          texture_indices  per-UV index into texture_pages
        """
        if self._mesh is not None:
            return self._mesh

        vtx = self._ref_vertices()
        colors = self.colors
        uvs = self.uvs
        anims = self.tex_anims
        names = self.texture_names

        # TextureCache identity -> compact index, first use order.
        page_ids = {}
        pages = []
        uv_texture_index = []
        tex_index_of_uv = []
        for uv in uvs:
            key = uv.page_key
            if key not in page_ids:
                page_ids[key] = len(pages)
                pages.append({
                    "texture_index": uv.texture_index,
                    "texture_name": (names[uv.texture_index]
                                     if uv.texture_index < len(names) else None),
                    "page": uv.tex_page,
                    "page_mode": uv.tex_page & TEX_PAGE_MASK,
                    "clut": uv.clut,
                    "min_u": uv.min_u, "max_u": uv.max_u,
                    "min_v": uv.min_v, "max_v": uv.max_v,
                })
            idx = page_ids[key]
            uv_texture_index.append(idx)
            tex_index_of_uv.append(uv.texture_index)

        seen = {}
        out_vertices = []

        def get_vtx(coord, u, v):
            key = (u << 20) | (v << 12) | coord
            got = seen.get(key)
            if got is not None:
                return got
            if coord >= len(vtx):
                coord = 0
            x, y, z, cidx, pos_effect, color_effect = vtx[coord]
            rgb = colors[cidx] if cidx < len(colors) else (255, 255, 255)
            out_index = len(out_vertices)
            seen[key] = out_index
            out_vertices.append({
                "x": x, "y": y, "z": z,
                "u": u, "v": v,
                "r": rgb[0], "g": rgb[1], "b": rgb[2],
                "color": cidx,
                "pos_effect": pos_effect,
                "color_effect": color_effect,
            })
            return out_index

        # draw-call key -> index list, insertion ordered (like the JS Map)
        lists = {}
        order = []

        def emit(key, indices):
            lst = lists.get(key)
            if lst is None:
                lst = []
                lists[key] = lst
                order.append(key)
            lst.extend(indices)

        def resolve_primitive(uv_ind):
            """Return (draw_key, uv_for_corners, tex_index_or_-1, anim_index)."""
            if uv_ind & UVFLAG_ANIMATED:
                anim_index = uv_ind & UVFLAG_BASE
                if anim_index < len(anims) and anims[anim_index].mip_mapped:
                    first = anims[anim_index].start - 1
                    if 0 <= first < len(uvs):
                        # viewer merges mips with the first static frame
                        return ("tex", uv_texture_index[first]), uvs[first], tex_index_of_uv[first], -1
                    return ("anim", anim_index), None, -1, anim_index
                return ("anim", anim_index), None, -1, anim_index
            if uv_ind > 0:
                uvi = uv_ind - 1
                if uvi < len(uvs):
                    return ("tex", uv_texture_index[uvi]), uvs[uvi], tex_index_of_uv[uvi], -1
            return ("none",), None, -1, -1

        # -- triangles ----------------------------------------------------
        for (v0, v1, v2, unk0, unk1) in self.triangles:
            uv_ind = ((unk0 & 0xFF) << 4) | (unk1 & 0xF)
            key, uv, tex_index, anim_index = resolve_primitive(uv_ind)
            src = (v0, v1, v2)
            inds = []
            for j in range(3):
                if uv is not None:
                    inds.append(get_vtx(src[j], uv.us[j], uv.vs[j]))
                elif uv_ind & UVFLAG_ANIMATED:
                    inds.append(get_vtx(src[j], j % 2, j >> 1))
                else:
                    inds.append(get_vtx(src[j], 0, 0))
            emit(key, inds)

        # -- quads --------------------------------------------------------
        for (v0, v1, v2, v3, unk0, unk1) in self.quads:
            uv_ind = ((unk0 & 0xFF) << 4) | (unk1 & 0xF)
            key, uv, tex_index, anim_index = resolve_primitive(uv_ind)
            src = (v0, v1, v2, v3)
            inds = []
            for j in range(4):
                if uv is not None:
                    inds.append(get_vtx(src[j], uv.us[j], uv.vs[j]))
                elif uv_ind & UVFLAG_ANIMATED:
                    inds.append(get_vtx(src[j], j % 2, j >> 1))
                else:
                    inds.append(get_vtx(src[j], 0, 0))
            emit(key, [inds[0], inds[1], inds[2], inds[2], inds[1], inds[3]])

        indices = []
        draw_calls = []
        for key in order:
            inds = lists[key]
            if not inds:
                continue
            dc = {
                "start_index": len(indices),
                "index_count": len(inds),
                "texture_index": -1,
                "texture_name": None,
                "page": 0,
                "clut": 0,
                "tex_anim_index": -1,
                "one_sided": False,
                "uv_index": None,
            }
            if key[0] == "tex":
                page = pages[key[1]]
                dc["texture_index"] = key[1]
                dc["texture_name"] = page["texture_name"]
                dc["page"] = page["page"]
                dc["clut"] = page["clut"]
            elif key[0] == "anim":
                dc["tex_anim_index"] = key[1]
            indices.extend(inds)
            draw_calls.append(dc)

        self.draw_calls = draw_calls
        self._mesh = {
            "origin": self.origin,
            "is_skybox": self.is_skybox,
            "vertices": out_vertices,
            "indices": indices,
            "draw_calls": draw_calls,
            "texture_pages": pages,
            "texture_indices": uv_texture_index,
            "vertex_count": len(out_vertices),
            "index_count": len(indices),
            "triangle_count": len(indices) // 3,
        }
        return self._mesh

    def bounds(self):
        xs = [v[0] for v in self.vertices]
        ys = [v[1] for v in self.vertices]
        zs = [v[2] for v in self.vertices]
        return (min(xs), min(ys), min(zs), max(xs), max(ys), max(zs))

    def bounds_ref(self):
        """Bounds in reference space (Y up, origin not applied)."""
        vtx = self._ref_vertices()
        if not vtx:
            return (0, 0, 0, 0, 0, 0)
        xs = [v[0] for v in vtx]
        ys = [v[1] for v in vtx]
        zs = [v[2] for v in vtx]
        return (min(xs), min(ys), min(zs), max(xs), max(ys), max(zs))

    def color_of(self, vertex):
        idx = vertex[3]
        if idx < len(self.colors):
            return self.colors[idx]
        return (255, 255, 255)

    def __repr__(self):
        return ("<Wgeo %s verts=%d tris=%d quads=%d uvs=%d tex=%d>"
                % (self.name, self.vertex_count, self.triangle_count,
                   self.quad_count, len(self.uvs), len(self.texture_names)))
