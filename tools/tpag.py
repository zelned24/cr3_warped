"""TPAG: PSX texture-page decoding for Crash Bandicoot: Warped.

Crash Warped has **no** TPAG/type-5 entries.  Texture data is shipped as
chunk type ``1``: raw 64 KiB PSX VRAM pages (334 of them over the 42 retail
level archives).  A page is referenced by its chunk CID from TGEO/WGEO, and
the per-triangle UV record chooses the sampling mode (4/8/16 bpp), the CLUT
and the X base inside the page.

Everything here is a 1:1 port of the reference viewer kept at
``tools/reference/crashwarped_bin.ts`` (noclip.website, MIT):

    TexModeFlags       line ~1282
    expand5to8         line ~1303
    fillColor          line ~1307
    parseTexPage       line ~1321
    TextureCache       line ~1248
    texPageName        line ~1277
    parseUV            line ~346
    containsUVs        line ~1236
    extendTexPage      line ~1240

The port is verified against the retail USA disc (SCUS-94244) by
``tools/test_tpag.py``; ``docs/assets/TPAG.md`` records the byte evidence and
the open questions.

Conventions
-----------
* ``page_data`` is the **whole** 64 KiB chunk, 16-byte chunk header included,
  exactly like the reference (which slices ``chunkOffs .. chunkOffs+0x10000``
  and then starts reading at byte 0).  Consequence: the 16 header bytes are
  decoded as pixels in the top-left corner of the page.
* Page rows are ``0x200`` bytes apart (the reference hard-codes that stride),
  so a page is ``PAGE_ROWS = 128`` rows tall and
  ``0x200 * 8 / depth`` pixels wide: 1024x128 (4 bpp), 512x128 (8 bpp) or
  256x128 (16 bpp).
* UV coordinates are absolute texels inside the page, not normalised.
* Colours are straight RGBA8.  Alpha keeps the PSX distinction:
  ``0x00`` = colour 0 / not drawn, ``0x80`` = bit 15 set (XLU candidate),
  ``0xFF`` = opaque.

No Pillow, no numpy.
"""
import collections
import struct

# ---------------------------------------------------------------------------
# constants (TexModeFlags, crashwarped_bin.ts ~1282)
# ---------------------------------------------------------------------------

TEX_MODE_X_BASE = 0x0F          # texpage X base, in 64-pixel units
TEX_MODE_Y_BASE = 0x10          # present in the mask, unused by the decoder
TEX_MODE_BLEND = 0x60           # semi-transparency mode (>> 5 -> 0..3)
TEX_MODE_COLOR_DEPTH = 0x180    # colour depth (>> 7 -> 0=4bpp 1=8bpp 2=16bpp)
TEX_MODE_TEXTURE_INDEX = 0x1C00  # index into TGEO.textureNames (>> 10)

PAGE_SIZE = 0x10000             # 64 KiB chunk, header included
PAGE_HEADER_SIZE = 0x10         # header decoded as pixels, see module docstring
PAGE_ROW_BYTES = 0x200          # row stride used by the reference
PAGE_ROWS = PAGE_SIZE // PAGE_ROW_BYTES  # 128

DEPTHS = (4, 8, 16)

#: PSX texture-page X base granularity: ``texpage & 0x0F`` counts 64 pixels.
TEX_X_UNIT = 64

ALPHA_NONE = 0x00
ALPHA_XLU = 0x80
ALPHA_OPAQUE = 0xFF


def tex_depth(tex_page):
    """Colour depth in bpp for a texpage word: 4, 8 or 16."""
    return 4 << ((tex_page & TEX_MODE_COLOR_DEPTH) >> 7)


def tex_blend(tex_page):
    """Semi-transparency mode 0..3 (``XLUBlendMode`` minus the negative ones)."""
    return (tex_page & TEX_MODE_BLEND) >> 5


def tex_x_base(tex_page):
    """X origin of the texture inside the page, in page pixels."""
    return (tex_page & TEX_MODE_X_BASE) << 6


def tex_index(tex_page):
    """Index into the TGEO ``textureNames`` list (page slot, not page id)."""
    return (tex_page & TEX_MODE_TEXTURE_INDEX) >> 10


def expand5to8(n):
    """PSX 5-bit channel to 8-bit, bit-replicating (``0x1F`` -> ``0xFF``)."""
    return (n << 3) | (n >> 2)


