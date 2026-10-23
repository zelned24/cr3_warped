"""ZDAT (zone database, chunk type 7) decoding for Crash Bandicoot: Warped.

Everything here follows the public viewer
``tools/reference/crashwarped_bin.ts`` (noclip.website, MIT) and was re-checked
against the retail USA disc (SCUS-94244).  A ZDAT entry is a small header plus
a list of *series databases* (key/value stores) and per-object databases.

Entry layout as exposed by ``nsf.Entry.items`` (the reference sees the same
bytes as ``offsets[]``):

  item 0  header, 856 bytes (0x358) in every level checked
  item 1  "info" block; if infoCount > 1 its first 12 bytes are an i32 vec3
          (``offset_vector``, used by the reference when placing object paths)
  item 2+ info_count .. +dbCount      zone databases
  item 2+ info_count+dbCount ..       one database per object/entity

Header fields (all little-endian u32 unless noted):

  0x000 u32  zone_index            (0x0B: 3..7; 0x13: 0)
  0x004 u32  sky_wgeo_eid          (0x0B: sk1bW for every zone, 0 if none)
  0x034 u32  world_model_eid[6]    stride 0x30, until 0x184
               -> 0x034, 0x064, 0x094, 0x0C4, 0x0F4, 0x124
  0x184 u32  info_count            (reference parseZDAT)
  0x188 u32  db_count
  0x18C u32  object_count
  0x190 u32  other_name_count
  0x194 u32  other_name_eid[other_name_count]

Database layout (reference ``parseZDATDatabase``):

  0x00 u32  byte size of the database (observed == entry item length)
  0x04 u32  0
  0x08 u32  0
  0x0C u16  series_count
  0x10 ...  series_count * 8-byte series headers:
        +0x0 u16 key
        +0x2 u16 data_offset   (relative to database_start + 0x0C)
        +0x4 u8  format        (ValueFormat bits, see below)
        +0x5 u8  stride        (bytes per value)
        +0x6 u16 value_count

ValueFormat bits (reference enum, same numeric values):

  0x01 u8   0x02 u16   0x03 s32 (S32_3)  0x04 s32 (S32_4)
  0x05 s32 (S32_5)     0x06 s16 vec3      0x07 s32 vec3
  0x11 s8   0x12 s16   0x13 s32 (S32_13)
  0x1F type mask       0x20 TIMES flag    0x40 VARIABLE_LENGTH flag
  0x80 seen on many series, ignored by the reference mask (UNVERIFIED)

Each series data block starts with the per-value *element count*:

  * VARIABLE_LENGTH: one u16 size per value;
  * otherwise a single u16 size repeated value_count times;
  * TIMES: value_count u16 timestamps follow, then align 4.

Entities (object databases) use these keys:

  0x002C  u8[]  ASCII object name, e.g. "obj_willy", "obj_butterflies#2"
  0x0047  s32   geometry format        (may be absent)
  0x004B  s16x3[] path waypoints       (reference DatabaseKey.PATH)
  0x009F  s32   lookup index           (reference LOOKUP_INDEX)
  0x00A4  s32[] object parameters      (reference OBJECT_PARAMETERS)
  0x00A9  s32   object file index -> NSD classNameList -> GOOL name
  0x00AA  s32   object id              (index into the GOOL state table, see
                                        ``resolve_entities``; 255 = none)
  0x030E  s32   path scale shift       (reference PATH_SCALE_SHIFT)

The reference computes object positions as
``(offset_vector + path_point * (1 << shift)) * 0x100``.  ``Entity.positions``
stores that reference-space result; ``Entity.path`` keeps the raw s16 values.
Converting those coordinates to the WGEO world space used by
``tools/export_levels.py`` is *not* solved yet (TODO/UNVERIFIED, see
docs/assets/ZDAT.md).
"""
import struct
import hashlib

import nsf


# ---------------------------------------------------------------------------
# value formats
# ---------------------------------------------------------------------------
U8 = 0x01
U16 = 0x02
S32_3 = 0x03
S32_4 = 0x04
S32_5 = 0x05
S16_VEC = 0x06
S32_VEC = 0x07
S8 = 0x11
S16 = 0x12
S32_13 = 0x13
TYPE_MASK = 0x1F
TIMES = 0x20
VARIABLE_LENGTH = 0x40

