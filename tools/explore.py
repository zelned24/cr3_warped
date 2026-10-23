"""Exploration helper: dump entries / items of a Crash PSX level."""
import struct
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nsf

ISO = r"C:\Users\operacion02\AppData\Local\Temp\opencode\cbwarped_iso"


def words(b, n=16):
    return [struct.unpack_from("<I", b, i * 4)[0] for i in range(min(n, len(b) // 4))]


def dump_entry(entry, item_words=12):
    print("== %s (type %d) items=%d" % (entry.name, entry.type, len(entry.items)))
    for i, it in enumerate(entry.items):
        print("  item %2d len=%6d  %s" % (i, len(it),
              " ".join("%08X" % w for w in words(it, item_words))))


def main():
    lvl = sys.argv[1] if len(sys.argv) > 1 else "S0/S0000002"
    want = sys.argv[2] if len(sys.argv) > 2 else None
    chunks = nsf.parse_nsf(os.path.join(ISO, lvl + ".NSF"))
    for c in chunks:
        for e in c.entries:
            if want is None or want in e.name or want == str(e.type):
                print("--- chunk cid=%04X" % c.cid)
                dump_entry(e)


if __name__ == "__main__":
    main()