def unpack_rgba5551(raw):
    """One PSX colour word -> ``(r, g, b, a)`` with the reference alpha rules."""
    r = expand5to8(raw & 0x1F)
    g = expand5to8((raw >> 5) & 0x1F)
    b = expand5to8((raw >> 10) & 0x1F)
    if raw == 0:
        a = ALPHA_NONE
    elif raw & 0x8000:
        a = ALPHA_XLU
    else:
        a = ALPHA_OPAQUE
    return r, g, b, a


# ---------------------------------------------------------------------------
# UV records (parseUV, crashwarped_bin.ts ~346)
# ---------------------------------------------------------------------------

_UV_BYTE_PAIRS = (0, 4, 8, 10)   # 4 corners; i == 2 and i == 6 are clut/texpage


def parse_uv(raw, offs=0):
    """Decode one 12-byte UV record into a plain dict.

    Layout (from the reference loop, ``crashwarped_bin.ts:346``)::

        0x0 u8 u0   0x1 u8 v0
        0x2 u16 clut
        0x4 u8 u1   0x5 u8 v1
        0x6 u16 texPage
        0x8 u8 u2   0x9 u8 v2
        0xA u8 u3   0xB u8 v3

    ``us``/``vs`` therefore hold the four corners at byte pairs
    ``(0,1) (4,5) (8,9) (10,11)`` -- ``i == 2`` and ``i == 6`` are skipped by
    the reference loop because they are the two u16 fields.

    Verified against BA10G UV 0 of level 0x0B, bytes
    ``60 3f 45 01 7f 3f 62 00 7f 20 7f 20`` -> clut ``0x145``, texPage
    ``0x0062``, us ``[0x60,0x7F,0x7F,0x7F]``, vs ``[0x3F,0x3F,0x20,0x20]``.
    """
    if raw is None or len(raw) - offs < 12:
        raise ValueError("UV record needs 12 bytes at offset %d" % offs)
    clut = raw[offs + 2] | (raw[offs + 3] << 8)
    tex_page = raw[offs + 6] | (raw[offs + 7] << 8)
    us = [raw[offs + i] for i in _UV_BYTE_PAIRS]
    vs = [raw[offs + i + 1] for i in _UV_BYTE_PAIRS]
    min_u, max_u = min(us), max(us)
    min_v, max_v = min(vs), max(vs)
    return {
        "clut": clut,
        "tex_page": tex_page,
        "us": us,
        "vs": vs,
        "min_u": min_u,
        "max_u": max_u,
        "min_v": min_v,
        "max_v": max_v,
        "bounds": (min_u, max_u, min_v, max_v),
        "depth": tex_depth(tex_page),
        "blend": tex_blend(tex_page),
        "tex_x": tex_x_base(tex_page),
        # Slot into TGEO.textureNames, derived from the texpage bits.  The
        # reference stores this in ``uv.pageSlot``-like fashion through
        # ``(texPage & TEXTURE_INDEX) >>> 10`` and keeps ``texIndex`` (-1) for
        # the cache, so mirror that split here.
        "page_slot": tex_index(tex_page),
        "tex_index": -1,
    }


def make_uv(clut, tex_page, bounds):
    """Build a UV dict from an explicit ``(min_u, max_u, min_v, max_v)`` box.

    Used by ``tools/dump_tex.py`` when only ``--uv-clut``/``--uv-page`` plus a
    rectangle are known.  The four corners are filled in the same order
    ``parse_uv`` would produce for a quad.
    """
    min_u, max_u, min_v, max_v = bounds
    raw = bytearray(12)
    corners = ((min_u, min_v), (max_u, min_v), (max_u, max_v), (min_u, max_v))
    for slot, (u, v) in zip((0, 4, 8, 10), corners):
        raw[slot] = u & 0xFF
        raw[slot + 1] = v & 0xFF
    struct.pack_into("<H", raw, 2, clut & 0xFFFF)
    struct.pack_into("<H", raw, 6, tex_page & 0xFFFF)
    uv = parse_uv(raw)
    if (uv["min_u"], uv["max_u"], uv["min_v"], uv["max_v"]) != tuple(bounds):
        raise ValueError("bounds %r do not survive 8-bit UV clamping" % (bounds,))
    return uv


# ---------------------------------------------------------------------------
# CLUT
# ---------------------------------------------------------------------------

