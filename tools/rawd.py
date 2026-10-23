"""RAWD (entry type 17) decoding for Crash Bandicoot: Warped.

The wiki describes entry type 17 as *"miscellaneous 3D terrain data
(scenery in baron levels, water in levels with water)"*.  The eight level
files that contain no ``WGEO`` (type 3) entries store their scenery here:
the biplane levels keep a pre-rendered terrain strip, the jet-ski levels
keep the animated water surface.

The layout below was derived from the USA disc (``SCUS_942.44``) by
comparing every RAWD entry of the 42 level archives; there is no public
documentation for it.

Item kinds
----------

mesh
    ``u16 type (=2), u16 count, u32 texture EID`` followed by ``count``
    16-byte records::

        u8 r, u8 g, u8 b, u8 pad
        3 x (u8 x, u8 y, u8 u, u8 v)

    ``x``/``y`` are layer coordinates (0..255); ``u``/``v`` are texture
    coordinates in 1/8 texel units, so ``u / 8`` and ``v / 8`` index the
    32x32 texel frame.  The triangles are flat 2D layers: for the
    biplane terrain ``u == x`` and ``v == y`` (a 1:1 strip of a
    pre-rendered backdrop), for water the layer is a strip of tiles.

texture
    A multiple of 4096 bytes: ``len / 4096`` frames of a 32x32 RGBA8888
    image (row stride 128 bytes).  ``Wat1R``/``Wat2R`` carry 14 frames
    (the water animation); ``Ter?R`` carries 4 and ``Wat5R`` 2.

constant
    A 65280-byte block filled with a single byte (0x00 or 0x70), i.e. a
    blank 256x255 byte page (no content, used as a fill).

grid
    The variable-size first item of ``Wat5R``/``Wat8R``/``WatBR``/
    ``WatER``: a 2D water-tile/scroll table of u16 pairs.  Not decoded
    (the port does not draw these layers natively); exposed as a raw
    block so the extractor is lossless.

The port renders vertex colours only, so :func:`layer_models` flattens
the layers into world-space triangles; the texture average colour of the
entry is used as the vertex colour.
"""
import struct

TEXTURE_FRAME = 4096          # 32 * 32 * 4
TEXTURE_SIZE = 32
UV_SCALE = 8.0                # u/v are stored in 1/8 texel units
MESH_TYPE = 2
MESH_RECORD = 16

BLOCK_CONSTANT = 65280        # 256 * 255 8-bit page


class RawdMesh:
    """One mesh item of a RAWD entry (a 2D textured triangle layer)."""

    def __init__(self, entry_name, index, data):
        self.entry_name = entry_name
        self.index = index
        self.type, self.count, self.tex_eid = struct.unpack_from("<HHI", data, 0)
        self.tex_name = eid_str(self.tex_eid)
        self.triangles = []
        for i in range(self.count):
            off = 8 + i * MESH_RECORD
            rec = data[off:off + MESH_RECORD]
            if len(rec) < MESH_RECORD:
                break
            colour = (rec[0], rec[1], rec[2])
            verts = [tuple(rec[4 + k * 4:8 + k * 4]) for k in range(3)]
            self.triangles.append((colour, verts))

    @property
    def name(self):
        return "%s_%d" % (self.entry_name, self.index)

    def bounds(self):
        xs = [v[0] for _, tri in self.triangles for v in tri]
        ys = [v[1] for _, tri in self.triangles for v in tri]
        return min(xs), min(ys), max(xs), max(ys)

    def average_colour(self):
        n = len(self.triangles)
        if not n:
            return (128, 128, 128)
        r = sum(t[0][0] for t in self.triangles) // n
        g = sum(t[0][1] for t in self.triangles) // n
        b = sum(t[0][2] for t in self.triangles) // n
        return (r, g, b)

    def __repr__(self):
        return "<RawdMesh %s n=%d tex=%s>" % (self.name, self.count, self.tex_name)


