"""Animation decoding for Crash Bandicoot: Warped (PSX) -> 3DS asset pipeline.

This module answers three questions from the retail disc bytes alone:

  1. *What are the poses?*   SVTX (entry type 1, ``...V``) holds the keyframes.
  2. *How long is a clip?*   GOOL (entry type 11, ``...C``) ``VertexGroup3``
     records name the SVTX entry and give the clip length in logic ticks.
  3. *How fast is a frame?*  Derived - see the timing rule below.

No GOOL bytecode is interpreted.  The only GOOL structure read is the frame
group table (item 5), which is a flat record array.

Frame timing rule (verified on this disc)
-----------------------------------------
An SVTX frame has **no** per-frame delay field.  Its header is fully accounted
for by ``0x1C + hitboxCount*0x28 + 3*specialVertexCount`` bytes (see
``validate_frame_header``), and the bit payload that follows is consumed by the
per-vertex codes with under 32 bits of slack, so there is no hidden timing
word.  All keyframes of a clip therefore have the **same** duration.

That duration comes from GOOL.  ``VertexGroup3.frames`` is the clip length in
logic ticks and the SVTX item count is the number of keyframes::

    ticks_per_frame = frames / keyframe_count

Two behaviours are observable disc-wide (see ``census`` in the tests):

* ``lerp == 0``  -> ``frames == keyframe_count`` in *every* record on the
  disc (crates, boxes): exactly one tick per keyframe.
* ``lerp == 1``  -> ``frames`` is close to ``2 * keyframe_count`` (measured
  1.33 .. 2.00): the engine interpolates, and a new pose lands about every
  two ticks.

So ``ticks_per_frame`` is an exact rational and is exported as ``num``/``den``;
``fps`` follows only once the tick rate is known.  The 30 Hz logic rate is an
**assumption** (``TICK_HZ_STATUS``) - nothing on the disc proves it here, so it
is flagged ``UNVERIFIED`` everywhere it is used.

Naming
------
The disc stores no animation names: a clip is identified by its SVTX EID
(``CrW0V``).  ``CLIP_NAMES`` below is an *inference* from the EID letter code
plus the measured motion of each clip; every clip carries ``name`` and
``name_confidence`` and ``--list`` prints the measurements so the guess can be
challenged.  There is no ``CrJ*`` (jump) entry anywhere on the disc.

Usage
-----
    python tools/anim.py --list
    python tools/anim.py --model Cr10V --json
    python tools/anim.py --model Cr10V --png strip.png
    python tools/anim.py --model DingC --json --out game_data/anim

``--model`` accepts an SVTX name (one clip) or a GOOL name (every clip the
GOOL references).  ``--out DIR`` writes ``<name>.json`` and ``index.json``
instead of only printing.

Python 3.11 stdlib only.  Output is deterministic (sorted keys, fixed float
precision, fixed zlib level).
"""
import argparse
import json
import math
import os
import struct
import sys
import zlib

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

import cb3
import nsf
import svtx
import tgeo

FORMAT = "cb3-anim-1"
DEFAULT_LEVEL = 0x02
TICK_HZ = 30.0
TICK_HZ_STATUS = "UNVERIFIED"

# SVTX frame header constants (see svtx.py; re-derived in validate_frame_header).
FRAME_HEADER = 0x1C
HITBOX_SIZE = 0x28

# GOOL VertexGroup3 record.
GROUP_SIZE = 20
GROUP_TYPE_VERTEX3 = 1

# ---------------------------------------------------------------------------
# clip naming (inference, not disc data)
# ---------------------------------------------------------------------------
#: SVTX EID -> (name, confidence, basis).  ``Cr10V``..``CrW3V`` are the nine
#: clips referenced by the ``WillC`` GOOL in all 36 level archives; the model
#: is ``Cr10G``.
CLIP_NAMES = {
    "Cr10V": ("idle", "high",
              "lowest per-frame motion for Crash (dXZ 0.36, dY 0.44 byte units) "
              "and tight loop closure (0.17 x one step); the control model named "
              "by TASKS.md F5"),
    "Cr20V": ("idle_alt", "medium",
              "second-lowest motion (0.57/0.51) and tight loop (0.16); 36 "
              "keyframes / 70 ticks is a long idle variant"),
    "CrW0V": ("walk", "high",
              "EID letter 'W'; 8x the idle motion (dXZ 3.08, dY 1.51) and "
              "loops (closure 0.32 x one step)"),
    "CrW2V": ("walk_alt_2", "low",
              "EID letter 'W' but only 2 keyframes / 3 ticks; a transition"),
    "CrW3V": ("walk_alt_3", "low",
              "EID letter 'W' but only 2 keyframes / 3 ticks; a transition"),
    "CrR0V": ("run", "medium",
              "EID letter 'R'; highest locomotion motion (dXZ 6.64, dY 6.59) "
              "and loops (closure 0.72 x one step)"),
    "CrS1V": ("spin", "medium",
              "EID letter 'S'; 5 keyframes / 9 ticks, one-shot (closure 1.43 x "
              "one step), large whole-body deltas"),
    "CrD1V": ("death", "medium",
              "EID letter 'D'; one-shot, ends far from the start pose "
              "(closure 1.91 x one step)"),
    "CrD2V": ("death_final", "low",
              "EID letter 'D'; single held pose (1 keyframe / 1 tick)"),
    # --- jump subsystem (C + J = Crash Jump) --------------------------------
    "CJS1V": ("jump_start", "high",
              "EID 'CJ'; 3 keyframes / 5 ticks and the most vertical motion of "
              "any Crash clip (dY 12.43 vs dXZ 2.59): a take-off"),
    "CJU1V": ("jump_up", "medium",
              "EID 'CJ' + 'U'; 8 keyframes / 15 ticks, dY 2.12: held airborne "
              "pose"),
    "CJF1V": ("jump_fall", "medium",
              "EID 'CJ' + 'F'; 17 keyframes / 17 ticks, the only Crash clip "
              "with lerp=0 (one tick per keyframe), dY 5.44: falling"),
    "CJL1V": ("jump_land", "medium",
              "EID 'CJ' + 'L'; 13 keyframes / 24 ticks, dY 2.89: landing"),
    "CJR1V": ("jump_run_1", "low",
              "EID 'CJ' + 'R'; 3 keyframes / 4 ticks, dY 9.51"),
    "CJR2V": ("jump_run_2", "low",
              "EID 'CJ' + 'R'; 2 keyframes / 3 ticks, dY 13.77"),
    "CJh1V": ("jump_high", "low",
              "EID 'CJ'; 11 keyframes / 21 ticks, dY 8.99"),
    # --- crouch ------------------------------------------------------------
    "CCw1V": ("crouch_walk", "medium",
              "EID 'CC' + 'w'; 10 keyframes / 19 ticks, dXZ 3.77 vs dY 1.32 "
              "(low, horizontal): a crawl"),
    "CCw2V": ("crouch_walk_alt", "low",
              "EID 'CC' + 'w'; 3 keyframes / 4 ticks"),
}