def clut_address(clut):
    """``(x, y)`` of a CLUT word in PSX VRAM, in 16-bit units.

    ``x = (clut & 0x000F) << 4`` (16-entry aligned), ``y = (clut & 0x1FC0) >> 6``
    -- ``crashwarped_bin.ts:1358``.  Bits 4..5 are unused by this mask.
    """
    return (clut & 0x000F) << 4, (clut & 0x1FC0) >> 6


def clut_offset(clut, index):
    """Byte offset inside ``page_data`` of palette entry ``index``."""
    x, y = clut_address(clut)
    return 2 * (x + index + 0x100 * y)


def read_palette(page_data, clut, depth):
    """Read ``1 << depth`` raw colour words from the page's CLUT area."""
    size = 1 << depth
    out = []
    for i in range(size):
        off = clut_offset(clut, i)
        if off + 2 > len(page_data):
            raise ValueError("CLUT 0x%04X entry %d outside the 64 KiB page"
                             % (clut, i))
        out.append(page_data[off] | (page_data[off + 1] << 8))
    return out


# ---------------------------------------------------------------------------
# page geometry
# ---------------------------------------------------------------------------

def page_dimensions(depth):
    """``(width, height)`` in pixels of a full 64 KiB page at ``depth`` bpp.

    Row stride is fixed at ``0x200`` bytes (reference), so::

        4 bpp -> 1024 x 128     (0x200*8/4 = 1024)
        8 bpp ->  512 x 128
        16 bpp -> 256 x 128

    ``1024*128 / 2 == 512*128 == 256*128*2 == 0x10000`` bytes, i.e. the whole
    chunk including its 16-byte header.
    """
    if depth not in DEPTHS:
        raise ValueError("unsupported depth %r (expected 4, 8 or 16)" % (depth,))
    return (PAGE_ROW_BYTES * 8) // depth, PAGE_ROWS


def page_bytes(depth):
    """Bytes consumed by a full page decode (always ``0x10000``)."""
    w, h = page_dimensions(depth)
    return (w * h * depth) // 8


# ---------------------------------------------------------------------------
# decoding
# ---------------------------------------------------------------------------

class TextureImage(object):
    """A decoded RGBA8 rectangle.

    Attributes: ``rgba`` (``bytes``, ``width*height*4``), ``width``,
    ``height``, ``name``, ``bounds`` (``(min_u, max_u, min_v, max_v)``),
    ``depth``, ``blend``, ``has_opa``, ``has_xlu``, ``opaque_pixels``,
    ``xlu_pixels``, ``transparent_pixels``.

    Iterating yields ``(rgba, width, height)`` so callers may do
    ``rgba, w, h = decode_texture(...)``.
    """

    __slots__ = ("rgba", "width", "height", "name", "bounds", "depth",
                 "blend", "has_opa", "has_xlu", "opaque_pixels",
                 "xlu_pixels", "transparent_pixels")

    def __init__(self, rgba, width, height, name, bounds, depth, blend,
                 opaque_pixels, xlu_pixels, transparent_pixels):
        self.rgba = rgba
        self.width = width
        self.height = height
        self.name = name
        self.bounds = bounds
        self.depth = depth
        self.blend = blend
        self.has_opa = opaque_pixels > 0
        self.has_xlu = xlu_pixels > 0
        self.opaque_pixels = opaque_pixels
        self.xlu_pixels = xlu_pixels
        self.transparent_pixels = transparent_pixels

    # -- convenience -----------------------------------------------------
    def __iter__(self):
        return iter((self.rgba, self.width, self.height))

    def __len__(self):
        return len(self.rgba)

    def pixel(self, x, y):
        """``(r, g, b, a)`` of one decoded pixel."""
        o = (y * self.width + x) * 4
        return tuple(self.rgba[o:o + 4])

    def unique_colors(self, with_alpha=False):
        """Number of distinct colours; alpha included only on request."""
        n = 4 if with_alpha else 3
        step = 4
        return len({self.rgba[i:i + n] for i in range(0, len(self.rgba), step)})

    def alpha_histogram(self):
        hist = collections.Counter()
        for i in range(3, len(self.rgba), 4):
            hist[self.rgba[i]] += 1
        return dict(hist)

    def is_uniform(self):
        return self.unique_colors(with_alpha=True) == 1

    def __repr__(self):
        return ("<TextureImage %s %dx%d %dbpp colors=%d alpha=%s>"
                % (self.name, self.width, self.height, self.depth,
                   self.unique_colors(True),
                   ",".join("0x%02X" % a for a in sorted(self.alpha_histogram()))))


