"""SVTX (vertex animation) decoding for Crash Bandicoot: Warped (CB3).

Ported from the public viewer ``tools/reference/crashwarped_bin.ts``
(``parseSVTX``, MIT), and verified against the retail USA disc (SCUS-94244).

An SVTX entry is a *list of frames*.  Every frame is one NSF item (entry item
table), so the reference's absolute file offsets are the item offsets below.

Frame layout (offsets relative to the frame's first byte)
---------------------------------------------------------
    0x00  i16  origin.x        \\
    0x02  i16  origin.y         > divide by 32 -> object-space origin
    0x04  i16  origin.z        /
    0x08  i16  view_space_offset.x
    0x0A  i16  view_space_offset.y
    0x0C  i32  view_space_offset.z
    0x10  u32  vertex_count
    0x14  u32  hitbox_count
    0x18  u32  vertex_data_offset   (relative to this frame)
    0x1C  hitbox[hitbox_count]      (0x28 bytes each, see below)
          u8[vertex_count*3]        vertex data
    0x1C + hitbox_count*0x28        = nominal vertex data start

Hitbox layout (0x28 bytes, as read by the reference):

    0x00 u32 flags
    0x04 i32 center[3]
    0x10 i32 min_delta[3]
    0x1C i32 max_delta[3]

Vertex data
-----------
Uncompressed (TGEO has no compression item): each vertex is three bytes::

    x = byte[0]
    y = byte[2]        # note the swapped order, exactly as the reference
    z = byte[1]

The bytes are unsigned 0..255; the reference adds ``origin`` and then applies
``* tgeo.scale / 8`` when building the mesh.  ``points`` below already has the
origin added (floating point), matching ``parseSVTX`` in the reference.

Compressed (TGEO has a compression item): the bytes at ``start`` are a signed
bit stream, not 3-byte vertices.  ``parse_svtx`` still reports them as points
(the reference does the same and never uses them for compressed models); use
``tgeo.generate_vertices`` for the real coordinates.

Observed quirks / TODO
-----------------------
* ``vertex_data_offset`` is not always ``0x1C + hitbox_count*0x28``:
  BA10V gives 0x44 (= 0x1C + 0x28) but BE10V gives 0x47 and Kn10V gives 0xBF
  (three bytes after the nominal end).  The reference trusts the field, so we
  do too.  TODO/UNVERIFIED: what those 3 bytes are.
* The reference reads points until the next frame offset, including item
  alignment padding; we read exactly ``vertex_count`` vertices and report the
  leftover bytes as ``trailing_bytes``.  For BA10V both give 8 points.
* Hitbox field values look byte-shifted when read as the reference does
  (e.g. BA10V center.y == 0xC700, min == 0xFFFF3900).  They are not used for
  rendering, so this is TODO/UNVERIFIED rather than "fixed".
"""
import struct

HITBOX_SIZE = 0x28
HEADER_SIZE = 0x1C
ORIGIN_SCALE = 32.0


class Hitbox:
    __slots__ = ("flags", "center", "min_delta", "max_delta")

    def __init__(self, flags, center, min_delta, max_delta):
        self.flags = flags
        self.center = center
        self.min_delta = min_delta
        self.max_delta = max_delta

    def as_dict(self):
        return {
            "flags": self.flags,
            "center": list(self.center),
            "min_delta": list(self.min_delta),
            "max_delta": list(self.max_delta),
        }

    def __repr__(self):
        return "<Hitbox flags=%08X center=%s>" % (self.flags, self.center)