#: Clips used by ``--png`` when no explicit ``--clips`` is given.
DEFAULT_STRIP = ("idle", "walk", "run")


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------
def _r(value, digits=6):
    """Round a float for deterministic JSON."""
    if value is None:
        return None
    return round(float(value), digits)


def _u32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


class Group(object):
    """One GOOL ``VertexGroup3`` record."""

    __slots__ = ("frames", "lerp", "num", "model_eid", "eid", "offset",
                 "gool", "chunk_cid", "chunk_index", "entry_offset")

    def __init__(self, frames, lerp, num, model_eid, eid, offset,
                 gool, chunk_cid, chunk_index, entry_offset):
        self.frames = frames
        self.lerp = lerp
        self.num = num
        self.model_eid = model_eid
        self.eid = eid
        self.offset = offset              # offset of the record inside item 5
        self.gool = gool                  # GOOL entry name
        self.chunk_cid = chunk_cid
        self.chunk_index = chunk_index
        self.entry_offset = entry_offset  # GOOL entry offset inside the page

    @property
    def v_name(self):
        return nsf.eid_str(self.eid)

    @property
    def model_name(self):
        return nsf.eid_str(self.model_eid)

    def as_dict(self):
        return {
            "gool": self.gool,
            "level_page_cid": "0x%04X" % self.chunk_cid,
            "chunk_index": self.chunk_index,
            "entry_offset_in_page": "0x%X" % self.entry_offset,
            "group_offset_in_item5": "0x%X" % self.offset,
            "frames": self.frames,
            "lerp": self.lerp,
            "num": self.num,
            "model": self.model_name,
        }


def iter_groups(archive):
    """Yield every GOOL ``VertexGroup3`` record in an archive.

    Item 5 is a flat array of 20-byte records (``type=1``); parsing stops at
    the first record whose type is not 1 or at the last whole record.
    """
    for chunk in archive.chunks:
        for entry in chunk.entries:
            if entry.type != 11 or len(entry.items) < 6:
                continue
            data = entry.items[5]
            off = 0
            while off + GROUP_SIZE <= len(data):
                gtype, frames, lerp, num, model_eid, eid = struct.unpack_from(
                    "<hhiIII", data, off)
                if gtype != GROUP_TYPE_VERTEX3:
                    break
                yield Group(frames, lerp, num, model_eid, eid, off,
                            entry.name, chunk.cid, chunk.index, entry.offset)
                off += GROUP_SIZE


def find_group(archive, v_name):
    """Return the first GOOL group whose ``eid`` names *v_name* (or None)."""
    for group in iter_groups(archive):
        if group.v_name == v_name:
            return group
    return None


def group_census(levels=None):
    """Scan every level archive and tally ``frames`` vs keyframe count.

    Returns ``{"lerp0": {...}, "lerp1": {...}}`` describing the observed
    ``frames / keyframes`` ratios.  Used by ``--census`` (the evidence for the
    timing rule) and safe to reproduce: it only reads the NSF files.
    """
    if levels is None:
        levels = list(range(0x02, 0x3D))
    item_counts = {}
    ratios = {0: [], 1: []}
    multi = {0: [], 1: []}
    equal = {0: 0, 1: 0}
    total = {0: 0, 1: 0}
    for level in levels:
        try:
            archive = cb3.load_level(level)
        except (FileNotFoundError, ValueError):
            continue
        for chunk in archive.chunks:
            for entry in chunk.entries:
                if entry.type == 1:
                    item_counts[entry.name] = len(entry.items)
        for group in iter_groups(archive):
            n = item_counts.get(group.v_name)
            if not n:
                continue
            lerp = 1 if group.lerp else 0
            ratio = group.frames / float(n)
            ratios[lerp].append(ratio)
            if n > 1:
                multi[lerp].append(ratio)
            total[lerp] += 1
            if group.frames == n:
                equal[lerp] += 1
    out = {}
    for lerp in (0, 1):
        values = sorted(ratios[lerp])
        more = multi[lerp]
        median = None
        if values:
            mid = len(values) // 2
            median = (values[mid] if len(values) % 2
                      else (values[mid - 1] + values[mid]) / 2.0)
        out["lerp%d" % lerp] = {
            "records": total[lerp],
            "frames_equals_keyframes": equal[lerp],
            "ratio_min": _r(min(values)) if values else None,
            "ratio_max": _r(max(values)) if values else None,
            "ratio_median": _r(median),
            "multi_keyframe_records": len(more),
            "multi_ratio_min": _r(min(more)) if more else None,
            "multi_ratio_max": _r(max(more)) if more else None,
        }
    return out