def _decode(page_data, depth, tex_x, clut, min_u, max_u, min_v, max_v,
            name, blend, palette=None):
    """Core decoder, a literal port of ``parseTexPage`` (~1321)."""
    if depth not in DEPTHS:
        raise ValueError("unsupported depth %r" % (depth,))
    width = max_u - min_u + 1
    height = max_v - min_v + 1
    if width <= 0 or height <= 0:
        raise ValueError("empty bounds (%d..%d, %d..%d)"
                         % (min_u, max_u, min_v, max_v))

    data = page_data
    out = bytearray(width * height * 4)
    offs = 0
    opa = xlu = trn = 0

    def fill(raw):
        r, g, b, a = unpack_rgba5551(raw)
        out[offs] = r
        out[offs + 1] = g
        out[offs + 2] = b
        out[offs + 3] = a
        return a

    if depth == 16:
        # 16 bpp: one u16 per pixel, top bit is transparency.
        last = 2 * (tex_x + max_u + 0x100 * max_v) + 2
        if last > len(data):
            raise ValueError("16 bpp span ends at 0x%X, page has 0x%X bytes"
                             % (last, len(data)))
        for y in range(min_v, max_v + 1):
            row = 2 * (tex_x + 0x100 * y)
            for x in range(min_u, max_u + 1):
                i = row + 2 * x
                a = fill(data[i] | (data[i + 1] << 8))
                offs += 4
                if a == ALPHA_OPAQUE:
                    opa += 1
                elif a == ALPHA_XLU:
                    xlu += 1
                else:
                    trn += 1
    else:
        pal = palette if palette is not None else read_palette(data, clut, depth)
        if len(pal) < (1 << depth):
            raise ValueError("palette has %d entries, need %d"
                             % (len(pal), 1 << depth))
        if depth == 8:
            step = 1
            last = 2 * tex_x + max_u + 0x200 * max_v + 1
        else:
            step = 2
            last = 2 * tex_x + (max_u >> 1) + 0x200 * max_v + 1
        if last > len(data):
            raise ValueError("%d bpp span ends at 0x%X, page has 0x%X bytes"
                             % (depth, last, len(data)))
        for y in range(min_v, max_v + 1):
            row = 2 * tex_x + 0x200 * y
            for x in range(min_u, max_u + 1):
                if depth == 8:
                    idx = data[row + x]
                else:
                    two = data[row + (x >> 1)]
                    idx = (two & 0xF) if (x & 1) == 0 else (two >> 4)
                a = fill(pal[idx])
                offs += 4
                if a == ALPHA_OPAQUE:
                    opa += 1
                elif a == ALPHA_XLU:
                    xlu += 1
                else:
                    trn += 1

    return TextureImage(bytes(out), width, height, name,
                        (min_u, max_u, min_v, max_v), depth, blend,
                        opa, xlu, trn)


def decode_texture(page_data, uv, bounds=None, palette=None):
    """Decode the region of ``page_data`` covered by one UV record.

    ``page_data`` is the full 64 KiB chunk (header included).
    ``uv`` is the dict from :func:`parse_uv`.  ``bounds`` optionally overrides
    the accumulated ``(min_u, max_u, min_v, max_v)`` box (same order as the
    reference ``vec4``; ``TextureCache`` uses it for atlas growth).

    Returns a :class:`TextureImage`; unpackable as ``(rgba, width, height)``.
    """
    if bounds is None:
        bounds = (uv["min_u"], uv["max_u"], uv["min_v"], uv["max_v"])
    depth = uv.get("depth") or tex_depth(uv["tex_page"])
    tex_x = uv.get("tex_x")
    if tex_x is None:
        tex_x = tex_x_base(uv["tex_page"])
    blend = uv.get("blend")
    if blend is None:
        blend = tex_blend(uv["tex_page"])
    name = uv.get("name") or tex_page_name(uv.get("page", "page"), uv)
    return _decode(page_data, depth, tex_x, uv["clut"],
                   bounds[0], bounds[1], bounds[2], bounds[3],
                   name, blend, palette)