class SVTXFrame:
    """One frame of an SVTX entry."""

    __slots__ = (
        "index", "origin", "unknown_06", "view_space_offset", "vertex_count",
        "hitbox_count", "start", "hitboxes", "points", "point_bytes",
        "trailing_bytes", "item_start", "item_end", "source",
    )

    def __init__(self, index, origin, view_space_offset, vertex_count,
                 hitbox_count, start, hitboxes, points, point_bytes,
                 trailing_bytes, item_start, item_end, source,
                 unknown_06=0):
        self.index = index
        self.origin = origin                      # tuple of 3 floats
        self.unknown_06 = unknown_06
        self.view_space_offset = view_space_offset
        self.vertex_count = vertex_count
        self.hitbox_count = hitbox_count
        self.start = start                        # offset inside source
        self.hitboxes = hitboxes
        self.points = points                      # list of (x,y,z) floats
        self.point_bytes = point_bytes            # list of (x,y,z) u8
        self.trailing_bytes = trailing_bytes
        self.item_start = item_start
        self.item_end = item_end
        self.source = source                      # bytes of the whole entry

    # -- convenience -----------------------------------------------------
    @property
    def point_count(self):
        return len(self.points)

    @property
    def start_relative(self):
        """Vertex-data offset relative to the start of this frame."""
        return self.start - self.item_start

    def as_dict(self, with_points=True):
        d = {
            "index": self.index,
            "origin": list(self.origin),
            "unknown_06": self.unknown_06,
            "view_space_offset": list(self.view_space_offset),
            "vertex_count": self.vertex_count,
            "hitbox_count": self.hitbox_count,
            "start": self.start,
            "start_relative": self.start_relative,
            "item_start": self.item_start,
            "item_end": self.item_end,
            "trailing_bytes": self.trailing_bytes,
            "hitboxes": [h.as_dict() for h in self.hitboxes],
        }
        if with_points:
            d["points"] = [list(p) for p in self.points]
        else:
            d["point_count"] = len(self.points)
        return d

    def __repr__(self):
        return "<SVTXFrame %d origin=%s verts=%d start=0x%X>" % (
            self.index, self.origin, self.vertex_count, self.start)


def _u32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


def _i16(buf, off):
    return struct.unpack_from("<h", buf, off)[0]


def _i32(buf, off):
    return struct.unpack_from("<i", buf, off)[0]


def _i32v3(buf, off):
    return struct.unpack_from("<3i", buf, off)


def _byte(buf, off):
    """getUint8 with a zero fill past the end (the reference reads into the
    next item for the last vertex of a few frames; see module docstring)."""
    if 0 <= off < len(buf):
        return buf[off]
    return 0


def parse_svtx(entry):
    """Decode an SVTX ``nsf.Entry`` into a list of :class:`SVTXFrame`.

    ``entry.raw`` starts at the entry header, so the item table values are
    directly usable as offsets into that buffer (this is the same arithmetic
    the reference does with absolute file offsets).
    """
    buf = entry.raw
    if len(buf) < 0x10:
        raise ValueError("SVTX entry too short")
    magic = _u32(buf, 0)
    if (magic & 0xFFFF) != 0xFFFF:
        raise ValueError("bad SVTX entry magic %08X" % magic)
    item_count = _u32(buf, 0xC)
    if item_count == 0:
        return []
    offsets = list(struct.unpack_from("<%dI" % (item_count + 1), buf, 0x10))

    frames = []
    for i in range(item_count):
        frame_start = offsets[i]
        frame_end = offsets[i + 1]
        if frame_start + HEADER_SIZE > len(buf):
            raise ValueError("SVTX frame %d header out of range" % i)

        ox = _i16(buf, frame_start + 0x00) / ORIGIN_SCALE
        oy = _i16(buf, frame_start + 0x02) / ORIGIN_SCALE
        oz = _i16(buf, frame_start + 0x04) / ORIGIN_SCALE
        origin = (ox, oy, oz)

        vx = _i16(buf, frame_start + 0x08)
        vy = _i16(buf, frame_start + 0x0A)
        vz = _i32(buf, frame_start + 0x0C)
        view_space_offset = (vx, vy, vz)

        vertex_count = _u32(buf, frame_start + 0x10)
        hitbox_count = _u32(buf, frame_start + 0x14)
        start = frame_start + _u32(buf, frame_start + 0x18)

        hitboxes = []
        offs = frame_start + HEADER_SIZE
        for _j in range(hitbox_count):
            hitboxes.append(Hitbox(
                _u32(buf, offs + 0x00),
                _i32v3(buf, offs + 0x04),
                _i32v3(buf, offs + 0x10),
                _i32v3(buf, offs + 0x1C),
            ))
            offs += HITBOX_SIZE

        # The reference loops until the next frame offset.  We stop after
        # vertex_count vertices (see module docstring) and report padding.
        points = []
        point_bytes = []
        for k in range(vertex_count):
            o = start + 3 * k
            bx = _byte(buf, o + 0)
            bz = _byte(buf, o + 1)
            by = _byte(buf, o + 2)
            point_bytes.append((bx, by, bz))
            points.append((bx + ox, by + oy, bz + oz))

        consumed = start + 3 * vertex_count
        trailing = max(0, frame_end - consumed)

        frames.append(SVTXFrame(
            i, origin, view_space_offset, vertex_count, hitbox_count, start,
            hitboxes, points, point_bytes, trailing, frame_start, frame_end,
            buf, _u16(buf, frame_start + 0x06)))
    return frames


