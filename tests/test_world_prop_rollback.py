"""Verify prop-load failure releases the complete disabled prop asset set."""
from pathlib import Path
import subprocess
import sys
import tempfile

from test_world_runtime import SDK

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <stdbool.h>
#include <stdio.h>
static const char **fixture_paths;
static int next_prop, missing_prop = -1;
static FILE *fixture_fopen(const char *path, const char *mode)
{
    (void)path;
    int index = next_prop++;
    assert(index < 8);
    return index == missing_prop ? NULL : fopen(fixture_paths[index], mode);
}
#define fopen fixture_fopen
#include "world_texture.c"
#undef fopen
#undef batch_count
const unsigned char world_shader_shbin[4] = {0};
const unsigned int world_shader_shbin_size = 4;
int main(int argc, char **argv)
{
    assert(argc == 9);
    fixture_paths = (const char **)(argv + 1);
    assert(world_texture_init() == 0);
    for (int missing = 0; missing < 5; ++missing) {
        missing_prop = -1; next_prop = 0;
        assert(world_texture_load_props() == 0);
        assert(world_texture_props_ready() && live == 8 && texture_live > 0);
        missing_prop = missing; next_prop = 0;
        assert(world_texture_load_props() == -1);
        assert(!world_texture_props_ready());
        assert(live == 0 && texture_live == 0);
        assert(current == &world_asset);
        for (unsigned i = 0; i < PROP_ASSET_COUNT; ++i) {
            assert(!prop_assets[i].bytes && !prop_assets[i].tex);
            assert(!prop_assets[i].tex_count && !prop_assets[i].batch_count);
        }
    }
    /* Optional props may be absent without disabling required props. */
    for (int missing = 5; missing < 8; ++missing) {
        missing_prop = missing; next_prop = 0;
        assert(world_texture_load_props() == 0);
        assert(world_texture_props_ready() && live == 7);
        world_texture_exit();
        assert(live == 0 && texture_live == 0);
        assert(world_texture_init() == 0);
    }
    world_texture_exit();
    puts("PASS prop rollback: required failures release all props; optional failures stay usable");
    return 0;
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="crash-prop-rollback-") as directory:
        temporary = Path(directory)
        (temporary / "citro3d.h").write_text(SDK)
        (temporary / "3ds.h").write_text('#include "citro3d.h"\n')
        (temporary / "test.c").write_text('#include "citro3d.h"\n' + HARNESS)
        executable = temporary / "test.exe"
        subprocess.run([sys.executable, "-m", "ziglang", "cc", "-O2", "-UNDEBUG",
                        "-Wall", "-Wextra", "-Werror", "-I" + str(temporary),
                        "-I" + str(ROOT / "source"), str(temporary / "test.c"),
                        "-o", str(executable)], check=True)
        names = ("wood", "tnt", "nitro", "checkpoint", "wumpa", "spring", "bounce", "aku_mask")
        subprocess.run([str(executable)] + [str(ROOT / "game_data" / "characters" / (name + ".cwt"))
                                           for name in names], check=True)


if __name__ == "__main__":
    main()
