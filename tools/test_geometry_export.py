"""Compare exported world coordinates and topology against raw retail WGEO.

This deliberately decodes the packed words independently of wgeo.Wgeo and
checks the bytes consumed by the console, including NSD checkpoint metadata.
"""
import struct
import sys
from pathlib import Path

import cb3

ROOT = Path(__file__).resolve().parents[1]


def raw_geometry(entry):
    header, vertices, triangles, quads = entry.items[:4]
    origin = struct.unpack_from("<iii", header)
    vc, tc, qc = struct.unpack_from("<III", header, 16)
    coords = []
    for i in range(vc):
        x, y = struct.unpack_from("<HH", vertices, 4 * (vc - i - 1))
        z = struct.unpack_from("<H", vertices, 4 * vc + 2 * i)[0]
        coords.append((x >> 4, y >> 4, z >> 4))
    tris = []
    for i in range(tc):
        upper = struct.unpack_from("<I", triangles, 4 * (tc - i - 1))[0]
        lower = struct.unpack_from("<H", triangles, 4 * tc + 2 * i)[0]
        tris.append((upper >> 20, (upper >> 8) & 0xFFF, lower >> 4))
    faces = []
    for i in range(qc):
        upper, lower = struct.unpack_from("<II", quads, 8 * i)
        faces.append((upper >> 20, (upper >> 8) & 0xFFF,
                      lower >> 20, (lower >> 8) & 0xFFF))
    return origin, coords, tris, faces


def main():
    blob = (ROOT / "game_data/levels.bin").read_bytes()
    count = struct.unpack_from("<I", blob, 8)[0]
    tested = 0
    for index in range(count):
        lid, offset, size = struct.unpack_from("<III", blob, 16 + index * 12)
        data = blob[offset:offset + size]
        arc = cb3.load_level(lid)
        entries = arc.of_type(3)
        if not entries:
            continue
        assert len(entries) == struct.unpack_from("<I", data, 8)[0]
        metadata = struct.unpack_from("<I", data, 16)[0]
        assert metadata and data[metadata:metadata + 4] == b"LMD1"
        cursor = 20
        for model_index, entry in enumerate(entries):
            origin, coords, triangles, quads = raw_geometry(entry)
            actual_origin = struct.unpack_from("<iii", data, cursor)
            vc, tc, qc = struct.unpack_from("<III", data, cursor + 12)
            assert actual_origin == origin, (lid, entry.name, "origin")
            assert (vc, tc, qc) == (len(coords), len(triangles), len(quads))
            cursor += 24
            for xyz in coords:
                actual = struct.unpack_from("<hhh", data, cursor)
                # Runtime applies origin/16 + local; no axis swap or extra scale.
                assert tuple(actual[i] + actual_origin[i] / 16 for i in range(3)) == tuple(
                    xyz[i] + origin[i] / 16 for i in range(3)), (lid, entry.name, "vertex")
                cursor += 8
            for face in triangles:
                assert struct.unpack_from("<3H", data, cursor) == face, (lid, entry.name, "triangle")
                cursor += 8
            for face in quads:
                assert struct.unpack_from("<4H", data, cursor) == face, (lid, entry.name, "quad")
                cursor += 10
            sky = struct.unpack_from("<I", data, metadata + 36 + model_index * 4)[0]
            assert bool(sky) == bool(struct.unpack_from("<I", entry.items[0], 12)[0])
            tested += 1
        assert cursor == metadata
        if lid == 11:
            assert struct.unpack_from("<I", data, metadata + 4)[0] == 1
            position = struct.unpack_from("<iii", data, metadata + 8)
            assert position == (12427939, 819200, 104243200), position
            print("Toad Village NSD entry:", tuple(v / 4096 for v in position))
    print("PASS: %d retail WGEO models match exported origin, axes, vertices, topology and sky flags" % tested)


if __name__ == "__main__":
    main()