# ---------------------------------------------------------------------------
# timing / motion
# ---------------------------------------------------------------------------
def timing_dict(keyframes, ticks_total, lerp):
    """Build the timing block for a clip.  See the module docstring."""
    if not keyframes:
        raise ValueError("clip has no keyframes")
    ticks_total = int(ticks_total)
    block = {
        "keyframes": keyframes,
        "ticks_total": ticks_total,
        "ticks_per_frame": {
            "num": ticks_total,
            "den": keyframes,
            "value": _r(ticks_total / float(keyframes)),
        },
        "seconds_per_frame": _r(ticks_total / (float(keyframes) * TICK_HZ)),
        "seconds_total": _r(ticks_total / TICK_HZ),
        "fps": _r(TICK_HZ * keyframes / float(ticks_total), 3),
        "fps_int": max(1, min(0xFFFF, int(round(TICK_HZ * keyframes / ticks_total)))),
        "interpolate": bool(lerp),
        "tick_hz": TICK_HZ,
        "tick_hz_status": TICK_HZ_STATUS,
        "rule": "frames are equally spaced; duration = GOOL VertexGroup3.frames / keyframes",
    }
    if ticks_total == keyframes:
        block["note"] = "lerp=0: exactly one tick per keyframe"
    return block


def _pose_distance(a, b):
    n = min(len(a), len(b))
    if not n:
        return 0.0
    total = 0
    for i in range(n):
        total += (abs(a[i][0] - b[i][0]) + abs(a[i][1] - b[i][1])
                  + abs(a[i][2] - b[i][2]))
    return total / (3.0 * n)


def motion_dict(poses):
    """Measured loop/motion statistics (byte units; these are *evidence*)."""
    n = len(poses)
    if n == 0:
        return {}
    step = 0.0
    dxz = 0.0
    dy = 0.0
    pairs = n if n > 1 else 1
    for i in range(pairs):
        a = poses[i]
        b = poses[(i + 1) % n] if n > 1 else poses[0]
        m = min(len(a), len(b))
        step += _pose_distance(a, b)
        for k in range(m):
            dxz += (abs(a[k][0] - b[k][0]) + abs(a[k][2] - b[k][2])) / (2.0 * m)
            dy += abs(a[k][1] - b[k][1]) / float(m)
    step /= pairs
    dxz /= pairs
    dy /= pairs
    closure = _pose_distance(poses[0], poses[-1]) if n > 1 else 0.0
    ratio = closure / step if step > 0 else 1.0
    return {
        "mean_step": _r(step),
        "mean_step_xz": _r(dxz),
        "mean_step_y": _r(dy),
        "loop_closure": _r(closure),
        "loop_closure_ratio": _r(ratio),
        # A clip loops when its ends meet at least as tightly as one normal
        # animation step.  Single-frame clips are static and count as looping.
        "loop": bool(n == 1 or ratio <= 1.0),
    }


# ---------------------------------------------------------------------------
# frame header validation (the proof that there is no timing field)
# ---------------------------------------------------------------------------
def validate_frame_header(entry, frames):
    """Check the SVTX header accounting for every frame.

    Returns a dict with per-frame evidence: the declared ``headerSize``, the
    computed ``0x1C + hitboxes*0x28 + 3*specialVertexCount``, the number of
    payload bytes and the number of bits left unused after the last vertex.
    ``specialVertexCount`` is derived as ``(headerSize - (0x1C + hb*0x28))/3``;
    a whole non-negative count, byte-identical across frames, plus ``slack``
    below 32 bits, leaves no room for an undocumented per-frame delay.
    """
    buf = entry.raw
    rows = []
    for frame in frames:
        start = frame.item_start
        header_size = _u32(buf, start + 0x18)
        fake = FRAME_HEADER + frame.hitbox_count * HITBOX_SIZE
        delta = header_size - fake
        specials = None
        if delta >= 0 and delta % 3 == 0:
            specials = delta // 3
        payload_start = start + fake
        payload_end = frame.item_end
        rows.append({
            "index": frame.index,
            "header_size": header_size,
            "computed_header_size": fake + (specials or 0) * 3,
            "hitbox_count": frame.hitbox_count,
            "special_vertices": specials,
            "vertex_count": frame.vertex_count,
            "item_bytes": frame.item_end - frame.item_start,
            "payload_bits": (payload_end - payload_start) * 8,
        })
    return rows


# ---------------------------------------------------------------------------
# clip decoding
# ---------------------------------------------------------------------------
class Clip(object):
    __slots__ = ("v_name", "v_entry", "level", "archive", "frames", "poses",
                 "group", "tgeo", "tgeo_entry", "name", "confidence", "basis")

    def __init__(self, v_name, v_entry, level, archive, frames, poses,
                 group, tgeo_obj, tgeo_entry):
        self.v_name = v_name
        self.v_entry = v_entry
        self.level = level
        self.archive = archive
        self.frames = frames
        self.poses = poses
        self.group = group
        self.tgeo = tgeo_obj
        self.tgeo_entry = tgeo_entry
        name, conf, basis = CLIP_NAMES.get(
            v_name, (v_name.lower(), "none", "no inference table entry"))
        self.name = name
        self.confidence = conf
        self.basis = basis


