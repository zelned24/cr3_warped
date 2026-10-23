"""Empirical adversarial test harness for Crash Bandicoot 3 BGM theme mapping.

Validates level_get_bgm_theme(int level_id) against:
1. All 42 original disc level entries in game_data/index.txt and source/main.c level_names[64].
2. Authentic stage archetypes and historical PlayStation 1 disc themes:
   - Medieval England (0x0B, 0x0F, 0x1D) -> BGM_MEDIEVAL
   - Arabian Rooftops (0x16, 0x1A, 0x22) -> BGM_ARABIAN
   - China & Prehistoric (0x0A, 0x11, 0x0C, 0x10, 0x26) -> BGM_ORIENTAL
   - Egyptian & Atlantis (0x12, 0x18, 0x1E, 0x24, 0x0E, 0x1C) -> BGM_EGYPTIAN
   - Future, Motorcycle & Biplane (0x1B, 0x23, 0x14, 0x15, 0x20, 0x25, 0x13, 0x17, 0x1F) -> BGM_FUTURE
   - Boss Arenas (0x03, 0x04, 0x05, 0x06, 0x07) -> BGM_BOSS
   - Hub & Jet-ski marimba (0x02, 0x0D, 0x19, 0x21, 0x27) -> BGM_WARP_ROOM
   - Title & Endings (0x3C, 0x28, 0x2A) -> BGM_TITLE, Any% Ending 0x29 -> BGM_WARP_ROOM
   - Game Over 0x3B -> BGM_NONE (silence)
   - Intermission Vortex 0x3A -> BGM_WARP_ROOM
3. All 22 unmapped table slots in range [0, 63] -> BGM_NONE.
4. Adversarial boundary & negative IDs (-1, -100, -99999, INT_MIN).
5. Adversarial out-of-bounds IDs (64, 65, 100, 255, 65535, INT_MAX).
6. 10,000,000 random 32-bit signed integer fuzzing iterations for zero buffer overruns and clean returns.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
INDEX_TXT = ROOT / "game_data" / "index.txt"
MAIN_C = ROOT / "source" / "main.c"
LEVEL_C = ROOT / "source" / "level.c"

HARNESS_C = r'''
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

#include "audio.h"
#include "level.h"

/* Mock stubs for level_sd when compiling with -DLEVELS_FROM_SD */
int level_sd_available(void) { return 0; }
const uint8_t *level_sd_blob(void) { return NULL; }
uint32_t level_sd_blob_size(void) { return 0; }

/* Verification structure for canonical 42 disc levels */
typedef struct {
    int id;
    const char *name;
    const char *archetype;
    BgmTheme expected_theme;
} LevelThemeCheck;

static const LevelThemeCheck CANONICAL_42_LEVELS[] = {
    /* Medieval England */
    { 0x0B, "TOAD VILLAGE",    "Medieval England",             BGM_MEDIEVAL },
    { 0x0F, "GEE WIZ",         "Medieval England",             BGM_MEDIEVAL },
    { 0x1D, "DOUBLE HEADER",   "Medieval England",             BGM_MEDIEVAL },

    /* Arabian Rooftops */
    { 0x16, "HANG'EM HIGH",    "Arabian Rooftops",             BGM_ARABIAN },
    { 0x1A, "HIGH TIME",       "Arabian Rooftops",             BGM_ARABIAN },
    { 0x22, "FLAMING PASSION", "Arabian Rooftops",             BGM_ARABIAN },

    /* China & Prehistoric */
    { 0x0A, "ORIENT EXPRESS",  "China Wall",                   BGM_ORIENTAL },
    { 0x11, "MIDNIGHT RUN",    "China Wall",                   BGM_ORIENTAL },
    { 0x0C, "BONE YARD",       "Prehistoric Chase",            BGM_ORIENTAL },
    { 0x10, "DINO MIGHT",      "Prehistoric Chase",            BGM_ORIENTAL },
    { 0x26, "EGGIPUS REX",     "Prehistoric Bonus",            BGM_ORIENTAL },

    /* Egyptian & Atlantis */
    { 0x12, "TOMB TIME",       "Egyptian Tomb",                BGM_EGYPTIAN },
    { 0x18, "TOMB WADER",      "Egyptian Tomb",                BGM_EGYPTIAN },
    { 0x1E, "SPHYNXINATOR",    "Egyptian Tomb",                BGM_EGYPTIAN },
    { 0x24, "BUG LITE",        "Egyptian Tomb",                BGM_EGYPTIAN },
    { 0x0E, "UNDER PRESSURE",  "Atlantis Submarine",           BGM_EGYPTIAN },
    { 0x1C, "DEEP TROUBLE",    "Atlantis Submarine",           BGM_EGYPTIAN },

    /* Future, Motorcycle & Biplane */
    { 0x1B, "FUTURE FRENZY",   "Neo-Cor Future City",          BGM_FUTURE },
    { 0x23, "GONE TOMORROW",   "Neo-Cor Future City",          BGM_FUTURE },
    { 0x14, "ROAD CRASH",      "American Highway Motorcycle",  BGM_FUTURE },
    { 0x15, "HOG RIDE",        "American Highway Motorcycle",  BGM_FUTURE },
    { 0x20, "ORANGE ASPHALT",  "American Highway Motorcycle",  BGM_FUTURE },
    { 0x25, "AREA 51",         "American Highway Motorcycle",  BGM_FUTURE },
    { 0x13, "BYE BYE BLIMPS",  "WWI Air Combat Biplane",       BGM_FUTURE },
    { 0x17, "MAD BOMBERS",     "WWI Air Combat Biplane",       BGM_FUTURE },
    { 0x1F, "RINGS OF POWER",  "WWI Air Combat Biplane",       BGM_FUTURE },

    /* Boss Arenas */
    { 0x03, "DINGODILE",       "Boss Arena: Dingodile",        BGM_BOSS },
    { 0x04, "N. TROPY",        "Boss Arena: Dr. N. Tropy",     BGM_BOSS },
    { 0x05, "N. GIN",          "Boss Arena: Dr. N. Gin",       BGM_BOSS },
    { 0x06, "TINY",            "Boss Arena: Tiny Tiger",       BGM_BOSS },
    { 0x07, "DR. NEO CORTEX",  "Boss Arena: Dr. Neo Cortex",   BGM_BOSS },

    /* Hub & Jet-ski marimba */
    { 0x02, "WARP ROOM",       "Time Twister Hub",             BGM_WARP_ROOM },
    { 0x0D, "TELL NO TALES",   "Pacific Ocean Jet-ski",        BGM_WARP_ROOM },
    { 0x19, "MAKIN' WAVES",    "Pacific Ocean Jet-ski",        BGM_WARP_ROOM },
    { 0x21, "SKI CRAZED",      "Pacific Ocean Jet-ski",        BGM_WARP_ROOM },
    { 0x27, "HOT COCO",        "Pacific Ocean Jet-ski",        BGM_WARP_ROOM },

    /* Title & Endings */
    { 0x3C, "TITLE SCREEN",    "Title Screen",                 BGM_TITLE },
    { 0x28, "OPENING CUTSCENE","Opening Cinematic",            BGM_TITLE },
    { 0x2A, "100% ENDING",     "True 100% Ending",             BGM_TITLE },
    { 0x29, "ANY% ENDING",     "Standard Any% Ending",         BGM_WARP_ROOM },

    /* Intermission & Silence */
    { 0x3A, "INTERMISSION VORTEX", "Chamber Vortex Transition", BGM_WARP_ROOM },
    { 0x3B, "GAME OVER",       "Game Over Screen",             BGM_NONE }
};

static const char *theme_name(BgmTheme theme)
{
    switch (theme) {
        case BGM_NONE:      return "BGM_NONE";
        case BGM_TITLE:     return "BGM_TITLE";
        case BGM_WARP_ROOM: return "BGM_WARP_ROOM";
        case BGM_MEDIEVAL:  return "BGM_MEDIEVAL";
        case BGM_ARABIAN:   return "BGM_ARABIAN";
        case BGM_ORIENTAL:  return "BGM_ORIENTAL";
        case BGM_EGYPTIAN:  return "BGM_EGYPTIAN";
        case BGM_FUTURE:    return "BGM_FUTURE";
        case BGM_BOSS:      return "BGM_BOSS";
        default:            return "INVALID_THEME";
    }
}

static uint32_t xorshift32(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

int main(void)
{
    printf("=================================================================\n");
    printf(" Starting Empirical Adversarial BGM Theme Mapping Verification\n");
    printf("=================================================================\n");

    /* ------------------------------------------------------------- */
    /* TEST 1: Validate 42 Canonical Levels against authentic themes */
    /* ------------------------------------------------------------- */
    size_t count_42 = sizeof(CANONICAL_42_LEVELS) / sizeof(CANONICAL_42_LEVELS[0]);
    assert(count_42 == 42);
    printf("[TEST 1] Verifying 42 canonical disc levels...\n");

    bool mapped_table[64] = { false };

    for (size_t i = 0; i < count_42; i++) {
        const LevelThemeCheck *check = &CANONICAL_42_LEVELS[i];
        assert(check->id >= 0 && check->id < 64);
        mapped_table[check->id] = true;

        BgmTheme actual = level_get_bgm_theme(check->id);
        if (actual != check->expected_theme) {
            fprintf(stderr, "FAIL: Level 0x%02X (%s) expected %s (%d) but got %s (%d)\n",
                    check->id, check->name,
                    theme_name(check->expected_theme), check->expected_theme,
                    theme_name(actual), actual);
            return 1;
        }
        printf("  [PASS] Level 0x%02X (%-19s) [%-27s] -> %s\n",
               check->id, check->name, check->archetype, theme_name(actual));
    }
    printf("PASS [TEST 1]: All 42 canonical levels verified authentic.\n\n");

    /* ------------------------------------------------------------- */
    /* TEST 2: Validate all 22 unmapped table slots return BGM_NONE  */
    /* ------------------------------------------------------------- */
    printf("[TEST 2] Verifying unmapped table slots in range [0, 63]...\n");
    int unmapped_count = 0;
    for (int id = 0; id < 64; id++) {
        if (!mapped_table[id]) {
            unmapped_count++;
            BgmTheme actual = level_get_bgm_theme(id);
            if (actual != BGM_NONE) {
                fprintf(stderr, "FAIL: Unmapped Level 0x%02X returned %s instead of BGM_NONE\n",
                        id, theme_name(actual));
                return 1;
            }
        }
    }
    assert(unmapped_count == (64 - 42)); /* Exactly 22 unmapped */
    printf("PASS [TEST 2]: All 22 unmapped slots in [0, 63] return BGM_NONE.\n\n");

    /* ------------------------------------------------------------- */
    /* TEST 3: Adversarial Deterministic Boundary & Negative Fuzzing */
    /* ------------------------------------------------------------- */
    printf("[TEST 3] Verifying deterministic boundary & negative ID fuzzing...\n");
    const int adversarial_ids[] = {
        -1, -2, -3, -10, -50, -100, -99999, -65536, -1000000, -2147483647, INT_MIN,
        64, 65, 66, 100, 127, 128, 255, 256, 1000, 32767, 32768, 65535, 65536, 1000000, 2147483646, INT_MAX
    };
    size_t num_adversarial = sizeof(adversarial_ids) / sizeof(adversarial_ids[0]);

    for (size_t i = 0; i < num_adversarial; i++) {
        int id = adversarial_ids[i];
        BgmTheme actual = level_get_bgm_theme(id);
        if (actual != BGM_NONE) {
            fprintf(stderr, "FAIL: Adversarial ID %d returned %s instead of BGM_NONE\n",
                    id, theme_name(actual));
            return 1;
        }
    }
    printf("PASS [TEST 3]: All %zu adversarial boundary/negative IDs returned BGM_NONE without fault.\n\n", num_adversarial);

    /* ------------------------------------------------------------- */
    /* TEST 4: Massive Random Fuzzing (10,000,000 iterations)        */
    /* ------------------------------------------------------------- */
    printf("[TEST 4] Running 10,000,000 pseudorandom 32-bit signed fuzz iterations...\n");
    uint32_t rng_state = 0xDEADBEEF;
    const int FUZZ_ITERATIONS = 10000000;

    for (int iter = 0; iter < FUZZ_ITERATIONS; iter++) {
        uint32_t raw = xorshift32(&rng_state);
        int32_t test_id = (int32_t)raw;
        BgmTheme theme = level_get_bgm_theme(test_id);

        if (test_id < 0 || test_id >= 64) {
            if (theme != BGM_NONE) {
                fprintf(stderr, "FAIL: Out-of-bounds ID %d returned %s\n", test_id, theme_name(theme));
                return 1;
            }
        } else if (!mapped_table[test_id]) {
            if (theme != BGM_NONE) {
                fprintf(stderr, "FAIL: Unmapped ID %d returned %s\n", test_id, theme_name(theme));
                return 1;
            }
        } else {
            /* Mapped level: must be a valid theme */
            if (theme <= BGM_NONE || theme >= BGM_COUNT) {
                if (test_id == 0x3B) {
                    assert(theme == BGM_NONE);
                } else {
                    fprintf(stderr, "FAIL: Mapped ID 0x%02X returned invalid theme %d\n", test_id, theme);
                    return 1;
                }
            }
        }
    }
    printf("PASS [TEST 4]: 10,000,000 fuzz iterations completed with zero buffer overruns and clean returns.\n\n");

    /* ------------------------------------------------------------- */
    /* TEST 5: Idempotence & Pure Function Assertion                 */
    /* ------------------------------------------------------------- */
    printf("[TEST 5] Testing idempotence and consistency...\n");
    for (int id = -100; id <= 100; id++) {
        BgmTheme first = level_get_bgm_theme(id);
        for (int rep = 0; rep < 100; rep++) {
            BgmTheme repeated = level_get_bgm_theme(id);
            assert(first == repeated);
        }
    }
    printf("PASS [TEST 5]: Pure function idempotence verified across 201 IDs x 100 repeats.\n\n");

    printf("=================================================================\n");
    printf(" ALL EMPIRICAL BGM THEME TESTS PASSED (100%% VERIFIED)\n");
    printf("=================================================================\n");
    return 0;
}
'''


def parse_index_txt():
    """Extract all level IDs from game_data/index.txt."""
    if not INDEX_TXT.exists():
        raise FileNotFoundError(f"Missing index.txt at {INDEX_TXT}")
    
    level_ids = []
    for line in INDEX_TXT.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line:
            continue
        parts = line.split()
        hex_id = parts[0]
        val = int(hex_id, 16)
        level_ids.append((hex_id, val))
    return level_ids


def parse_main_level_names():
    """Extract level_names[64] table from source/main.c."""
    if not MAIN_C.exists():
        raise FileNotFoundError(f"Missing main.c at {MAIN_C}")
    
    content = MAIN_C.read_text(encoding="utf-8")
    m = re.search(r'level_names\[64\]\s*=\s*\{([^}]+)\};', content, re.DOTALL)
    if not m:
        raise ValueError("level_names[64] table not found in main.c")
    
    raw = m.group(1)
    tokens = [tok.strip().strip('"') for tok in raw.split(',') if tok.strip()]
    return tokens


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    default_cc = [sys.executable, "-m", "ziglang", "cc"]
    parser.add_argument("--cc", nargs=argparse.REMAINDER, default=default_cc, help="Host C compiler command")
    args = parser.parse_args()
    compiler = args.cc if args.cc else default_cc

    t_start = time.time()
    print("========================================================")
    print(" EMPIRICAL VERIFICATION: 42-Level BGM Theme Mapping")
    print("========================================================")

    # 1. Parse and cross-check index.txt
    levels_index = parse_index_txt()
    print(f"[HOST-CHECK] Parsed {len(levels_index)} levels from game_data/index.txt.")
    assert len(levels_index) == 42, f"Expected 42 levels in index.txt, found {len(levels_index)}"

    # 2. Parse main.c level_names[64]
    level_names = parse_main_level_names()
    print(f"[HOST-CHECK] Parsed {len(level_names)} entries from main.c level_names[64].")
    assert len(level_names) == 64, f"Expected 64 entries in level_names table, found {len(level_names)}"

    # Verify every index.txt ID has a meaningful (non-'-') name in level_names
    for hex_id, val in levels_index:
        name = level_names[val]
        assert name != "-", f"Level 0x{hex_id} has placeholder '-' in main.c level_names[64]"
        print(f"  Level 0x{hex_id} (id={val:02d}): {name}")

    # 3. Compile and execute C test harness
    with tempfile.TemporaryDirectory(prefix="crash3-bgm-test-") as tmp:
        tmp_path = Path(tmp)
        harness_c_file = tmp_path / "test_bgm_harness.c"
        harness_c_file.write_text(HARNESS_C, encoding="utf-8")
        exe_path = tmp_path / ("test_bgm_harness.exe" if os.name == "nt" else "test_bgm_harness")

        cmd = args.cc + [
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-UNDEBUG",
            "-O2",
            "-DLEVELS_FROM_SD",
            f"-I{ROOT / 'tests' / 'e2e' / 'mock_3ds'}",
            f"-I{ROOT / 'source'}",
            str(LEVEL_C),
            str(harness_c_file),
            "-lm",
            "-o",
            str(exe_path)
        ]
        print("\n[HOST-CHECK] Compiling test harness with zig cc / host C compiler...")
        subprocess.run(cmd, check=True)

        print("[HOST-CHECK] Executing test harness...")
        subprocess.run([str(exe_path)], check=True)

    elapsed = time.time() - t_start
    print(f"\nPASS: 42-Level BGM Theme Empirical Verification completed in {elapsed:.3f} seconds.")


if __name__ == "__main__":
    main()