VALUE_FORMAT_NAMES = {
    U8: "u8", U16: "u16",
    S32_3: "s32_3", S32_4: "s32_4", S32_5: "s32_5",
    S16_VEC: "s16_vec3", S32_VEC: "s32_vec3",
    S8: "s8", S16: "s16", S32_13: "s32_13",
}

VALUE_STRIDES = {
    U8: 1, S8: 1,
    U16: 2, S16: 2,
    S32_3: 4, S32_4: 4, S32_5: 4, S32_13: 4,
    S16_VEC: 6, S32_VEC: 12,
}

# Reference DatabaseKey enum (only those names are confirmed).
DATABASE_KEYS = {
    0x47: "geometry_format",
    0x4B: "path",
    0x9F: "lookup_index",
    0xA4: "object_parameters",
    0xA9: "object_file",
    0xAA: "object_id",
    0x185: "level_flags",
    0x30E: "path_scale_shift",
}

# Keys seen in object databases, named only when the evidence is strong.
OBJECT_KEYS = {
    0x2C: "name",
    0x47: "geometry_format",
    0x4B: "path",
    0x9F: "lookup_index",
    0xA4: "object_parameters",
    0xA9: "object_file",
    0xAA: "object_id",
    0x30E: "path_scale_shift",
}


def _align(value, alignment=4):
    return (value + alignment - 1) & ~(alignment - 1)


def _name_of(eid):
    return nsf.eid_str(eid) if eid else None


# ---------------------------------------------------------------------------
# series / database
# ---------------------------------------------------------------------------
class Series:
    """One key/value series inside a ZDAT database."""

    __slots__ = ("key", "format", "value_type", "stride", "count",
                 "values", "times", "data_offset", "header_offset")

    def __init__(self, key, fmt, stride, count, values, times,
                 data_offset, header_offset):
        self.key = key
        self.format = fmt
        self.value_type = fmt & TYPE_MASK
        self.stride = stride
        self.count = count
        self.values = values                # [count][elementCount] items
        self.times = times
        self.data_offset = data_offset
        self.header_offset = header_offset

    @property
    def format_name(self):
        return VALUE_FORMAT_NAMES.get(self.value_type, "unknown_%02x" % self.value_type)

    @property
    def key_name(self):
        return DATABASE_KEYS.get(self.key) or OBJECT_KEYS.get(self.key)

    def single(self, default=None):
        if self.count >= 1 and len(self.values[0]) >= 1:
            return self.values[0][0]
        return default

    def __repr__(self):
        return ("<Series %04X %s count=%d stride=%d>"
                % (self.key, self.format_name, self.count, self.stride))

    def as_dict(self, max_values=None):
        values = self.values
        truncated = False
        if max_values is not None and len(values) > max_values:
            values = values[:max_values]
            truncated = True
        out = {
            "key": self.key,
            "key_name": self.key_name,
            "format": self.format,
            "format_name": self.format_name,
            "stride": self.stride,
            "count": self.count,
            "times": list(self.times),
            "values": values,
        }
        if truncated:
            out["truncated"] = True
        return out


class Database:
    """A parsed ZDAT key/value database."""

    __slots__ = ("index", "start", "size", "series", "by_key")

    def __init__(self, index, start, size, series):
        self.index = index
        self.start = start
        self.size = size
        self.series = series
        self.by_key = {s.key: s for s in series}

    def series_of(self, key):
        return self.by_key.get(key)

    def scalar(self, key, default=None):
        s = self.by_key.get(key)
        return s.single(default) if s is not None else default

    def vec3_series(self, key):
        s = self.by_key.get(key)
        if s is None:
            return None
        return s.values

    def __repr__(self):
        return "<Database %d series=%d size=%d>" % (
            self.index, len(self.series), self.size)

    def as_dict(self, max_values=None):
        return {
            "index": self.index,
            "start": self.start,
            "size": self.size,
            "series": [s.as_dict(max_values) for s in self.series],
        }


