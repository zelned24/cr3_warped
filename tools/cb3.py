"""Shared Crash Bandicoot: Warped (PSX) archive helpers for the asset pipeline.

The format knowledge comes from the public Crash Warped viewer:
  noclip.website, src/CrashWarped/bin.ts (MIT)
kept as a reference in tools/reference/crashwarped_bin.ts, and is verified
against the retail USA disc (SCUS-94244) by the test suite in this directory.

This module only does container-level work: load an NSF/NSD pair, expose its
entries by name/type and expose the raw 64 KiB texture pages (chunk type 1).
Entry decoding lives in svtx.py / tgeo.py / tpag.py / wgeo.py / zdat.py.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nsf

ISO = r"C:\Users\operacion02\AppData\Local\Temp\opencode\cbwarped_iso"
LEVEL_DIRS = ("S0", "S1", "S2", "S3")


def decode_chunk_name(h):
    """Decode the 5-character name of a chunk/EID (HD style base64)."""
    h >>= 1
    out = ""
    for _ in range(5):
        out = nsf.CHARSET[h & 0x3F] + out
        h >>= 6
    return out


class TexturePage:
    """One 64 KiB raw PSX VRAM page (chunk type 1)."""

    __slots__ = ("name", "cid", "data", "index")

    def __init__(self, name, cid, data, index):
        self.name = name
        self.cid = cid
        self.data = data          # exactly CHUNK_SIZE bytes, header included
        self.index = index

    def __repr__(self):
        return "<TexturePage %s cid=%08X>" % (self.name, self.cid)


class Archive:
    """A parsed level archive (NSF) plus its NSD metadata when available."""

    def __init__(self, nsf_path, nsd_path=None, level_id=None):
        self.nsf_path = nsf_path
        self.nsd_path = nsd_path
        self.level_id = level_id
        self.chunks = nsf.parse_nsf(nsf_path)
        self.entries = []
        self.by_name = {}
        self.by_type = {}
        self.texture_pages = []
        for chunk in self.chunks:
            if chunk.ctype == 1:
                page = TexturePage(decode_chunk_name(chunk.cid), chunk.cid,
                                   chunk.data, len(self.texture_pages))
                self.texture_pages.append(page)
            for entry in chunk.entries:
                self.entries.append(entry)
                self.by_name.setdefault(entry.name, entry)
                self.by_type.setdefault(entry.type, []).append(entry)
        self.texture_by_name = {p.name: p for p in self.texture_pages}
        self.nsd = None
        if nsd_path and os.path.exists(nsd_path):
            self.nsd = nsf.NSD(nsd_path)

    # -- lookups ---------------------------------------------------------
    def entry(self, name):
        return self.by_name.get(name)

    def of_type(self, etype):
        return self.by_type.get(etype, [])

    def texture(self, name):
        return self.texture_by_name.get(name)

    # -- convenience -----------------------------------------------------
    def counts(self):
        out = {}
        for e in self.entries:
            out[e.type] = out.get(e.type, 0) + 1
        return out

    def summary(self):
        lines = ["%s: %d chunks, %d entries, %d texture pages"
                 % (os.path.basename(self.nsf_path), len(self.chunks),
                    len(self.entries), len(self.texture_pages))]
        for t in sorted(self.counts()):
            lines.append("  type %2d: %d" % (t, self.counts()[t]))
        return "\n".join(lines)


_ARCHIVE_CACHE = {}


def level_paths(level_id):
    """Return (nsf, nsd) paths for a level id (0x02..0x3C)."""
    for folder in LEVEL_DIRS:
        stem = "S%07X" % level_id
        nsf_path = os.path.join(ISO, folder, stem + ".NSF")
        if os.path.exists(nsf_path):
            return nsf_path, os.path.join(ISO, folder, stem + ".NSD")
    raise FileNotFoundError("level 0x%02X not found under %s" % (level_id, ISO))


def load_level(level_id, cache=True):
    """Load (and memoise) one level archive by id.  0x0B = Toad Village."""
    if cache and level_id in _ARCHIVE_CACHE:
        return _ARCHIVE_CACHE[level_id]
    nsf_path, nsd_path = level_paths(level_id)
    arc = Archive(nsf_path, nsd_path, level_id)
    if cache:
        _ARCHIVE_CACHE[level_id] = arc
    return arc


def all_level_ids():
    ids = []
    for folder in LEVEL_DIRS:
        fdir = os.path.join(ISO, folder)
        if not os.path.isdir(fdir):
            continue
        for name in os.listdir(fdir):
            if name.endswith(".NSF"):
                ids.append(int(name[1:-4], 16))
    return sorted(ids)


if __name__ == "__main__":
    if len(sys.argv) > 1:
        arc = load_level(int(sys.argv[1], 0))
    else:
        arc = load_level(0x0B)
    print(arc.summary())
    print("texture pages:", arc.texture_pages)
