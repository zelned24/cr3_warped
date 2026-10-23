"""Export Crash Bandicoot: Warped (PSX) levels into the compact binary format
consumed by the 3DS port.

Output: <out>/levels.bin, a CBWB directory followed by CBW3 level records.
  magic 'CBW3', level id, world model count, palette size, metadata offset
  per world model:
     i32 origin x,y,z        (runtime origin = raw / 16)
     u32 vcount, tcount, qcount
     vertices : i16 x, i16 y, i16 z, u16 colour index
     triangles: u16 a, u16 b, u16 c, u16 flags
     quads    : u16 a, u16 b, u16 c, u16 d, u16 flags
  LMD1 metadata: NSD entry checkpoint and per-model sky flags
  palette: u8 r, u8 g, u8 b, u8 pad
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nsf
import rawd
import wgeo

ISO = r"C:\Users\operacion02\AppData\Local\Temp\opencode\cbwarped_iso"
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "game_data")

# The disc stores each level in S0..S3 as S<8 hex digits>.NSF / .NSD.
LEVELS = []
for folder in ("S0", "S1", "S2", "S3"):
    fdir = os.path.join(ISO, folder)
    if not os.path.isdir(fdir):
        continue
    for name in sorted(os.listdir(fdir)):
        if name.endswith(".NSF"):
            level_id = int(name[1:-4], 16)
            LEVELS.append((level_id, folder, name[:-4]))


def build_level(level_id, folder, stem):
    nsf_path = os.path.join(ISO, folder, stem + ".NSF")
    nsd_path = os.path.join(ISO, folder, stem + ".NSD")
    if not os.path.exists(nsf_path):
        return None
    chunks = nsf.parse_nsf(nsf_path)
    nsd = nsf.NSD(nsd_path) if os.path.exists(nsd_path) else None

    entries = {}
    for c in chunks:
        for e in c.entries:
            entries.setdefault(e.name, e)

    # world models are the type 3 (WGEO) entries
    models = []
    from_rawd = False
    for c in chunks:
        for e in c.entries:
            if e.type == 3:
                try:
                    models.append((e.name, wgeo.Wgeo(e)))
                except Exception as exc:            # pragma: no cover
                    print("  ! %s: %s" % (e.name, exc))
    if not models:
        # The vehicle / boss levels keep no WGEO at all: their scenery is
        # stored as RAWD (type 17) 2D layers.  Flatten those layers onto the
        # XZ plane so the level still has renderable, walkable geometry.
        rawd_model, rawds = rawd.build_model(chunks)
        if rawd_model is None:
            return None
        models.append((rawds[0].name if rawds else "RAWD", rawd_model))
        from_rawd = True

    # merge the colour palettes of every model into one table
    palette = []
    palette_index = {}
    for _, m in models:
        for (r, g, b) in m.colors:
            key = (r, g, b)
            if key not in palette_index:
                palette_index[key] = len(palette)
                palette.append(key)

    # The stored vertex colours are the raw (unlit) palette entries; the game
    # applies its light matrix on top of them.  Lift them a little so the
    # 3DS port reads well without per-pixel lighting.  RAWD levels already
    # carry the average texture colour, so they are left at 1:1.
    LIT = 1.0 if from_rawd else 2.0
    palette_lit = [tuple(min(255, int(c * LIT)) for c in col) for col in palette]

    out = bytearray()
    out += b"CBW3"
    out += struct.pack("<IIII", level_id, len(models), len(palette), 0)
    for name, m in models:
        out += struct.pack("<iii", m.world_x, m.world_y, m.world_z)
        out += struct.pack("<III", m.vertex_count, m.triangle_count, m.quad_count)
        for (x, y, z, cidx, fx) in m.vertices:
            out += struct.pack("<hhhH", x, y, z, palette_index[m.colors[cidx]]
                               if cidx < len(m.colors) else 0)
        for (a, b, c, unk0, unk1) in m.triangles:
            out += struct.pack("<HHHH", a, b, c, (unk0 & 1) | ((unk1 & 1) << 1))
        for (a, b, c, d, unk0, unk1) in m.quads:
            out += struct.pack("<HHHHH", a, b, c, d, (unk0 & 1) | ((unk1 & 1) << 1))

    # Preserve the disc's entry point and sky classification. Geometry order
    # inside an NSF is storage order, not the player's starting location.
    # +16 was reserved in CBW3 v1; zero still means an old metadata-free level.
    metadata_offset = len(out)
    struct.pack_into("<I", out, 16, metadata_offset)
    checkpoint = None
    if nsd is not None and not from_rawd:
        table = 0x520 + nsd.num_list_entries * 8
        if table + 4 <= len(nsd.buf):
            count = struct.unpack_from("<I", nsd.buf, table)[0]
            first = table + 0x2DC
            if count and first + count * 24 <= len(nsd.buf):
                checkpoint = struct.unpack_from("<IIIiii", nsd.buf, first)
                # The first word must identify a ZDAT in this archive.
                if not any(e.type == 7 and e.eid == checkpoint[0]
                           for e in entries.values()):
                    checkpoint = None
    out += b"LMD1"
    out += struct.pack("<I", 1 if checkpoint else 0)
    out += struct.pack("<iiiIII", *(checkpoint[3:] + checkpoint[:3]
                                   if checkpoint else (0,) * 6))
    out += struct.pack("<I", len(models))
    for _, model in models:
        out += struct.pack("<I", 1 if getattr(model, "is_skybox", False) else 0)
    for (r, g, b) in palette_lit:
        out += struct.pack("<4B", r, g, b, 0)
    return bytes(out), [n for n, _ in models], nsd


def main():
    os.makedirs(OUT, exist_ok=True)
    limit = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    index = []
    blobs = []
    for level_id, folder, stem in LEVELS:
        if limit and len(blobs) >= limit:
            break
        res = build_level(level_id, folder, stem)
        if res is None:
            print("skip %s (no world-geometry entries)" % stem)
            continue
        data, models, nsd = res
        blobs.append((level_id, data))
        index.append((level_id, folder, stem, len(data), models))
        print("level 0x%02X %s/%s  %8d bytes  models=%d"
              % (level_id, folder, stem, len(data), len(models)))

    # single embedded blob: 'CBWB' + index + level data
    out = bytearray()
    out += b"CBWB"
    out += struct.pack("<III", 1, len(blobs), 0)
    offset = 16 + len(blobs) * 12
    for level_id, data in blobs:
        out += struct.pack("<III", level_id, offset, len(data))
        offset += len(data)
    for _, data in blobs:
        out += data
    with open(os.path.join(OUT, "levels.bin"), "wb") as f:
        f.write(out)
    print("wrote game_data/levels.bin  (%d levels, %d bytes)"
          % (len(blobs), len(out)))

    hdr = os.path.join(os.path.dirname(OUT), "source", "levels_bin.h")
    with open(hdr, "w") as f:
        f.write("/* Generated by tools/export_levels.py - do not edit. */\n")
        f.write("#ifndef LEVELS_BIN_H\n#define LEVELS_BIN_H\n")
        f.write("#include <stddef.h>\n#include <stdint.h>\n\n")
        f.write("extern const uint8_t levels_bin[];\n")
        f.write("extern const uint8_t levels_bin_end[];\n\n")
        f.write("static const size_t levels_bin_size = %d;\n" % len(out))
        f.write("\n#endif\n")
    print("wrote", hdr)

    with open(os.path.join(OUT, "index.txt"), "w") as f:
        for level_id, folder, stem, size, models in index:
            f.write("%02X %s %s %d %s\n" % (level_id, folder, stem, size,
                                           ",".join(models)))


if __name__ == "__main__":
    main()