class RawdTexture:
    """A texture item: 1..N frames of a 32x32 RGBA8888 image."""

    def __init__(self, entry_name, index, data):
        self.entry_name = entry_name
        self.index = index
        self.data = data
        self.frame_count = len(data) // TEXTURE_FRAME

    @property
    def name(self):
        return "%s_%d" % (self.entry_name, self.index)

    def frame(self, i=0):
        return self.data[i * TEXTURE_FRAME:(i + 1) * TEXTURE_FRAME]

    def average_colour(self):
        """Alpha-weighted average RGB of every frame."""
        if not self.frame_count:
            return (128, 128, 128)
        r = g = b = w = 0
        for f in range(self.frame_count):
            fr = self.frame(f)
            for i in range(0, TEXTURE_FRAME, 4):
                a = fr[i + 3]
                if not a:
                    continue
                r += fr[i] * a
                g += fr[i + 1] * a
                b += fr[i + 2] * a
                w += a
        if not w:
            return (128, 128, 128)
        return (r // w, g // w, b // w)

    def __repr__(self):
        return "<RawdTexture %s frames=%d>" % (self.name, self.frame_count)


class RawdBlock:
    """Any item that is not a mesh or a texture (kept verbatim)."""

    def __init__(self, entry_name, index, data):
        self.entry_name = entry_name
        self.index = index
        self.data = data

    @property
    def name(self):
        return "%s_%d" % (self.entry_name, self.index)

    @property
    def kind(self):
        if len(self.data) == BLOCK_CONSTANT and self.data.count(self.data[0]) == len(self.data):
            return "constant"
        return "grid"

    def __repr__(self):
        return "<RawdBlock %s %s %d bytes>" % (self.name, self.kind, len(self.data))


def eid_str(value):
    """Local copy of :func:`nsf.eid_str` (kept dependency free)."""
    charset = ("0123456789"
               "abcdefghijklmnopqrstuvwxyz"
               "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
               "_!")
    out = "=" if value & 0x80000000 else ""
    for shift in (25, 19, 13, 7, 1):
        out += charset[(value >> shift) & 0x3F]
    if not (value & 1):
        out += "="
    return out


def classify(entry):
    """Split the items of a RAWD entry into meshes, textures and blocks."""
    meshes, textures, blocks = [], [], []
    for i, item in enumerate(entry.items):
        mesh = None
        if len(item) >= 8 + MESH_RECORD:
            mtype, count = struct.unpack_from("<HH", item, 0)
            if mtype == MESH_TYPE and len(item) == 8 + count * MESH_RECORD:
                mesh = RawdMesh(entry.name, i, item)
        if mesh is not None:
            meshes.append(mesh)
        elif item and len(item) % TEXTURE_FRAME == 0:
            textures.append(RawdTexture(entry.name, i, item))
        else:
            blocks.append(RawdBlock(entry.name, i, item))
    return meshes, textures, blocks


class Rawd:
    """A decoded RAWD (type 17) entry."""

    def __init__(self, entry):
        self.entry = entry
        self.name = entry.name
        self.meshes, self.textures, self.blocks = classify(entry)

    def __repr__(self):
        return ("<Rawd %s meshes=%d textures=%d blocks=%d>"
                % (self.name, len(self.meshes), len(self.textures), len(self.blocks)))


def entries_of(chunks):
    """Yield every :class:`Rawd` entry inside a parsed NSF chunk list."""
    for chunk in chunks:
        for entry in chunk.entries:
            if entry.type == 17:
                yield Rawd(entry)


# ---------------------------------------------------------------------------
# World-space flattening (used by export_levels.py)
# ---------------------------------------------------------------------------

LAYER_SCALE = 16.0        # cm per layer unit -> a 256-unit layer spans ~41 m
LAYER_LIFT = 0.5          # cm between stacked layers, avoids z-fighting


class _RawdModel:
    """Duck-typed stand-in for :class:`wgeo.Wgeo`."""

    def __init__(self):
        self.world_x = 0
        self.world_y = 0
        self.world_z = 0
        self.vertices = []        # (x, y, z, colour index, flags)
        self.triangles = []       # (a, b, c, unk0, unk1)
        self.quads = []
        self.colors = []
        self._colour_index = {}

    @property
    def vertex_count(self):
        return len(self.vertices)

    @property
    def triangle_count(self):
        return len(self.triangles)

    @property
    def quad_count(self):
        return 0

    def add_colour(self, rgb):
        if rgb not in self._colour_index:
            self._colour_index[rgb] = len(self.colors)
            self.colors.append(rgb)
        return self._colour_index[rgb]


def layer_models(rawds, scale=LAYER_SCALE):
    """Flatten every RAWD mesh of a level into one world-space model.

    The layers are 2D screen-space data in the original engine, so there is
    no stored placement in the level.  They are laid flat on the XZ plane,
    centred on the world origin, ``scale`` centimetres per layer unit.  The
    result is an approximation that gives these levels a walkable surface;
    it is *not* the original composition.
    """
    model = _RawdModel()

    # entry name -> alpha-weighted average colour of its texture frames
    tex_colours = {}
    for rawd in rawds:
        if rawd.textures:
            r = g = b = w = 0
            for tex in rawd.textures:
                tr, tg, tb = tex.average_colour()
                # every frame of every texture in the entry counts once
                r += tr
                g += tg
                b += tb
                w += 1
            tex_colours[rawd.name] = (r // w, g // w, b // w)

    for layer, rawd in enumerate(rawds):
        for mesh in rawd.meshes:
            tname = mesh.tex_name
            # the mesh references the "…T" texture page; the RAWD entry
            # provides the matching "…R" pixel data.  Fall back to any
            # texture in the same entry, then to the per-triangle colour.
            rgb = tex_colours.get(tname[:-1] + "R") or tex_colours.get(rawd.name)
            lift = LAYER_LIFT * (layer + 1)
            for colour, tri in mesh.triangles:
                rgb_t = rgb or colour
                cidx = model.add_colour(rgb_t)
                base = len(model.vertices)
                for (x, y, _u, _v) in tri:
                    wx = (x - 128.0) * scale
                    wz = (y - 64.0) * scale
                    model.vertices.append((int(round(wx * 16)),
                                           int(round(lift * 16)),
                                           int(round(wz * 16)),
                                           cidx, 0))
                model.triangles.append((base, base + 1, base + 2, 0, 0))
    return model


def build_model(chunks):
    """Return a flattened model for every RAWD entry of a level, or None."""
    rawds = list(entries_of(chunks))
    if not any(r.meshes for r in rawds):
        return None, rawds
    return layer_models(rawds), rawds
