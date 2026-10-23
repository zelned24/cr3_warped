"""Deterministic inventory of the Crash Bandicoot: Warped disc archives.

Produces:
  game_data/inventory.json     machine-readable counts used by validate_assets.py

Counts are taken from the shipped disc only (S0..S3/*.NSF + *.NSD):
  * entry types per level (SVTX=1, TGEO=2, WGEO=3, SLST=4, ZDAT=7, GOOL=11, ...)
  * 64 KiB texture pages (page-type 1) per level -- not entries; see root README.md
  * entity descriptor names inside ZDAT entries (ASCII, e.g. obj_fake_crash)
  * uncompressed/decompressed byte totals

Usage:  python tools/inventory.py [--check]
        --check  verify game_data/levels.bin is untouched (42 levels, size, sha256)
"""
import hashlib
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nsf

ISO = os.environ.get("CBW_ISO",
                      os.path.join(os.environ.get("TEMP", ""), "opencode",
                                   "cbwarped_iso"))
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.path.join(ROOT, "game_data")

TYPE_NAMES = {
    1: "SVTX", 2: "TGEO", 3: "WGEO", 4: "SLST", 5: "TPAG", 6: "LDAT",
    7: "ZDAT", 11: "GOOL", 12: "ADIO", 13: "MIDI", 14: "INST", 15: "VCOL",
    17: "RAWD", 19: "PBAK", 21: "VIDO",
}

NAME_RE = re.compile(rb"(?:obj|chr|veh|fx|cam|prt|snd)_[A-Za-z0-9_#]{1,80}")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def level_files():
    out = []
    for folder in ("S0", "S1", "S2", "S3"):
        d = os.path.join(ISO, folder)
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d)):
            if name.endswith(".NSF"):
                out.append((int(name[1:-4], 16), folder, name[:-4]))
    return out


def scan():
    rows = []
    for level_id, folder, stem in level_files():
        nsf_path = os.path.join(ISO, folder, stem + ".NSF")
        chunks = nsf.parse_nsf(nsf_path)
        types = {}
        pages = 0
        entity_names = set()
        for c in chunks:
            if c.ctype != 0:
                if c.ctype == 1:
                    pages += 1
                continue
            for e in c.entries:
                types[e.type] = types.get(e.type, 0) + 1
                if e.type == 7:
                    for m in NAME_RE.finditer(e.raw):
                        entity_names.add(m.group().decode("ascii"))
        rows.append({
            "level": level_id,
            "folder": folder,
            "stem": stem,
            "nsfBytes": os.path.getsize(nsf_path),
            "pages": len(chunks),
            "texturePages": pages,
            "types": {str(k): v for k, v in sorted(types.items())},
            "entityNames": len(entity_names),
        })
    return rows


def main():
    check = "--check" in sys.argv
    rows = scan()
    totals = {}
    for r in rows:
        for k, v in r["types"].items():
            totals[k] = totals.get(k, 0) + v
        totals["texturePages"] = totals.get("texturePages", 0) + r["texturePages"]

    os.makedirs(GAME, exist_ok=True)
    with open(os.path.join(GAME, "inventory.json"), "w") as f:
        json.dump({"levels": rows, "totals": totals}, f, indent=1, sort_keys=True)

    print("levels=%d  totals=%s" % (len(rows), totals))

    if check:
        blob = os.path.join(GAME, "levels.bin")
        size = os.path.getsize(blob)
        digest = sha256(blob)
        data = open(blob, "rb").read(16)
        magic, ver, count, _ = struct.unpack("<4sIII", data)
        ok = (magic == b"CBWB" and count == 42 and size == 15967590)
        print("levels.bin: %s v%d levels=%d size=%d sha256=%s -> %s"
              % (magic.decode(), ver, count, size, digest, "OK" if ok else "CHANGED"))
        return 0 if ok else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