def parse_database(buf, start=0, index=0):
    """Parse one ZDAT database from ``buf`` (a complete ZDAT item)."""
    if start + 0x10 > len(buf):
        raise ValueError("database header out of range")
    size = struct.unpack_from("<I", buf, start)[0]
    series_count = struct.unpack_from("<H", buf, start + 0xC)[0]
    series = []
    offs = start + 0x10
    for i in range(series_count):
        if offs + 8 > len(buf):
            raise ValueError("series header %d out of range" % i)
        key, rel, fmt, stride, count = struct.unpack_from("<HHBBH", buf, offs)
        header_offset = offs
        data_start = start + 0xC + rel
        valtype = fmt & TYPE_MASK
        if valtype not in VALUE_STRIDES:
            raise ValueError("bad value format %02X for key %04X" % (fmt, key))
        if stride != VALUE_STRIDES[valtype]:
            # The reference asserts equality; keep the file value but record
            # it in the repr so a mismatch is visible.
            pass

        valoffs = data_start
        sizes = []
        if fmt & VARIABLE_LENGTH:
            for _ in range(count):
                sizes.append(struct.unpack_from("<H", buf, valoffs)[0])
                valoffs += 2
        else:
            value_size = struct.unpack_from("<H", buf, valoffs)[0]
            valoffs += 2
            sizes = [value_size] * count
        times = []
        if fmt & TIMES:
            for _ in range(count):
                times.append(struct.unpack_from("<H", buf, valoffs)[0])
                valoffs += 2
        valoffs = _align(valoffs)

        values = []
        for k in range(count):
            sub = []
            for _ in range(sizes[k]):
                if valtype == U8:
                    val = buf[valoffs]
                elif valtype == S8:
                    val = struct.unpack_from("<b", buf, valoffs)[0]
                elif valtype == S16:
                    val = struct.unpack_from("<h", buf, valoffs)[0]
                elif valtype == U16:
                    val = struct.unpack_from("<H", buf, valoffs)[0]
                elif valtype in (S32_3, S32_4, S32_5, S32_13):
                    val = struct.unpack_from("<i", buf, valoffs)[0]
                elif valtype == S16_VEC:
                    val = struct.unpack_from("<3h", buf, valoffs)
                elif valtype == S32_VEC:
                    val = struct.unpack_from("<3i", buf, valoffs)
                else:                       # pragma: no cover - guarded above
                    val = None
                sub.append(val)
                valoffs += stride
            values.append(sub)
        series.append(Series(key, fmt, stride, count, values, times,
                             data_start, header_offset))
        offs += 8
    return Database(index, start, size, series)


# ---------------------------------------------------------------------------
# GOOL object-state table (minimal reader, only for entity resolution)
# ---------------------------------------------------------------------------
class GoolModel:
    __slots__ = ("index", "offset", "kind", "geo_name", "vtx_name",
                 "tex_name", "count")

    def __init__(self, index, offset, kind, geo_name=None, vtx_name=None,
                 tex_name=None, count=0):
        self.index = index
        self.offset = offset
        self.kind = kind
        self.geo_name = geo_name     # TGEO name ("MESH")
        self.vtx_name = vtx_name     # SVTX name ("MESH")
        self.tex_name = tex_name     # texture page  ("SPRITE"/"QUAD")
        self.count = count

    def as_dict(self):
        return {
            "index": self.index,
            "offset": self.offset,
            "kind": self.kind,
            "geo": self.geo_name,
            "vtx": self.vtx_name,
            "texture": self.tex_name,
            "count": self.count,
        }


class GoolState:
    __slots__ = ("index", "motion_flags", "interact_flags", "alt_file_index",
                 "alt_name", "on_signal", "pre_run", "script")

    def __init__(self, index, motion_flags, interact_flags, alt_file_index,
                 alt_name, on_signal, pre_run, script):
        self.index = index
        self.motion_flags = motion_flags
        self.interact_flags = interact_flags
        self.alt_file_index = alt_file_index
        self.alt_name = alt_name
        self.on_signal = on_signal
        self.pre_run = pre_run
        self.script = script

    def as_dict(self):
        return {
            "index": self.index,
            "motion_flags": self.motion_flags,
            "interact_flags": self.interact_flags,
            "alt_file_index": self.alt_file_index,
            "alt_file": self.alt_name,
            "on_signal": self.on_signal,
            "pre_run": self.pre_run,
            "script": self.script,
        }