def _u16(buf, off):
    return struct.unpack_from("<H", buf, off)[0]


def generate_vertices(entry_or_frame, compression=None):
    """Return ``[(x,y,z), ...]`` byte coordinates for one frame.

    ``compression`` is the list of :class:`tgeo.CompressedVertex` instructions
    (empty/None for uncompressed SVTX).  Mirrors ``generateVertices`` in the
    reference, including the 4-byte output stride (we drop the pad byte).
    """
    if not isinstance(entry_or_frame, SVTXFrame):
        frames = parse_svtx(entry_or_frame)
        if not frames:
            return []
        frame = frames[0]
    else:
        frame = entry_or_frame

    buf = frame.source
    out = []
    if not compression:
        offs = frame.start
        for _i in range(frame.vertex_count):
            out.append((_byte(buf, offs + 0),
                        _byte(buf, offs + 2),
                        _byte(buf, offs + 1)))
            offs += 3
        return out

    stream = SignedBitStream(buf)
    stream.offs = frame.start
    tmp = [0, 0, 0]
    for inst in compression:
        for i in range(3):
            val = stream.read(inst.bit_counts[i])
            if inst.bit_counts[i] == 8:
                tmp[i] = val
            else:
                tmp[i] += val + inst.offsets[i]
            tmp[i] &= 0xFF
        out.append((tmp[0], tmp[1], tmp[2]))
    return out


class SignedBitStream:
    """Port of the reference's signed MSB-first bit reader.

    ``offs`` counts bytes inside the buffer.  The reference reads a u16 at
    ``offs ^ 2`` (PSX byte-swapped 32-bit words) and shifts it into a 32-bit
    sink.  Values are sign-extended by ``read``.
    """

    __slots__ = ("buf", "offs", "nbits", "sink")

    def __init__(self, buf):
        self.buf = buf
        self.offs = 0
        self.nbits = 0
        self.sink = 0

    def reset(self):
        self.nbits = 0
        self.sink = 0

    def fill(self, nbits):
        while self.nbits < nbits:
            o = self.offs ^ 2
            if o + 2 <= len(self.buf):
                b = struct.unpack_from("<H", self.buf, o)[0]
            else:
                b = 0
            self.sink = (self.sink | (b << (32 - 16 - self.nbits))) & 0xFFFFFFFF
            self.nbits += 16
            self.offs += 2

    def peek(self, nbits):
        return self.sink >> (32 - nbits)

    def eat(self, nbits):
        self.sink = (self.sink << nbits) & 0xFFFFFFFF
        self.nbits -= nbits

    def read(self, nbits):
        if nbits <= 0:
            return 0
        self.fill(nbits)
        val = self.peek(nbits)
        self.eat(nbits)
        if val >= (1 << (nbits - 1)):
            val -= (1 << nbits)
        return val


if __name__ == "__main__":
    import os
    import sys
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import cb3

    level = int(sys.argv[1], 0) if len(sys.argv) > 1 else 0x0B
    name = sys.argv[2] if len(sys.argv) > 2 else "BA10V"
    arc = cb3.load_level(level)
    entry = arc.entry(name)
    if entry is None:
        raise SystemExit("entry %s not found" % name)
    frames = parse_svtx(entry)
    print("%s: %d frames" % (name, len(frames)))
    for f in frames[:4]:
        print("  ", f, "points", len(f.points))
