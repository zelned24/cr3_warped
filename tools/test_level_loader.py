"""Compile and run the production level loader with a minimal host SDK adapter.

Usage: python tools/test_level_loader.py --cc C:/path/to/gcc.exe
The adapter replaces allocation/draw calls only; all parsing and mesh expansion
come directly from source/level.c. No Nintendo SDK or console is needed.
"""
import argparse
import pathlib
import struct
import subprocess
import tempfile
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def fixture():
    level = bytearray(b"CBW3" + struct.pack("<IIII", 11, 2, 1, 0))
    level += struct.pack("<iiiIII", 0, 0, 0, 4, 0, 1)
    for x, z in ((0, 0), (10, 0), (0, 10), (10, 10)):
        level += struct.pack("<hhhH", x, 0, z, 0)
    level += struct.pack("<5H", 0, 1, 2, 3, 0)
    # This header is intentionally at offset 86 (2 mod 4).
    level += struct.pack("<iiiIII", -1600, 3200, -4800, 3, 1, 0)
    for x, z in ((0, 0), (10, 0), (0, 10)):
        level += struct.pack("<hhhH", x, 0, z, 0)
    level += struct.pack("<4H", 0, 1, 2, 0)
    level += bytes((255, 128, 64, 0))
    return b"CBWB" + struct.pack("<III", 1, 1, 0) + struct.pack("<III", 11, 28, len(level)) + level


def collision_fixture():
    # Two overlapping floors, a separate slope, and a vertical wall.
    vertices = [(0, 0, 0), (100, 0, 0), (0, 0, 100),
                (0, 100, 0), (100, 100, 0), (0, 100, 100),
                (200, 0, 0), (300, 50, 0), (200, 0, 100),
                (400, 0, 0), (400, 100, 0), (400, 0, 100)]
    level = bytearray(b"CBW3" + struct.pack("<IIII", 11, 1, 1, 0))
    level += struct.pack("<iiiIII", 0, 0, 0, len(vertices), 4, 0)
    for x, y, z in vertices:
        level += struct.pack("<hhhH", x, y, z, 0)
    for first in range(0, len(vertices), 3):
        level += struct.pack("<4H", first, first + 1, first + 2, 0)
    level += bytes((255, 128, 64, 0))
    return b"CBWB" + struct.pack("<III", 1, 1, 0) + struct.pack("<III", 11, 28, len(level)) + level


def metadata_fixture():
    data = bytearray(fixture()[28:])
    palette = data[-4:]
    del data[-4:]
    struct.pack_into("<I", data, 16, len(data))
    data += b"LMD1" + struct.pack("<IiiiIIIIII", 1, 4096, 8192, 12288,
                                  0, 0, 0, 2, 1, 0)
    data += palette
    return b"CBWB" + struct.pack("<III", 1, 1, 0) + struct.pack("<III", 11, 28, len(data)) + data