def decode_page(page_data, depth=4, clut=0x0000, tex_x=0, name=None,
                palette=None):
    """Decode the **whole** 64 KiB page as one rectangle.

    Geometry comes from :func:`page_dimensions` (1024x128 / 512x128 /
    256x128).  A page mixes 4 and 8 bpp regions in practice, so this is a
    diagnostic view: only the pixels authored at ``depth`` and ``clut`` look
    right.  ``clut`` defaults to 0 (VRAM 0,0) -- pass the CLUT of a known
    region to get a readable picture.
    """
    width, height = page_dimensions(depth)
    blend = tex_blend(0)
    return _decode(page_data, depth, tex_x, clut,
                   0, width - 1, 0, height - 1,
                   name or ("page_%dbpp" % depth), blend, palette)


# ---------------------------------------------------------------------------
# cache / atlas keys (TextureCache, texPageName, containsUVs, extendTexPage)
# ---------------------------------------------------------------------------

TextureKey = collections.namedtuple("TextureKey", "page tex_page blend clut")
TextureKey.__doc__ = """Cache key: page name + texpage (blend/depth/xbase) + CLUT.

The reference (``texPageName``) uses ``texPage & ~TEXTURE_INDEX``, i.e. it
keeps the blend bits, the colour depth bits and the X base -- only the
``textureNames`` slot is dropped.  So two UVs that share a key are guaranteed
to need the same decode parameters, which is what lets the cache merge their
bounds into one atlas rectangle.
"""


def tex_page_name(page_name, uv):
    """Reference texture name: ``"<page>_<texpage>&~0x1C00>_<clut>"`` in hex."""
    tex_page = uv["tex_page"] & ~TEX_MODE_TEXTURE_INDEX & 0xFFFF
    return "%s_%x_%x" % (page_name, tex_page, uv["clut"])


def make_key(page_name, uv):
    return TextureKey(page_name,
                      uv["tex_page"] & ~TEX_MODE_TEXTURE_INDEX & 0xFFFF,
                      uv.get("blend", tex_blend(uv["tex_page"])),
                      uv["clut"])


def contains_uvs(bounds, uv):
    """True when ``bounds`` already covers the UV (``crashwarped_bin.ts:1236``)."""
    return (bounds[0] <= uv["min_u"] and bounds[1] >= uv["max_u"]
            and bounds[2] <= uv["min_v"] and bounds[3] >= uv["max_v"])


def extend_bounds(bounds, uv):
    """Grow a ``(min_u, max_u, min_v, max_v)`` box to include ``uv``."""
    return (min(bounds[0], uv["min_u"]), max(bounds[1], uv["max_u"]),
            min(bounds[2], uv["min_v"]), max(bounds[3], uv["max_v"]))


