"""
NSF / NSD parser for Naughty Dog's PSX Crash Bandicoot data format.

Format knowledge synthesised from:
  * wurlyfox/crashutils  (include/crash/{nsf,nsd,chunk,entry}.h, lib/crash/*.cpp)
  * cbhacks/drnsf        (src/nsf_archive.cc, src/nsf_wgeo_v2.cc, src/nsf_tpage.cc)
  * cbhacks/wiki + retro-reversing notes

NSF archive layout
------------------
A sequence of 64 KiB pages.  Each page starts with a u16 magic:
  0x1234 -> uncompressed page
  0x1235 -> compressed page (12 byte header: magic u16, pad u16,
            length u32, skip u32, then the LZ stream)

Page header (after decompression):
  0x00 magic u16 (0x1234)
  0x02 type  u16 (0 = entry chunk, 1 = texture page)
  0x04 CID   u32   (chunk id, chunk id * 2 = page index)
  0x08 entryCount u32
  0x0C checksum u32
  0x10 entry offsets u32[entryCount]  (relative to page start)

Entry header (inside a type 0 chunk):
  0x00 magic u32 (0x100FFFF)
  0x04 EID   u32
  0x08 type  u32
  0x0C itemCount u32
  0x10 item offsets u32[itemCount+1]  (relative to entry start)

NSD layout
----------
  0x000 256 x u32 list indices (relative offsets into the list-entry table)
  0x404 u32 number of list entries
  0x408 u32 EID of the LDAT entry (level header)
  0x520 list entries: numListEntries x (u32 CID, u32 EID)
  then level header: magic, LID, firstZone, firstSect, unknown
  then 64 x u32 level EIDs
"""

import struct
import os

CHUNK_SIZE = 0x10000
MAGIC_PAGE = 0x1234
MAGIC_COMPRESSED = 0x1235

CHARSET = ("0123456789"
            "abcdefghijklmnopqrstuvwxyz"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "_!")


def eid_str(value):
    """Decode a 5-character Naughty Dog entry id."""
    if value & 0x80000000:
        out = "="
    else:
        out = ""
    for shift in (25, 19, 13, 7, 1):
        out += CHARSET[(value >> shift) & 0x3F]
    if not (value & 1):
        out += "="
    return out


def eid_val(text):
    """Encode a 5-character entry id."""
    value = 0
    for ch in text[:5]:
        value = (value << 6) | (CHARSET.index(ch) & 0x3F)
    return (value << 1) | 1


def decompress_page(src, offset):
    """Decompress one compressed NSF page.  Returns (data, consumed_bytes)."""
    magic, _pad, length, skip = struct.unpack_from("<HHII", src, offset)
    assert magic == MAGIC_COMPRESSED, "not a compressed page: %04X" % magic
    pos = offset + 12
    out = bytearray()
    while len(out) < length:
        fmt = src[pos]
        if fmt & 0x80:
            # back reference: 12 bit distance, 3 bit span
            data = src[pos + 1]
            seek = ((fmt & 0x7F) << 5) | (data >> 3)
            span = data & 7
            span = 0x40 if span == 7 else span + 3
            pos += 2
            start = len(out) - seek
            for i in range(span):
                out.append(out[start + i])
        else:
            count = fmt & 0x7F
            out += src[pos + 1: pos + 1 + count]
            pos += 1 + count
    pos += skip
    remainder = CHUNK_SIZE - length
    out += src[pos: pos + remainder]
    pos += remainder
    return bytes(out), pos - offset


class Entry:
    __slots__ = ("eid", "type", "items", "raw", "offset")

    def __init__(self, eid, etype, items, raw, offset):
        self.eid = eid
        self.type = etype
        self.items = items
        self.raw = raw
        self.offset = offset

    @property
    def name(self):
        return eid_str(self.eid)

    def __repr__(self):
        return "<Entry %s type=%d items=%d>" % (self.name, self.type, len(self.items))


