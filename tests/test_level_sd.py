"""Exercise the production SD reader against malformed blobs and real assets.

Run: python tests/test_level_sd.py (requires the ziglang Python package).
All fixtures and executable are temporary; no production save or asset changes.
"""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def level(level_id):
    return (b"CBW3" + struct.pack("<IIII", level_id, 1, 1, 76)
            + struct.pack("<iiiIII", -16, 0, 32, 3, 1, 0)
            + struct.pack("<12h", 0, 0, 0, 0, 16, 0, 0, 0, 0, 0, 16, 0)
            + struct.pack("<4H", 0, 1, 2, 0)
            + b"LMD1" + struct.pack("<IiiiIIIII", 1, -4096, 0, 8192, 0, 0, 0, 1, 0)
            + bytes((255, 127, 0, 0)))


def fixture():
    first, second = level(11), level(12)
    return (b"CBWB" + struct.pack("<III", 1, 2, 0)
            + struct.pack("<III", 11, 40, len(first))
            + struct.pack("<III", 12, 40 + len(first), len(second))
            + first + second)


def replace_u32(data, offset, value):
    result = bytearray(data)
    struct.pack_into("<I", result, offset, value)
    return result


def overlapping_fixture():
    # Both CBW3 records are structurally valid with distinct IDs. The second
    # lives inside the first model's vertex bytes, so only range checks catch it.
    inner = level(12)
    outer = bytearray(b"CBW3" + struct.pack("<IIII", 11, 1, 1, 0)
                      + struct.pack("<iiiIII", 0, 0, 0, 32, 1, 0)
                      + bytes(256 + 8) + bytes((255, 127, 0, 0)))
    outer[44:44 + len(inner)] = inner
    return (b"CBWB" + struct.pack("<III", 1, 2, 0)
            + struct.pack("<III", 11, 40, len(outer))
            + struct.pack("<III", 12, 84, len(inner)) + outer)


HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "level_sd.h"
int main(int argc, char **argv) {
    assert(argc >= 4);
    assert(level_sd_available() == 0);
    assert(level_sd_open(argv[1]) == 0);
    assert(level_sd_count() == 2);
    const uint8_t *previous = level_sd_blob();
    uint32_t old_size = level_sd_blob_size();
    for (int i = 3; i < argc; i++) {
        assert(level_sd_open(argv[i]) == -1);
        assert(level_sd_blob() == previous);
        assert(level_sd_blob_size() == old_size);
        assert(level_sd_count() == 2 && level_sd_available());
        assert(memcmp(previous, "CBWB", 4) == 0);
    }
    assert(level_sd_open(NULL) == -1);
    assert(level_sd_blob() == previous);
    assert(level_sd_open(argv[2]) == 0);
    assert(level_sd_count() == 42);
    level_sd_close(); level_sd_close();
    assert(level_sd_blob() == NULL && level_sd_blob_size() == 0);
    assert(level_sd_count() == 0 && !level_sd_available());
    puts("PASS: SD reader rejection/preservation, replacement, real 42-level blob, cleanup");
    return 0;
}
'''


def main():
    good = fixture()
    cases = {
        "version": replace_u32(good, 4, 2),
        "empty_directory": replace_u32(good, 8, 0),
        "huge_directory": replace_u32(good, 8, 0xFFFFFFFF),
        "id_outside_table": replace_u32(good, 16, 64),
        "duplicate_id": replace_u32(replace_u32(good, 28, 11), 40 + len(level(11)) + 4, 11),
        "directory_overlap": replace_u32(good, 20, 16),
        "level_overlap": overlapping_fixture(),
        "extent_overflow": replace_u32(good, 24, 0xFFFFFFFF),
        "internal_id": replace_u32(good, 44, 12),
        "model_count": replace_u32(good, 48, 161),
        "vertex_extent": replace_u32(good, 40 + 20 + 12, 0xFFFFFFFF),
        "triangle_extent": replace_u32(good, 40 + 20 + 16, 0xFFFFFFFF),
        "quad_extent": replace_u32(good, 40 + 20 + 20, 0xFFFFFFFF),
        "palette_extent": replace_u32(good, 52, 0xFFFFFFFF),
        "metadata_before_geometry": replace_u32(good, 40 + 16, 1),
        "metadata_beyond_level": replace_u32(good, 40 + 16, 0xFFFFFFFF),
        "metadata_bad_magic": replace_u32(good, 40 + 76, 0),
        "metadata_bad_spawn_flag": replace_u32(good, 40 + 76 + 4, 2),
        "metadata_bad_model_count": replace_u32(good, 40 + 76 + 32, 2),
        "metadata_bad_sky_flag": replace_u32(good, 40 + 76 + 36, 2),
        "short_header": b"CBWB",
        "truncated": good[:-1],
    }
    with tempfile.TemporaryDirectory(prefix="crash3-level-sd-") as folder:
        folder = Path(folder)
        source = folder / "test.c"
        source.write_text(HARNESS)
        (folder / "valid.bin").write_bytes(good)
        paths = []
        for name, data in cases.items():
            path = folder / (name + ".bin")
            path.write_bytes(data)
            paths.append(str(path))
        paths.append(str(folder / "missing.bin"))
        executable = folder / "test.exe"
        subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11",
                        "-Wall", "-Wextra", "-Werror", "-O2", "-UNDEBUG", "-I" + str(ROOT / "source"),
                        str(ROOT / "source/level_sd.c"), str(source),
                        "-o", str(executable)], check=True)
        subprocess.run([str(executable), str(folder / "valid.bin"),
                        str(ROOT / "game_data/levels.bin")] + paths, check=True)


if __name__ == "__main__":
    main()