class TextureCache(object):
    """Accumulate one atlas rectangle per (page, texpage, clut) key.

    Port of ``TextureCache`` (``crashwarped_bin.ts:1248``).  ``pages`` maps a
    page name (``"Cr10T"``) to its 64 KiB chunk; ``find_or_parse`` returns
    ``-1`` for unknown pages, exactly like the reference, and writes the
    resulting index back into ``uv["tex_index"]``.
    """

    def __init__(self, pages=None):
        self.pages = {}
        if pages:
            for name, data in dict(pages).items():
                self.pages[name] = data
        self.tex_list = []
        self.key_map = {}

    # -- population ------------------------------------------------------
    def add_page(self, name, data):
        self.pages[name] = data

    @classmethod
    def from_archive(cls, archive):
        """Build a cache from a :class:`cb3.Archive` (all type-1 pages)."""
        return cls({p.name: p.data for p in archive.texture_pages})

    # -- reference API ---------------------------------------------------
    def find_or_parse(self, page_name, uv, page_data=None):
        data = page_data if page_data is not None else self.pages.get(page_name)
        if data is None:
            return -1
        key = make_key(page_name, uv)
        index = self.key_map.get(key)
        if index is None:
            image = decode_texture(data, uv)
            image.name = tex_page_name(page_name, uv)
            index = len(self.tex_list)
            self.key_map[key] = index
            self.tex_list.append(image)
        else:
            prev = self.tex_list[index]
            if not contains_uvs(prev.bounds, uv):
                bounds = extend_bounds(prev.bounds, uv)
                image = decode_texture(data, uv, bounds=bounds)
                image.name = prev.name
                self.tex_list[index] = image
        uv["tex_index"] = index
        return index

    # -- helpers ---------------------------------------------------------
    def __len__(self):
        return len(self.tex_list)

    def __iter__(self):
        return iter(self.tex_list)

    def total_bytes(self, bpp=16):
        """Decoded size of every atlas rectangle at ``bpp`` bits per pixel."""
        return sum((im.width * im.height * bpp) // 8 for im in self.tex_list)

    def summary(self):
        lines = ["%d textures, %d keys" % (len(self.tex_list), len(self.key_map))]
        for im in self.tex_list:
            lines.append("  %-24s %4dx%-4d %2dbpp %s"
                         % (im.name, im.width, im.height, im.depth,
                            im.bounds))
        return "\n".join(lines)


# ---------------------------------------------------------------------------
# packing helpers (contract: RGBA5551 u16, bit15 = alpha)
# ---------------------------------------------------------------------------

def rgba8_size(width, height):
    """Bytes of an RGBA8 rectangle."""
    return width * height * 4


def rgba5551_size(width, height):
    """Bytes of an RGBA5551 rectangle (2 bytes per pixel, no padding)."""
    return width * height * 2


def texture_cost(width, height):
    """``{"rgba8": bytes, "rgba5551": bytes, "pixels": n}`` for one rectangle."""
    return {"pixels": width * height,
            "rgba8": rgba8_size(width, height),
            "rgba5551": rgba5551_size(width, height)}


def to_rgba5551(rgba, width=None, height=None):
    """Pack straight RGBA8 into little-endian RGBA5551 words.

    ``bit15 = 1`` for any non-zero alpha (the PSX "draw this texel" bit), so
    ``0x80`` and ``0xFF`` both become opaque *in the packed word*: the
    0x80/0xFF distinction only survives in RGBA8 and must be carried by the
    texture flags (contract §2, ``flags`` bit1).  Colour channels are
    truncated 8 -> 5 bits.

    Returns ``bytes`` of ``width*height*2``.
    """
    n = len(rgba) // 4
    if width is not None and height is not None and width * height != n:
        raise ValueError("rgba holds %d pixels, %dx%d given" % (n, width, height))
    out = bytearray(n * 2)
    for i in range(n):
        o = i * 4
        raw = ((rgba[o] >> 3)
               | ((rgba[o + 1] >> 3) << 5)
               | ((rgba[o + 2] >> 3) << 10))
        if rgba[o + 3]:
            raw |= 0x8000
        out[i * 2] = raw & 0xFF
        out[i * 2 + 1] = raw >> 8
    return bytes(out)


def from_rgba5551(data):
    """Unpack little-endian RGBA5551 words back to RGBA8 (inverse of the above)."""
    out = bytearray((len(data) // 2) * 4)
    for i in range(len(data) // 2):
        raw = data[i * 2] | (data[i * 2 + 1] << 8)
        r, g, b, a = unpack_rgba5551(raw)
        o = i * 4
        out[o] = r
        out[o + 1] = g
        out[o + 2] = b
        out[o + 3] = a
    return bytes(out)


def pack_texture(image):
    """Convenience: :class:`TextureImage` -> RGBA5551 bytes + contract header.

    Returns ``(payload, meta)`` where ``meta`` matches the ``MDB1`` texture
    record (§3.2): ``width``, ``height``, ``format`` 0, ``flags``
    (bit0 = has alpha, bit1 = has semitransparent texels), ``size``.
    """
    payload = to_rgba5551(image.rgba, image.width, image.height)
    flags = 0
    if image.transparent_pixels:
        flags |= 1
    if image.xlu_pixels:
        flags |= 2
    meta = {"width": image.width, "height": image.height, "format": 0,
            "flags": flags, "size": len(payload),
            "name": image.name, "depth": image.depth, "blend": image.blend}
    return payload, meta


# ---------------------------------------------------------------------------
# small CLI for quick poking (dump_tex.py is the full tool)
# ---------------------------------------------------------------------------

def main(argv=None):
    import sys
    import cb3

    argv = list(sys.argv[1:] if argv is None else argv)
    level = int(argv[0], 0) if argv else 0x0B
    arc = cb3.load_level(level)
    print(arc.summary())
    cache = TextureCache.from_archive(arc)
    for page in arc.texture_pages:
        for depth in DEPTHS:
            w, h = page_dimensions(depth)
            print("%-6s %dbpp %4dx%-4d %s"
                  % (page.name, depth, w, h, texture_cost(w, h)))
    print(cache.summary())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