class GoolTable:
    """The pieces of a GOOL entry needed to resolve ZDAT objects."""

    def __init__(self, name, category, states, models, object_indices,
                 constants):
        self.name = name
        self.category = category
        self.states = states
        self.models = models
        self.object_indices = object_indices
        self.constants = constants

    def model(self, index):
        if 0 <= index < len(self.models):
            return self.models[index]
        return None

    def as_dict(self):
        return {
            "name": self.name,
            "category": self.category,
            "state_count": len(self.states),
            "model_count": len(self.models),
            "object_index_count": len(self.object_indices),
        }


_GOOL_CACHE = {}


def parse_gool_table(entry, class_names=None):
    """Minimal GOOL reader: model table + object/state tables.

    A full GOOL/script parser is out of scope here; this only exposes the
    records ENTITY resolution needs.  Layout (reference ``parseGOOL``):

      item 0  +0x05 u8 category, +0x0C u32 stack start, +0x10 u32 maxSignal
      item 2  constants: u32 EIDs used by states (``altFileIndex``)
      item 3  int16[]: maxSignal signal indices, then the object-index table
      item 4  states, 0x10 bytes each
      item 5  model records
    """
    # EIDs are reused by different level archives. Include section boundaries:
    # the same concatenation split differently is not the same GOOL entry.
    digest = hashlib.sha256()
    for item in entry.items:
        digest.update(struct.pack("<Q", len(item)))
        digest.update(item)
    key = (entry.eid, digest.digest())
    cached = _GOOL_CACHE.get(key)
    if cached is not None:
        return cached

    items = entry.items
    if len(items) < 5:
        table = GoolTable(entry.name, -1, [], [], [], [])
        _GOOL_CACHE[key] = table
        return table

    buf = b"".join(items)
    offs = [0]
    for it in items:
        offs.append(offs[-1] + len(it))

    category = buf[offs[0] + 5]
    max_signal = struct.unpack_from("<I", buf, offs[0] + 0x10)[0]

    # constants (item 2) hold EIDs referenced by state altFileIndex
    constants = []
    if len(items) > 2:
        for i in range(len(items[2]) // 4):
            constants.append(nsf.eid_str(
                struct.unpack_from("<I", buf, offs[2] + 4 * i)[0]))

    # object index table follows the signal index table in item 3
    object_indices = []
    start3 = offs[3] + 2 * max_signal
    for o in range(start3, offs[3] + len(items[3]), 2):
        object_indices.append(struct.unpack_from("<h", buf, o)[0])

    # states: 0x10 bytes each in item 4
    states = []
    if len(items) > 4:
        count = len(items[4]) // 0x10
        for i in range(count):
            o = offs[4] + i * 0x10
            motion, interact = struct.unpack_from("<II", buf, o)
            alt_index = struct.unpack_from("<H", buf, o + 0x8)[0]
            alt_name = (constants[alt_index]
                        if 0 <= alt_index < len(constants) else None)
            on_signal = struct.unpack_from("<H", buf, o + 0xA)[0]
            pre_run = struct.unpack_from("<H", buf, o + 0xC)[0]
            script = struct.unpack_from("<H", buf, o + 0xE)[0]
            states.append(GoolState(
                i, motion, interact, alt_index, alt_name,
                None if on_signal == 0x3FFF else (on_signal & 0x7FFF),
                None if pre_run == 0x3FFF else (pre_run & 0x7FFF),
                script & 0x7FFF))

    # models: records in item 5
    models = []
    if len(items) > 5:
        o = offs[5]
        end = offs[6] if len(items) > 6 else offs[5] + len(items[5])
        index = 0
        while o < end:
            kind = buf[o]
            if kind == 1:                       # MESH (0x14 bytes)
                if o + 0x14 > len(buf):
                    break
                geo = nsf.eid_str(struct.unpack_from("<I", buf, o + 0xC)[0])
                vtx = nsf.eid_str(struct.unpack_from("<I", buf, o + 0x10)[0])
                models.append(GoolModel(index, o - offs[5], "MESH", geo, vtx))
                o += 0x14
            elif kind == 2:                     # SPRITE (8 + 0x10 * count)
                if o + 8 > len(buf):
                    break
                count = struct.unpack_from("<H", buf, o + 2)[0]
                tex = nsf.eid_str(struct.unpack_from("<I", buf, o + 4)[0])
                models.append(GoolModel(index, o - offs[5], "SPRITE",
                                        tex_name=tex, count=count))
                o += 8 + 0x10 * count
            elif kind == 5:                     # QUAD_LIST (0xC + 0x18 * n)
                if o + 0xC > len(buf):
                    break
                frames, rects = struct.unpack_from("<HH", buf, o + 2)
                tex = nsf.eid_str(struct.unpack_from("<I", buf, o + 4)[0])
                models.append(GoolModel(index, o - offs[5], "QUAD",
                                        tex_name=tex, count=frames * rects))
                o += 0xC + 0x18 * frames * rects
            elif kind == 3:                     # unknown record, skip
                if o + 8 > len(buf):
                    break
                count = struct.unpack_from("<H", buf, o + 2)[0]
                models.append(GoolModel(index, o - offs[5], "TYPE3",
                                        count=count))
                o += 8 + 0x14 * count
            else:
                models.append(GoolModel(index, o - offs[5], "UNKNOWN_%02X" % kind))
                break
            index += 1

    table = GoolTable(entry.name, category, states, models, object_indices,
                      constants)
    _GOOL_CACHE[key] = table
    return table


# ---------------------------------------------------------------------------
# objects / entities
# ---------------------------------------------------------------------------
class Entity:
    """One object placement stored in a ZDAT."""

    __slots__ = ("index", "db", "name", "file_index", "file_name",
                 "object_id", "lookup_index", "geometry_format",
                 "path_scale_shift", "path", "positions", "parameters",
                 "extra_keys", "gool", "gool_found", "state_index", "state",
                 "model_options", "model", "resolution", "unresolved_reason")

    def __init__(self):
        self.index = 0
        self.db = None
        self.name = None
        self.file_index = None
        self.file_name = None
        self.object_id = None
        self.lookup_index = None
        self.geometry_format = None
        self.path_scale_shift = 0
        self.path = []
        self.positions = []
        self.parameters = []
        self.extra_keys = {}
        self.gool = None
        self.gool_found = False
        self.state_index = None
        self.state = None
        self.model_options = []
        self.model = None
        self.resolution = "unresolved"
        self.unresolved_reason = None

    @property
    def position(self):
        return self.positions[0] if self.positions else None

    def as_dict(self):
        return {
            "index": self.index,
            "name": self.name,
            "file_index": self.file_index,
            "file": self.file_name,
            "object_id": self.object_id,
            "lookup_index": self.lookup_index,
            "geometry_format": self.geometry_format,
            "path_scale_shift": self.path_scale_shift,
            "path": [list(p) for p in self.path],
            "positions": [list(p) for p in self.positions],
            "position": list(self.position) if self.position else None,
            "parameters": list(self.parameters),
            "rotation": None,
            "scale": None,
            "flags": None,
            "extra_keys": {("%04X" % k): v for k, v in self.extra_keys.items()},
            "gool": self.gool,
            "gool_found": self.gool_found,
            "state_index": self.state_index,
            "state": self.state.as_dict() if self.state is not None else None,
            "model_options": [m.as_dict() for m in self.model_options],
            "model": ({"kind": self.model.kind, "geo": self.model.geo_name,
                       "vtx": self.model.vtx_name}
                      if self.model is not None else None),
            "resolution": self.resolution,
            "unresolved_reason": self.unresolved_reason,
        }

    def __repr__(self):
        return "<Entity %d %s id=%s %s>" % (
            self.index, self.name, self.object_id, self.resolution)


def _read_entity(db, index, class_names, offset_vector):
    e = Entity()
    e.index = index
    e.db = db

    raw_name = db.series_of(0x2C)
    if raw_name is not None and raw_name.values and raw_name.values[0]:
        codes = [c for c in raw_name.values[0] if c]
        e.name = bytes(bytearray(codes)).decode("latin-1")
    e.file_index = db.scalar(0xA9)
    e.object_id = db.scalar(0xAA)
    e.lookup_index = db.scalar(0x9F)
    e.geometry_format = db.scalar(0x47)
    shift = db.scalar(0x30E, 0)
    e.path_scale_shift = shift if isinstance(shift, int) else 0

    if class_names and e.file_index is not None and 0 <= e.file_index < len(class_names):
        e.file_name = class_names[e.file_index]

    path_series = db.series_of(0x4B)
    if path_series is not None and path_series.values:
        e.path = list(path_series.values[0])

    params = db.series_of(0xA4)
    if params is not None and params.values:
        e.parameters = list(params.values[0])

    # reference: v = (offset + v * (1 << shift)) * 0x100
    scale = 1 << max(0, min(30, e.path_scale_shift))
    for p in e.path:
        if not (isinstance(p, tuple) and len(p) >= 3):
            continue
        if offset_vector is not None:
            x = (offset_vector[0] + p[0] * scale) * 0x100
            y = (offset_vector[1] + p[1] * scale) * 0x100
            z = (offset_vector[2] + p[2] * scale) * 0x100
        else:
            x = p[0] * scale * 0x100
            y = p[1] * scale * 0x100
            z = p[2] * scale * 0x100
        e.positions.append((x, y, z))

    for s in db.series:
        if s.key in OBJECT_KEYS:
            continue
        e.extra_keys[s.key] = s.values[0] if s.values else []
    return e


def resolve_entities(zdat, archive, class_names):
    """Resolve every entity to its GOOL file and object state.

    ``OBJECT_FILE`` indexes the NSD ``classNameList`` (the reference passes
    that list to ``parseZDAT``), which holds GOOL names.  ``OBJECT_ID``
    indexes the GOOL object-index table in item 3; that table maps it to a
    **state index** (``0xFF`` = no state).  Measured on the retail disc, the
    mapped value is always inside the GOOL state table (0x0B: 198/198 once
    the two-level lookup is used), while ``OBJECT_ID`` alone overflows it for
    26 entities.  Which mesh a state shows is chosen by GOOL bytecode,
    therefore a single TGEO/SVTX can only be reported when the behaviour has
    exactly one mesh model (otherwise the entity is marked
    ``state_resolved``/ambiguous).

    Returns a summary dict.
    """
    gools = {}
    summary = {"entities": 0, "gool_resolved": 0, "state_resolved": 0,
               "no_state": 0, "model_resolved": 0, "unresolved": 0}
    for ent in zdat.objects:
        summary["entities"] += 1
        if not ent.file_name:
            ent.unresolved_reason = "object_file out of classNameList range"
            summary["unresolved"] += 1
            continue
        entry = archive.entry(ent.file_name) if archive is not None else None
        if entry is None or entry.type != 11:
            ent.unresolved_reason = "GOOL entry %s not present in archive" % ent.file_name
            summary["unresolved"] += 1
            continue
        ent.gool_found = True
        ent.gool = ent.file_name
        summary["gool_resolved"] += 1

        table = gools.get(ent.file_name)
        if table is None:
            table = parse_gool_table(entry, class_names)
            gools[ent.file_name] = table

        ent.model_options = list(table.models)

        if (ent.object_id is None or ent.object_id < 0 or
                ent.object_id >= len(table.object_indices)):
            ent.unresolved_reason = ("object_id %s out of GOOL object-index "
                                     "range (%d)"
                                     % (ent.object_id, len(table.object_indices)))
            summary["unresolved"] += 1
            continue

        mapped = table.object_indices[ent.object_id]
        if mapped < 0 or mapped == 0xFF:
            ent.resolution = "no_state"
            ent.unresolved_reason = "object index 0xFF (no state)"
            summary["no_state"] += 1
            continue

        if mapped < len(table.states):
            ent.state_index = mapped
            ent.state = table.states[mapped]
            summary["state_resolved"] += 1
            ent.resolution = "state_resolved"

            mesh_models = [m for m in table.models if m.kind == "MESH"]
            if len(mesh_models) == 1:
                ent.model = mesh_models[0]
                ent.resolution = "model_resolved"
                summary["model_resolved"] += 1
            else:
                ent.unresolved_reason = ("model is selected by GOOL script "
                                         "(%d mesh candidates)" % len(mesh_models))
        else:
            ent.unresolved_reason = ("object index %d out of GOOL state range "
                                     "(%d)" % (mapped, len(table.states)))
            summary["unresolved"] += 1
    return summary


# ---------------------------------------------------------------------------
# ZDAT
# ---------------------------------------------------------------------------
class Zdat:
    """A decoded ZDAT entry: zone header + databases + entities."""

    __slots__ = ("name", "eid", "entry", "zone_index", "sky_wgeo_eid",
                 "sky_wgeo", "world_model_eids", "world_models",
                 "info_count", "db_count", "object_count", "other_name_count",
                 "other_names", "offset_vector", "databases", "objects",
                 "camera_candidates", "sections", "resolution_summary")

    def __init__(self):
        self.name = None
        self.eid = 0
        self.entry = None
        self.zone_index = None
        self.sky_wgeo_eid = 0
        self.sky_wgeo = None
        self.world_model_eids = []
        self.world_models = []
        self.info_count = 0
        self.db_count = 0
        self.object_count = 0
        self.other_name_count = 0
        self.other_names = []
        self.offset_vector = None
        self.databases = []
        self.objects = []
        self.camera_candidates = []
        self.sections = []
        self.resolution_summary = None

    def as_dict(self, max_values=None):
        return {
            "name": self.name,
            "eid": "0x%08X" % self.eid,
            "zone_index": self.zone_index,
            "sky_wgeo": self.sky_wgeo,
            "sky_wgeo_eid": ("0x%08X" % self.sky_wgeo_eid
                             if self.sky_wgeo_eid else None),
            "world_models": list(self.world_models),
            "world_model_eids": ["0x%08X" % e for e in self.world_model_eids],
            "info_count": self.info_count,
            "db_count": self.db_count,
            "object_count": self.object_count,
            "other_name_count": self.other_name_count,
            "other_names": list(self.other_names),
            "offset_vector": (list(self.offset_vector)
                              if self.offset_vector is not None else None),
            "databases": [d.as_dict(max_values) for d in self.databases],
            "entities": [e.as_dict() for e in self.objects],
            "camera_candidates": self.camera_candidates,
            "sections": list(self.sections),
        }

    def __repr__(self):
        return ("<ZDAT %s zone=%s db=%d obj=%d models=%d>"
                % (self.name, self.zone_index, self.db_count,
                   self.object_count, len(self.world_models)))


def parse_zdat(entry, class_names=None, archive=None):
    """Decode one ZDAT entry.

    ``class_names`` is the NSD classNameList (list of 128 EID strings) used to
    turn ``OBJECT_FILE`` into a GOOL name.  Pass ``archive`` (a
    ``cb3.Archive``) to also resolve entities with :func:`resolve_entities`.
    """
    z = Zdat()
    z.entry = entry
    z.name = entry.name
    z.eid = entry.eid
    items = entry.items
    if not items:
        raise ValueError("empty ZDAT entry")
    hdr = items[0]
    if len(hdr) < 0x194:
        raise ValueError("ZDAT header too small (%d bytes)" % len(hdr))

    z.zone_index = struct.unpack_from("<I", hdr, 0)[0]
    z.sky_wgeo_eid = struct.unpack_from("<I", hdr, 4)[0]
    z.sky_wgeo = _name_of(z.sky_wgeo_eid)

    for i in range(6):
        off = 0x34 + 0x30 * i
        if off + 4 > 0x184:
            break
        eid = struct.unpack_from("<I", hdr, off)[0]
        if eid:
            z.world_model_eids.append(eid)
            z.world_models.append(_name_of(eid))

    z.info_count, z.db_count, z.object_count, z.other_name_count = \
        struct.unpack_from("<4I", hdr, 0x184)
    for i in range(z.other_name_count):
        off = 0x194 + 4 * i
        if off + 4 > len(hdr):
            break
        z.other_names.append(_name_of(struct.unpack_from("<I", hdr, off)[0]))

    if z.info_count > 1 and len(items) > 1 and len(items[1]) >= 12:
        z.offset_vector = struct.unpack_from("<3i", items[1], 0)

    # zone databases: items[2 .. 2+db_count)
    for i in range(z.db_count):
        item_index = 2 + i
        if item_index >= len(items):
            break
        z.databases.append(parse_database(items[item_index], 0, i))

    # object databases: items[2+db_count .. ]
    for i in range(z.object_count):
        item_index = 2 + z.db_count + i
        if item_index >= len(items):
            break
        db = parse_database(items[item_index], 0, i)
        z.objects.append(_read_entity(db, i, class_names, z.offset_vector))

    # Camera data is not parsed by the reference viewer.  These databases are
    # the only ones that carry a path plus the [.., .., .., ..] key 0x142 and
    # key 0x131 track; they are reported as *candidates* only (UNVERIFIED).
    for db in z.databases:
        path_series = db.series_of(0x4B)
        if db.series_of(0x131) is not None or db.series_of(0x142) is not None:
            cam = {
                "db_index": db.index,
                "verified": False,
                "note": "candidate camera track (keys 0x131/0x142), UNVERIFIED",
                "path": (list(path_series.values[0])
                         if path_series is not None and path_series.values else []),
                "key_0131": (db.series_of(0x131).values
                             if db.series_of(0x131) is not None else None),
                "key_0142": (db.series_of(0x142).values
                             if db.series_of(0x142) is not None else None),
                "key_0119": (db.series_of(0x119).values
                             if db.series_of(0x119) is not None else None),
            }
            z.camera_candidates.append(cam)

    if archive is not None and class_names is not None:
        z.resolution_summary = resolve_entities(z, archive, class_names)
    return z


def class_names_from_nsd(nsd):
    """Return the 128-entry NSD classNameList (GOOL names).

    ``nsf.NSD`` calls these words ``level_eids`` at ``pos + 0x14``; the
    reference reads the same table at ``tableOffset + 0x10`` with 0x80
    entries.  Both point 4 bytes apart, so the reference offset is used here.
    """
    if nsd is None:
        return []
    base = 0x520 + 8 * nsd.num_list_entries
    out = []
    for i in range(0x80):
        off = base + 0x10 + 4 * i
        if off + 4 > len(nsd.buf):
            break
        out.append(nsf.eid_str(struct.unpack_from("<I", nsd.buf, off)[0]))
    return out


def parse_zdat_archive(archive, entry=None):
    """Convenience: parse one (or every) ZDAT of a ``cb3.Archive``."""
    class_names = class_names_from_nsd(archive.nsd)
    if entry is not None:
        return parse_zdat(entry, class_names, archive)
    return [parse_zdat(e, class_names, archive) for e in archive.of_type(7)]


def parse_checkpoints(nsd):
    """Parse the NSD checkpoint table (respawn points).

    Layout after ``tableOffset = 0x520 + 8 * numListEntries``:

      0x000 u32 checkpoint_count
      0x004 u32 0
      0x008 u32 level id
      0x00C u32 0x??  (0x0B: 0xC6, 0x13: 0x??) -- UNVERIFIED
      0x010 128 * u32 classNameList (GOOL names)
      0x2D0 ?  (padding / unknown)
      0x2DC count * 0x18 checkpoint records:
             +0x00 u32 zdat EID   (respawn zone)
             +0x04 u32 0
             +0x08 u32 index       (0x0B: 0,9,18,22,4,9 -- UNVERIFIED)
             +0x0C i32 x, +0x10 i32 y, +0x14 i32 z

    Cross-check for 0x0B: checkpoint 00_bZ = (12427939, 819200, 104242816)
    and the nearest entity (obj_willy, zone 01_bZ) = (12427776, 818944,
    104242944): same point within 303 units out of ~1e8.  This validates the
    reference ZDAT position formula (it lands in "path space", see ZDAT.md).
    """
    if nsd is None:
        return []
    base = 0x520 + 8 * nsd.num_list_entries
    if base + 0x2DC > len(nsd.buf):
        return []
    count = struct.unpack_from("<I", nsd.buf, base)[0]
    out = []
    for i in range(min(count, 0x100)):
        off = base + 0x2DC + i * 0x18
        if off + 0x18 > len(nsd.buf):
            break
        eid, unused, index, x, y, z = struct.unpack_from("<IIIiii",
                                                         nsd.buf, off)
        out.append({
            "index": i,
            "zdat": nsf.eid_str(eid) if eid else None,
            "zdat_eid": "0x%08X" % eid,
            "record_index": index,
            "position": [x, y, z],
        })
    return out