def load_clip(archive, v_name, level=None):
    """Decode one SVTX entry into a :class:`Clip` (poses in byte units)."""
    entry = archive.entry(v_name)
    if entry is None:
        raise KeyError("no SVTX entry %r in this archive" % v_name)
    if entry.type != 1:
        raise ValueError("%r is entry type %d, not SVTX (1)" % (v_name, entry.type))
    frames = svtx.parse_svtx(entry)
    if not frames:
        raise ValueError("%r has no frames" % v_name)

    group = find_group(archive, v_name)
    if group is not None:
        tgeo_entry = archive.entry(group.model_name)
    else:
        # Fallback: assume the matching "...G" entry (Cr10V -> Cr10G).
        tgeo_entry = archive.entry(v_name[:-1] + "G")
    if tgeo_entry is None:
        raise KeyError("no TGEO entry for %r" % v_name)
    tgeo_obj = tgeo.parse_tgeo(tgeo_entry)
    poses = [svtx.generate_vertices(f, tgeo_obj.compression) for f in frames]
    return Clip(v_name, entry, level, archive, frames, poses, group,
                tgeo_obj, tgeo_entry)


def clip_dict(clip, with_frames=True):
    """JSON-ready description of one clip."""
    keyframes = len(clip.frames)
    if clip.group is not None:
        ticks = clip.group.frames
        ticks_source = "gool"
    else:
        ticks = keyframes
        ticks_source = "fallback(1 tick/frame)"

    motion = motion_dict(clip.poses)
    timing = timing_dict(keyframes, ticks, clip.group.lerp if clip.group else 1)
    timing["ticks_source"] = ticks_source

    flags = 0
    if motion.get("loop"):
        flags |= 1

    out = {
        "name": clip.name,
        "name_confidence": clip.confidence,
        "name_basis": clip.basis,
        "entry": clip.v_name,
        "eid": "0x%08X" % clip.v_entry.eid,
        "model": clip.tgeo_entry.name,
        "model_eid": "0x%08X" % clip.tgeo_entry.eid,
        "timing": timing,
        "flags": flags,
        "loop": motion.get("loop", False),
        "metrics": motion,
        "source": {
            "disc": "SCUS-94244",
            "level": "0x%02X" % clip.level if clip.level is not None else None,
            "nsf": os.path.basename(clip.archive.nsf_path),
            "entry": clip.v_name,
            "entry_offset_in_page": "0x%X" % clip.v_entry.offset,
        },
    }
    if clip.group is not None:
        out["gool"] = clip.group.as_dict()
    else:
        out["gool"] = None
        out["timing"]["note"] = "no GOOL frame group found; 1 tick/frame assumed"
    if with_frames:
        num = timing["ticks_per_frame"]["num"]
        den = timing["ticks_per_frame"]["den"]
        out["frames"] = [
            {
                "index": i,
                "start_tick": {"num": i * num, "den": den},
                "duration_ticks": {"num": num, "den": den},
                "origin": [_r(v) for v in clip.frames[i].origin],
            }
            for i in range(keyframes)
        ]
    return out


def model_dict(clips, key):
    """Wrap a list of clips into the top-level JSON document."""
    first = clips[0]
    return {
        "format": FORMAT,
        "source": {
            "disc": "SCUS-94244",
            "level": "0x%02X" % first.level if first.level is not None else None,
            "nsf": os.path.basename(first.archive.nsf_path),
            "entry": key,
        },
        "tick_hz": TICK_HZ,
        "tick_hz_status": TICK_HZ_STATUS,
        "clips": [clip_dict(c) for c in clips],
    }


# ---------------------------------------------------------------------------
# PNG rasteriser (software, no dependencies)
# ---------------------------------------------------------------------------
_FONT_SRC = {
    "A": "01110 10001 10001 11111 10001 10001 10001",
    "B": "11110 10001 10001 11110 10001 10001 11110",
    "C": "01110 10001 10000 10000 10000 10001 01110",
    "D": "11110 10001 10001 10001 10001 10001 11110",
    "E": "11111 10000 10000 11110 10000 10000 11111",
    "F": "11111 10000 10000 11110 10000 10000 10000",
    "G": "01110 10001 10000 10111 10001 10001 01111",
    "H": "10001 10001 10001 11111 10001 10001 10001",
    "I": "11111 00100 00100 00100 00100 00100 11111",
    "J": "00111 00010 00010 00010 00010 10010 01100",
    "K": "10001 10010 10100 11000 10100 10010 10001",
    "L": "10000 10000 10000 10000 10000 10000 11111",
    "M": "10001 11011 10101 10101 10001 10001 10001",
    "N": "10001 11001 10101 10011 10001 10001 10001",
    "O": "01110 10001 10001 10001 10001 10001 01110",
    "P": "11110 10001 10001 11110 10000 10000 10000",
    "Q": "01110 10001 10001 10001 10101 10010 01101",
    "R": "11110 10001 10001 11110 10100 10010 10001",
    "S": "01111 10000 10000 01110 00001 00001 11110",
    "T": "11111 00100 00100 00100 00100 00100 00100",
    "U": "10001 10001 10001 10001 10001 10001 01110",
    "V": "10001 10001 10001 10001 10001 01010 00100",
    "W": "10001 10001 10001 10101 10101 11011 10001",
    "X": "10001 10001 01010 00100 01010 10001 10001",
    "Y": "10001 10001 01010 00100 00100 00100 00100",
    "Z": "11111 00001 00010 00100 01000 10000 11111",
    "0": "01110 10001 10011 10101 11001 10001 01110",
    "1": "00100 01100 00100 00100 00100 00100 01110",
    "2": "01110 10001 00001 00010 00100 01000 11111",
    "3": "11111 00010 00100 00010 00001 10001 01110",
    "4": "00010 00110 01010 10010 11111 00010 00010",
    "5": "11111 10000 11110 00001 00001 10001 01110",
    "6": "00110 01000 10000 11110 10001 10001 01110",
    "7": "11111 00001 00010 00100 01000 01000 01000",
    "8": "01110 10001 10001 01110 10001 10001 01110",
    "9": "01110 10001 10001 01111 00001 00010 01100",
    " ": "00000 00000 00000 00000 00000 00000 00000",
    "-": "00000 00000 00000 11111 00000 00000 00000",
    ".": "00000 00000 00000 00000 00000 01100 01100",
    ":": "00000 01100 01100 00000 01100 01100 00000",
    "/": "00001 00010 00010 00100 01000 01000 10000",
}
FONT_WIDTH = 5
FONT_HEIGHT = 7