class Chunk:
    __slots__ = ("ctype", "cid", "entries", "data", "index")

    def __init__(self, ctype, cid, entries, data, index):
        self.ctype = ctype
        self.cid = cid
        self.entries = entries
        self.data = data
        self.index = index

    def __repr__(self):
        return "<Chunk idx=%d cid=%04X type=%d entries=%d>" % (
            self.index, self.cid, self.ctype, len(self.entries))


def parse_entries(page):
    """Parse the entry table of a type 0 chunk page."""
    magic, ctype, cid, entry_count, checksum = struct.unpack_from("<HHIII", page, 0)
    entries = []
    if ctype != 0:
        return ctype, cid, entries, checksum
    offsets = struct.unpack_from("<%dI" % entry_count, page, 0x10)
    for i in range(entry_count):
        off = offsets[i]
        emagic, eid, etype, item_count = struct.unpack_from("<IIII", page, off)
        assert emagic == 0x100FFFF, "bad entry magic %08X" % emagic
        ioffs = struct.unpack_from("<%dI" % (item_count + 1), page, off + 0x10)
        items = []
        for k in range(item_count):
            items.append(page[off + ioffs[k]: off + ioffs[k + 1]])
        entries.append(Entry(eid, etype, items, page[off:], off))
    return ctype, cid, entries, checksum


def parse_nsf(path):
    """Parse an NSF archive into a list of chunks."""
    with open(path, "rb") as source_file:
        src = source_file.read()
    chunks = []
    offset = 0
    index = 0
    while offset < len(src):
        magic = struct.unpack_from("<H", src, offset)[0]
        if magic == MAGIC_PAGE:
            page = src[offset: offset + CHUNK_SIZE]
            offset += CHUNK_SIZE
        elif magic == MAGIC_COMPRESSED:
            page, used = decompress_page(src, offset)
            offset += used
        else:
            raise ValueError("bad page magic %04X at %08X" % (magic, offset))
        ctype, cid, entries, _ck = parse_entries(page)
        chunks.append(Chunk(ctype, cid, entries, page, index))
        index += 1
    return chunks


class NSD:
    def __init__(self, path):
        with open(path, "rb") as source_file:
            self.buf = source_file.read()
        self.num_list_entries = struct.unpack_from("<I", self.buf, 0x404)[0]
        self.ldat_eid = struct.unpack_from("<I", self.buf, 0x408)[0]
        self.list_indices = struct.unpack_from("<256I", self.buf, 0x000)
        base = 0x520
        self.list_entries = []
        for i in range(self.num_list_entries):
            cid, eid = struct.unpack_from("<II", self.buf, base + i * 8)
            self.list_entries.append((cid, eid))
        pos = base + self.num_list_entries * 8
        (self.magic, self.lid, self.first_zone, self.first_section,
         self.unknown) = struct.unpack_from("<IIIII", self.buf, pos)
        self.level_eids = list(struct.unpack_from("<64I", self.buf, pos + 0x14))

    def cid_of(self, eid):
        for cid, e in self.list_entries:
            if e == eid:
                return cid
        return None

    def eid_of(self, cid):
        for c, e in self.list_entries:
            if c == cid:
                return e
        return None

    def summary(self):
        lines = ["NSD: %d list entries, ldat=%s, level magic=%08X lid=%08X firstZone=%s firstSect=%s"
                 % (self.num_list_entries, eid_str(self.ldat_eid), self.magic,
                    self.lid, eid_str(self.first_zone), eid_str(self.first_section))]
        for i, eid in enumerate(self.level_eids):
            if eid:
                lines.append("  level eid[%02d] = %08X  %s" % (i, eid, eid_str(eid)))
        return "\n".join(lines)


if __name__ == "__main__":
    import sys
    nsf_path = sys.argv[1]
    chunks = parse_nsf(nsf_path)
    print("%s: %d chunks" % (os.path.basename(nsf_path), len(chunks)))
    types = {}
    for c in chunks:
        for e in c.entries:
            types[e.type] = types.get(e.type, 0) + 1
    print("entry types:", dict(sorted(types.items())))
    for c in chunks[:4]:
        print(c)
        for e in c.entries[:20]:
            print("   ", e)
