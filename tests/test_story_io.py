"""Verify that a failed device flush cannot rotate either save generation."""
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
static int fail_sync, sync_calls;
static int injected_sync(int descriptor);
#ifdef _WIN32
#define _commit injected_sync
#else
#define fsync injected_sync
#endif
#include "story.c"
#ifdef _WIN32
#undef _commit
#else
#undef fsync
#endif
static int injected_sync(int descriptor)
{
    sync_calls++;
    if (fail_sync) return -1;
#ifdef _WIN32
    return _commit(descriptor);
#else
    return fsync(descriptor);
#endif
}
void audio_play_sfx(SfxId id) { (void)id; }
static void assert_generation(const char *path, const SaveData *expected)
{
    SaveData actual;
    assert(read_save_file(path, &actual) == 0);
    assert(memcmp(&actual, expected, sizeof(actual)) == 0);
}
int main(void)
{
    story_init();
    assert(story_save() == 0);
    SaveData first = g_save;
    g_save.levels[0x0B].crystal = 1;
    assert(story_save() == 0);
    SaveData second = g_save;
    assert_generation("save.bak", &first);
    fail_sync = 1;
    g_save.levels[0x0E].crystal = 1;
    assert(story_save() == -1);
    assert(sync_calls == 3);
    assert_generation("save.dat", &second);
    assert_generation("save.bak", &first);
    FILE *temporary = fopen("save.tmp", "rb");
    assert(temporary == NULL);
    assert(story_load() == 0);
    assert(g_save.crystals_count == 1);
    fail_sync = 0;
    assert(story_save() == 0);
    assert(sync_calls == 4);
    puts("PASS: device flush failure preserves both save generations; retry commits");
    return 0;
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="crash3-story-io-") as folder:
        folder = Path(folder)
        source = folder / "test.c"
        source.write_text(HARNESS)
        executable = folder / "test.exe"
        subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11",
                        "-Wall", "-Wextra", "-Werror", "-O2", "-UNDEBUG",
                        '-DSTORY_SAVE_ROOT="."', "-I" + str(ROOT / "source"),
                        str(source), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], cwd=folder, check=True)


if __name__ == "__main__":
    main()