def _font():
    table = {}
    for ch, rows in _FONT_SRC.items():
        table[ch] = [int(r, 2) for r in rows.split()]
    return table


FONT = _font()


class Canvas(object):
    """Tiny RGB canvas with a z-buffer and a 5x7 bitmap font."""

    def __init__(self, width, height, background=(18, 18, 24)):
        self.width = width
        self.height = height
        self.pixels = bytearray(background * (width * height))

    def fill_rect(self, x0, y0, x1, y1, color):
        x0 = max(0, x0)
        y0 = max(0, y0)
        x1 = min(self.width, x1)
        y1 = min(self.height, y1)
        for y in range(y0, y1):
            row = (y * self.width) * 3
            self.pixels[row + x0 * 3: row + x1 * 3] = bytes(color) * (x1 - x0)

    def text(self, x, y, string, color=(235, 235, 235), scale=1):
        cx = x
        for ch in string.upper():
            glyph = FONT.get(ch)
            if glyph is None:
                glyph = FONT["-"]
            for gy in range(FONT_HEIGHT):
                bits = glyph[gy]
                for gx in range(FONT_WIDTH):
                    if bits & (1 << (FONT_WIDTH - 1 - gx)):
                        self.fill_rect(cx + gx * scale, y + gy * scale,
                                       cx + (gx + 1) * scale,
                                       y + (gy + 1) * scale, color)
            cx += (FONT_WIDTH + 1) * scale

    def to_png(self, path):
        raw = bytearray()
        stride = self.width * 3
        for y in range(self.height):
            raw.append(0)
            raw += self.pixels[y * stride:(y + 1) * stride]

        def chunk(tag, data):
            return (struct.pack(">I", len(data)) + tag + data
                    + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

        png = b"\x89PNG\r\n\x1a\n"
        png += chunk(b"IHDR", struct.pack(">IIBBBBB", self.width, self.height,
                                          8, 2, 0, 0, 0))
        png += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
        png += chunk(b"IEND", b"")
        with open(path, "wb") as fp:
            fp.write(png)


def _rotate(point, yaw, pitch):
    x, y, z = point
    cy, sy = math.cos(yaw), math.sin(yaw)
    x, z = x * cy + z * sy, -x * sy + z * cy
    cp, sp = math.cos(pitch), math.sin(pitch)
    y, z = y * cp - z * sp, y * sp + z * cp
    return x, y, z


class Scene(object):
    """Triangle soup for one clip, already in world units (Y up)."""

    __slots__ = ("triangles", "bbox_min", "bbox_max")

    def __init__(self):
        self.triangles = []      # ([(x,y,z) x3], color)
        self.bbox_min = [1e30, 1e30, 1e30]
        self.bbox_max = [-1e30, -1e30, -1e30]


def build_scene(clip, up_axis="y"):
    """Flatten a clip into one triangle list per keyframe.

    Uses the same vertex convention as ``tgeo.build_mesh`` so animation
    previews line up with the model exports: ``pos = (byte + origin) * scale/8``
    with ``origin = frame offset / 32`` from ``svtx``.  ``up_axis='z'`` applies
    CrashEdit's viewer swap (X, Z, Y) instead.
    """
    mesh = tgeo.build_mesh(clip.tgeo, clip.frames)
    vertices = mesh["vertices"]
    triangles = mesh["triangles"]
    scenes = []
    for fi in range(len(clip.frames)):
        scene = Scene()
        for tri in triangles:
            pts = []
            color = [0.0, 0.0, 0.0]
            for index in tri["indices"]:
                vert = vertices[index]
                pos = vert["frame_positions"][fi]
                if up_axis == "z":
                    # CrashEdit's AnimationEntryViewer: (vert.X, vert.Z, vert.Y)
                    p = (pos[0], pos[2], pos[1])
                else:
                    p = (pos[0], pos[1], pos[2])
                pts.append(p)
                for c in range(3):
                    color[c] += vert["color"][c] / 3.0
            scene.triangles.append((pts, color))
            for p in pts:
                for a in range(3):
                    scene.bbox_min[a] = min(scene.bbox_min[a], p[a])
                    scene.bbox_max[a] = max(scene.bbox_max[a], p[a])
        scenes.append(scene)
    return scenes


def render_strip(clips, path, cols=6, cell=(112, 132), title=None,
                 yaw=math.radians(18.0), pitch=math.radians(8.0),
                 up_axis="y"):
    """Draw one row per clip, ``cols`` evenly sampled keyframes per row."""
    scenes = [build_scene(clip, up_axis) for clip in clips]

    # One shared scale so rows are comparable.
    lo = [1e30, 1e30, 1e30]
    hi = [-1e30, -1e30, -1e30]
    for row in scenes:
        for scene in row:
            for a in range(3):
                lo[a] = min(lo[a], scene.bbox_min[a])
                hi[a] = max(hi[a], scene.bbox_max[a])
    center = [(lo[a] + hi[a]) / 2.0 for a in range(3)]

    # Rotated extents (canvas) to compute the zoom.
    xmin = ymin = 1e30
    xmax = ymax = -1e30
    for a in (lo[0], hi[0]):
        for b in (lo[1], hi[1]):
            for c in (lo[2], hi[2]):
                x, y, _z = _rotate((a - center[0], b - center[1], c - center[2]),
                                   yaw, pitch)
                xmin = min(xmin, x)
                xmax = max(xmax, x)
                ymin = min(ymin, y)
                ymax = max(ymax, y)
    cw, ch = cell
    zoom = min((cw - 12) / max(1e-6, xmax - xmin),
               (ch - 12) / max(1e-6, ymax - ymin))

    margin = 6
    label_h = 13
    rows = len(clips)
    width = margin * 2 + cols * cw
    height = margin * 2 + rows * (label_h + ch)
    canvas = Canvas(width, height)
    canvas.fill_rect(0, 0, width, height, (12, 12, 16))

    light = (0.35, 0.62, 0.70)
    ln = math.sqrt(sum(c * c for c in light))
    light = tuple(c / ln for c in light)

    for ri, clip in enumerate(clips):
        row_scenes = scenes[ri]
        n = len(clip.frames)
        ticks = clip.group.frames if clip.group else n
        top = margin + ri * (label_h + ch)
        label = "%s  %s  %dKF/%dT  %.1fFPS%s" % (
            clip.name.upper(), clip.v_name, n, ticks,
            TICK_HZ * n / ticks, "  LOOP" if clip.group and clip.group.lerp else "")
        canvas.text(margin + 2, top + 3, label, (200, 210, 230))

        for ci in range(cols):
            if cols == 1:
                fi = 0
            else:
                fi = int(round(ci * (n - 1) / float(cols - 1)))
            fi = max(0, min(n - 1, fi))
            scene = row_scenes[fi]
            cx = margin + ci * cw + cw // 2
            cy = top + label_h + ch // 2
            # cell frame
            canvas.fill_rect(margin + ci * cw + 1, top + label_h + 1,
                             margin + (ci + 1) * cw - 1, top + label_h + ch - 1,
                             (24, 24, 32))
            zbuf = [-1e30] * (cw * ch)

            tris = []
            for pts, color in scene.triangles:
                sp = []
                depth = 0.0
                for p in pts:
                    rx, ry, rz = _rotate((p[0] - center[0], p[1] - center[1],
                                          p[2] - center[2]), yaw, pitch)
                    sp.append((cx + rx * zoom, cy - ry * zoom))
                    depth += rz / 3.0
                tris.append((depth, sp, color, pts))
            tris.sort(key=lambda t: t[0])

            for depth, sp, color, pts in tris:
                # flat normal in rotated space
                rx = []
                for p in pts:
                    rx.append(_rotate((p[0] - center[0], p[1] - center[1],
                                       p[2] - center[2]), yaw, pitch))
                ux = [rx[1][i] - rx[0][i] for i in range(3)]
                vx = [rx[2][i] - rx[0][i] for i in range(3)]
                nx = ux[1] * vx[2] - ux[2] * vx[1]
                ny = ux[2] * vx[0] - ux[0] * vx[2]
                nz = ux[0] * vx[1] - ux[1] * vx[0]
                nl = math.sqrt(nx * nx + ny * ny + nz * nz)
                if nl < 1e-9:
                    continue
                diff = abs((nx * light[0] + ny * light[1] + nz * light[2]) / nl)
                shade = 0.22 + 0.78 * diff
                rgb = tuple(
                    max(0, min(255, int(255.0 * (0.10 + 0.90 * color[k]) * shade)))
                    for k in range(3))
                _raster(canvas, zbuf, cw, ch,
                        margin + ci * cw, top + label_h, sp, depth, rgb)

    canvas.to_png(path)
    return path


def _raster(canvas, zbuf, cw, ch, ox, oy, tri, depth, color):
    (x0, y0), (x1, y1), (x2, y2) = tri
    minx = max(0, int(math.floor(min(x0, x1, x2))) - ox)
    maxx = min(cw - 1, int(math.ceil(max(x0, x1, x2))) - ox)
    miny = max(0, int(math.floor(min(y0, y1, y2))) - oy)
    maxy = min(ch - 1, int(math.ceil(max(y0, y1, y2))) - oy)
    if minx > maxx or miny > maxy:
        return
    area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
    if abs(area) < 1e-9:
        return
    inv = 1.0 / area
    for py in range(miny, maxy + 1):
        sy = oy + py + 0.5
        for px in range(minx, maxx + 1):
            sx = ox + px + 0.5
            w0 = ((x1 - x0) * (sy - y0) - (sx - x0) * (y1 - y0)) * inv
            w1 = ((sx - x0) * (y2 - y0) - (x2 - x0) * (sy - y0)) * inv
            w2 = 1.0 - w0 - w1
            if w0 < 0 or w1 < 0 or w2 < 0:
                continue
            z = depth
            idx = py * cw + px
            if z < zbuf[idx]:
                continue
            zbuf[idx] = z
            base = ((oy + py) * canvas.width + ox + px) * 3
            canvas.pixels[base:base + 3] = bytes(color)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def collect_clips(archive, key, level):
    """Resolve ``--model`` to a list of clips.

    An SVTX name gives one clip.  A GOOL name gives every distinct SVTX entry
    that GOOL's frame-group table references, in table order.
    """
    entry = archive.entry(key)
    if entry is not None and entry.type == 1:
        return [load_clip(archive, key, level)]
    clips = []
    seen = set()
    for group in iter_groups(archive):
        if group.gool != key:
            continue
        if group.v_name in seen:
            continue
        seen.add(group.v_name)
        if archive.entry(group.v_name) is None:
            continue
        clips.append(load_clip(archive, group.v_name, level))
    if not clips:
        raise SystemExit("no SVTX or GOOL entry named %r in level 0x%02X"
                         % (key, level))
    return clips


def list_clips(archive, level):
    """Print every clip referenced by a GOOL in this archive."""
    rows = []
    seen = set()
    for group in iter_groups(archive):
        if group.v_name in seen:
            continue
        if archive.entry(group.v_name) is None:
            continue
        seen.add(group.v_name)
        try:
            clip = load_clip(archive, group.v_name, level)
        except (KeyError, ValueError, IndexError):
            continue
        n = len(clip.frames)
        rows.append((clip.name, clip.v_name, n, group.frames,
                     TICK_HZ * n / group.frames, group.lerp,
                     clip.confidence, clip.group.gool if clip.group else "-"))
    print("level 0x%02X  %s" % (level, os.path.basename(archive.nsf_path)))
    print("%-12s %-7s %5s %6s %7s %5s %-6s %s"
          % ("name", "entry", "keys", "ticks", "fps", "lerp", "conf", "gool"))
    for name, entry, n, ticks, fps, lerp, conf, gool in rows:
        print("%-12s %-7s %5d %6d %7.2f %5d %-6s %s"
              % (name, entry, n, ticks, fps, lerp, conf, gool))
    print("%d clips" % len(rows))


def index_entry(clip):
    doc = clip_dict(clip, with_frames=False)
    return {
        "file": "%s.json" % clip.v_name,
        "entry": clip.v_name,
        "eid": doc["eid"],
        "model": doc["model"],
        "model_eid": doc["model_eid"],
        "level": doc["source"]["level"],
        "nsf": doc["source"]["nsf"],
        "name": clip.name,
        "name_confidence": clip.confidence,
        "keyframes": doc["timing"]["keyframes"],
        "ticks_total": doc["timing"]["ticks_total"],
        "ticks_per_frame": doc["timing"]["ticks_per_frame"]["value"],
        "fps": doc["timing"]["fps"],
        "loop": doc["loop"],
        "flags": doc["flags"],
        "gool": doc["gool"]["gool"] if doc["gool"] else None,
        "gool_group_offset_in_item5":
            doc["gool"]["group_offset_in_item5"] if doc["gool"] else None,
    }


def run_batch(args):
    """Write one JSON per ``LEVEL:ENTRY`` plus a merged ``index.json``."""
    if not args.out:
        raise SystemExit("--batch needs --out DIR")
    wanted = [p.strip() for p in args.batch.split(",") if p.strip()]
    if not wanted:
        raise SystemExit("--batch is empty")
    docs = []
    for item in wanted:
        if ":" not in item:
            raise SystemExit("bad --batch item %r (want LEVEL:ENTRY)" % item)
        level_text, entry = item.split(":", 1)
        level = int(level_text, 0)
        archive = cb3.load_level(level)
        clip = load_clip(archive, entry.strip(), level)
        docs.append((clip, model_dict([clip], clip.v_name)))

    os.makedirs(args.out, exist_ok=True)
    for clip, doc in docs:
        path = os.path.join(args.out, "%s.json" % clip.v_name)
        text = json.dumps(doc, indent=2, sort_keys=True) + "\n"
        with open(path, "w", newline="\n") as fp:
            fp.write(text)
        print("wrote %s (%d bytes)" % (path, len(text)), file=sys.stderr)

    index = {
        "format": FORMAT,
        "disc": "SCUS-94244",
        "tick_hz": TICK_HZ,
        "tick_hz_status": TICK_HZ_STATUS,
        "timing_rule": "ticks_per_frame = GOOL VertexGroup3.frames / SVTX keyframes",
        "models": [index_entry(clip) for clip, _doc in docs],
    }
    path = os.path.join(args.out, "index.json")
    text = json.dumps(index, indent=2, sort_keys=True) + "\n"
    with open(path, "w", newline="\n") as fp:
        fp.write(text)
    print("wrote %s (%d bytes)" % (path, len(text)), file=sys.stderr)
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Decode CB3 SVTX animation + GOOL timing.")
    parser.add_argument("--model", help="SVTX entry (one clip) or GOOL entry "
                                        "(all its clips)")
    parser.add_argument("--level", default="0x%02X" % DEFAULT_LEVEL,
                        help="level id, hex or decimal (default 0x02)")
    parser.add_argument("--json", action="store_true",
                        help="print the clip document as JSON")
    parser.add_argument("--png", help="render a frame strip to this path")
    parser.add_argument("--clips", help="comma separated names/entries for "
                                        "--png (default: idle,walk,run)")
    parser.add_argument("--cols", type=int, default=6,
                        help="frames per row in --png (default 6)")
    parser.add_argument("--cell", default="112x132", help="cell size for --png")
    parser.add_argument("--yaw", type=float, default=18.0,
                        help="view yaw in degrees for --png")
    parser.add_argument("--pitch", type=float, default=8.0,
                        help="view pitch in degrees for --png")
    parser.add_argument("--up-axis", choices=("y", "z"), default="y",
                        help="renderer up axis (default y, same as tgeo.build_mesh)")
    parser.add_argument("--out", help="write <name>.json and index.json here")
    parser.add_argument("--batch",
                        help="comma separated LEVEL:ENTRY pairs; writes one "
                             "JSON per entry plus a merged index.json to --out")
    parser.add_argument("--list", action="store_true",
                        help="list clips in the level archive")
    parser.add_argument("--census", action="store_true",
                        help="print the disc-wide frames/keyframes census "
                             "(evidence for the timing rule; slow)")
    parser.add_argument("--check", action="store_true",
                        help="validate SVTX frame headers for --model")
    args = parser.parse_args(argv)

    level = int(args.level, 0)

    if args.census:
        print(json.dumps(group_census(), indent=2, sort_keys=True))
        return 0

    if args.batch:
        return run_batch(args)

    archive = cb3.load_level(level)

    if args.list:
        list_clips(archive, level)
        if not (args.model or args.json or args.png):
            return 0

    if not args.model:
        parser.error("--model is required unless --list/--census is used")

    keys = [k.strip() for k in args.model.split(",") if k.strip()]
    clips = []
    seen = set()
    for key in keys:
        for clip in collect_clips(archive, key, level):
            if clip.v_name in seen:
                continue
            seen.add(clip.v_name)
            clips.append(clip)
    if args.out and len(keys) != 1:
        parser.error("--out needs exactly one --model entry")

    if args.check:
        for clip in clips:
            rows = validate_frame_header(clip.v_entry, clip.frames)
            ok = all(r["header_size"] == r["computed_header_size"] for r in rows)
            print("%s: %d frames, header accounting %s"
                  % (clip.v_name, len(rows), "OK" if ok else "MISMATCH"))
            for row in rows[:3]:
                print("  frame %2d headerSize=0x%X computed=0x%X hitboxes=%d "
                      "specials=%s verts=%d itemBytes=%d payloadBits=%s"
                      % (row["index"], row["header_size"],
                         row["computed_header_size"], row["hitbox_count"],
                         row["special_vertices"], row["vertex_count"],
                         row["item_bytes"], row["payload_bits"]))

    if args.json or not args.png:
        doc = model_dict(clips, args.model)
        text = json.dumps(doc, indent=2, sort_keys=True) + "\n"
        if args.out:
            os.makedirs(args.out, exist_ok=True)
            path = os.path.join(args.out, "%s.json" % args.model)
            with open(path, "w", newline="\n") as fp:
                fp.write(text)
            print("wrote %s (%d bytes)" % (path, len(text)), file=sys.stderr)
        if args.json or not args.png:
            sys.stdout.write(text)

    if args.png:
        selected = clips
        if args.clips:
            wanted = [c.strip() for c in args.clips.split(",") if c.strip()]
            selected = []
            for want in wanted:
                for clip in clips:
                    if want.lower() in (clip.name.lower(), clip.v_name.lower()):
                        selected.append(clip)
                        break
                else:
                    raise SystemExit("no clip named %r (have: %s)"
                                     % (want, ", ".join(c.name for c in clips)))
        elif len(clips) > 3:
            by_name = {c.name: c for c in clips}
            selected = [by_name[n] for n in DEFAULT_STRIP if n in by_name]
            if not selected:
                selected = clips[:3]
        cw, ch = (int(v) for v in args.cell.lower().split("x"))
        render_strip(selected, args.png, cols=args.cols, cell=(cw, ch),
                     yaw=math.radians(args.yaw), pitch=math.radians(args.pitch),
                     up_axis=args.up_axis)
        print("wrote %s (%d rows x %d cols)" % (args.png, len(selected), args.cols),
              file=sys.stderr)

    if args.out:
        index = {
            "format": FORMAT,
            "disc": "SCUS-94244",
            "tick_hz": TICK_HZ,
            "tick_hz_status": TICK_HZ_STATUS,
            "models": [],
        }
        path = os.path.join(args.out, "index.json")
        doc = model_dict(clips, args.model)
        for clip in doc["clips"]:
            index["models"].append({
                "entry": clip["entry"],
                "model": clip["model"],
                "name": clip["name"],
                "name_confidence": clip["name_confidence"],
                "keyframes": clip["timing"]["keyframes"],
                "ticks_total": clip["timing"]["ticks_total"],
                "fps": clip["timing"]["fps"],
                "loop": clip["loop"],
                "flags": clip["flags"],
                "gool": clip["gool"]["gool"] if clip["gool"] else None,
            })
        text = json.dumps(index, indent=2, sort_keys=True) + "\n"
        with open(path, "w", newline="\n") as fp:
            fp.write(text)
        print("wrote %s (%d bytes)" % (path, len(text)), file=sys.stderr)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