HARNESS = r'''
#undef NDEBUG
#include <assert.h>
#ifdef NDEBUG
#error Collision regression assertions must remain enabled
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
static const uint8_t *active_blob;
static uint32_t active_size;
int level_sd_available(void) { return active_blob != NULL; }
const uint8_t *level_sd_blob(void) { return active_blob; }
uint32_t level_sd_blob_size(void) { return active_size; }
#include "level.c"

static uint8_t *read_file(const char *path, uint32_t *size)
{
    FILE *f = fopen(path, "rb"); assert(f);
    assert(fseek(f, 0, SEEK_END) == 0);
    long n = ftell(f); assert(n > 0 && (uint64_t)n <= UINT32_MAX);
    rewind(f);
    uint8_t *p = malloc((size_t)n); assert(p);
    assert(fread(p, 1, (size_t)n, f) == (size_t)n);
    fclose(f); *size = (uint32_t)n; return p;
}

int main(int argc, char **argv)
{
    assert(argc == 5);
    uint32_t size;
    uint8_t *fixture = read_file(argv[1], &size);
    for (int shift = 0; shift < 4; shift++) {
        uint8_t *base = malloc(size + 4); assert(base);
        memcpy(base + shift, fixture, size);
        active_blob = base + shift; active_size = size;
        s_parsed_blob = NULL; s_entry_count = 0;
        assert(level_load(11) == 0);
        assert(level_model_count() == 2 && level_vertex_count() == 9);
        assert(level_vertices()[6].x == -100);
        assert(level_vertices()[6].y == 200);
        assert(level_vertices()[6].z == -300);
        const level_vertex *previous = level_vertices();
        force_alloc_fail = 1;
        assert(level_load(11) == -1);
        assert(level_vertices() == previous && level_model_count() == 2);
        force_alloc_fail = 0;
        // Oversized vertex count must fail before reading the vertex block.
        memset(base + shift + 28 + 20 + 12, 0xff, 4);
        assert(level_load(11) == -1);
        assert(level_vertices() == previous);
        level_unload(); free(base);
    }
    free(fixture);
    uint8_t *real = read_file(argv[2], &size);
    for (int shift = 0; shift < 4; shift++) {
        uint8_t *base = malloc(size + 4); assert(base);
        memcpy(base + shift, real, size);
        active_blob = base + shift; active_size = size;
        s_parsed_blob = NULL; s_entry_count = 0;
        int count = level_count(); assert(count == 42);
        for (int i = 0; i < count; i++) {
            uint32_t id = level_get(i)->id;
            assert(level_load(id) == 0);
            assert(level_vertex_count() > 0);
            if (id == 11) {
                float x, y, z;
                assert(level_spawn_point(&x, &y, &z) == 0);
                assert(fabsf(x - 3034.164794921875f) < 0.001f);
                assert(y == 200.0f && z == 25450.0f);
                assert(level_model_at(x, z) == 1);
                assert(level_height_below(x, z, y) == 189);
                assert(level_model(5)->is_skybox);
            }
            level_unload();
        }
        free(base);
    }
    free(real);
    uint8_t *collision = read_file(argv[3], &size);
    active_blob = collision; active_size = size;
    s_parsed_blob = NULL; s_entry_count = 0;
    assert(level_load(11) == 0);
    assert(level_height(10, 10) == 100);
    assert(level_height_below(10, 10, 20) == 0);
    assert(level_height_below(10, 10, 100) == 100);
    assert(level_height_below(10, 10, -1) == -1000000);
    assert(level_height_below(0, 0, 0) == 0);
    assert(level_height_below(-10, -10, 1000) == -1000000);
    assert(level_height_below(210, 10, 10) == 5);
    assert(level_height_below(210, 10, 4) == -1000000);
    assert(level_height_below(400, 10, 1000) == -1000000);
    assert(level_height_below(NAN, 10, 1000) == -1000000);
    assert(level_height_below(10, 10, NAN) == -1000000);
    assert(level_spawn_point(NULL, NULL, NULL) == -1);
    level_unload(); free(collision);
    uint8_t *metadata = read_file(argv[4], &size);
    active_blob = metadata; active_size = size;
    s_parsed_blob = NULL; s_entry_count = 0;
    assert(level_load(11) == 0);
    assert(level_model(0)->is_skybox && !level_model(1)->is_skybox);
    assert(level_render_visible(0, 0, 0, 100000) == 3);
    assert(level_render_visible(1000000, 0, 0, 1) == 0);
    assert(level_height(5, 5) == -1000000);
    assert(level_model_at(5, 5) == -1);
    float sx, sy, sz;
    assert(level_spawn_point(&sx, &sy, &sz) == 0);
    assert(sx == 1 && sy == 2 && sz == 3);
    level_unload(); free(metadata);
    puts("PASS: packed fixture, malformed counts, signed origins, 42 real levels x 4 alignments, stacked floors/slope/wall");
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", nargs="+", default=[sys.executable, "-m", "ziglang", "cc"])
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="crash-level-test-") as tmp:
        tmp = pathlib.Path(tmp)
        (tmp / "3ds.h").write_text(
            "#include <stdlib.h>\n"
            "static int force_alloc_fail;\n"
            "static inline void *linearAlloc(size_t n) { return force_alloc_fail ? NULL : malloc(n); }\n"
            "static inline void linearFree(void *p) { free(p); }\n")
        (tmp / "citro3d.h").write_text(
            "#define GPU_TRIANGLES 0\n"
            "static inline void C3D_DrawArrays(int p,int f,int n) {(void)p;(void)f;(void)n;}\n")
        (tmp / "test.c").write_text(HARNESS)
        (tmp / "fixture.bin").write_bytes(fixture())
        (tmp / "collision.bin").write_bytes(collision_fixture())
        (tmp / "metadata.bin").write_bytes(metadata_fixture())
        exe = tmp / "test.exe"
        cmd = args.cc + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", "-UNDEBUG",
               "-DLEVELS_FROM_SD", "-I" + str(tmp), "-I" + str(ROOT / "source"),
               str(tmp / "test.c"), "-lm", "-o", str(exe)]
        if args.sanitize:
            cmd += ["-fsanitize=undefined,address", "-fno-omit-frame-pointer"]
        subprocess.run(cmd, check=True)
        subprocess.run([str(exe), str(tmp / "fixture.bin"),
                        str(ROOT / "game_data" / "levels.bin"),
                        str(tmp / "collision.bin"), str(tmp / "metadata.bin")], check=True)


if __name__ == "__main__":
    main()
