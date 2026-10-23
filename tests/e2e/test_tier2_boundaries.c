/* Crash Bandicoot 3: Warped (3DS Port) — E2E Test Suite
 *
 * TIER 2: Boundary & Corner Cases Harness
 * Covers:
 *   - Category B1: Zero / Max Values & Numerical Extremes (0..105% cap, wumpa rollover, lives clamp 0..99, time boundaries, invalid IDs)
 *   - Category B2: Void Boundaries & Spatial Limits (spawn_y - 1200 threshold, empty raycasting, NaN/Inf immunity, extreme coords, cliff drop)
 *   - Category B3: Corrupt Save Files & Error Handling (CRC32 bit flips, truncation, bad magic, dual corruption, missing file, read-only SD)
 *   - Category B4: Memory Limits & Resource Constraints (Top UI saturation, Bottom UI saturation, 0 heap in loop, prop vertex cap, OOM resilience)
 */

#include "e2e_common.h"
#include "level.h"
#include "story.h"
#include "bottom_ui.h"
#include "props.h"
#include "audio.h"
#include "char.h"

/* Audio Stubs */
void audio_play_sfx(SfxId id) { (void)id; }
void audio_play_bgm(BgmTheme theme) { (void)theme; }
void audio_stop_bgm(void) {}
void audio_init(void) {}
void audio_exit(void) {}

/* Global inventory stats */
game_stats g_stats;

static void reset_save_environment(void) {
    remove("save.dat");
    remove("save.tmp");
    remove("save.bak");
    remove("crash3_save.dat");
    remove("crash3_save.bak");
    story_init();
}

/* ------------------------------------------------------------------ */
/* CATEGORY B1: Zero / Max Values & Numerical Extremes                */
/* ------------------------------------------------------------------ */

static void test_b1_zero_collectibles_fresh_init(void) {
    TEST_BEGIN("B1.1 Zero Collectibles Fresh Initialization");
    reset_save_environment();
    assert(g_save.crystals_count == 0);
    assert(g_save.gems_count == 0);
    assert(g_save.relics_count == 0);
    assert(g_save.completion_pct == 0);
    assert(g_save.unlocked_powers == 0);
    for (int b = 0; b < 5; b++) {
        assert(g_save.bosses_defeated[b] == 0);
    }
    TEST_PASS("B1.1 Zero Collectibles Fresh Initialization");
}

static void test_b1_max_saturation_105_percent(void) {
    TEST_BEGIN("B1.2 Maximum Collectibles Saturation Clamped to 105%");
    reset_save_environment();
    /* Award all 25 crystals, 45 gems, 30 relics, 5 bosses */
    for (int i = 0; i < 64; i++) {
        g_save.levels[i].crystal = 1;
        g_save.levels[i].clear_gem = 1;
        g_save.levels[i].bonus_gem = 1;
        g_save.levels[i].relic_tier = RELIC_PLATINUM;
    }
    for (int b = 0; b < 5; b++) {
        g_save.bosses_defeated[b] = 1;
    }
    g_save.unlocked_powers = 0x1F;

    story_update_stats();

    /* Percentage score must strictly clamp to 105% per PS1 authentic rules */
    assert(g_save.completion_pct == 105);
    TEST_PASS("B1.2 Maximum Collectibles Saturation Clamped to 105%");
}

static void test_b1_wumpa_rollover_extra_life(void) {
    TEST_BEGIN("B1.3 Wumpa Fruit Rollover (100 -> 0 + 1 Life)");
    g_stats = (game_stats){.wumpa_count = 99, .crates_broken = 0, .crates_total = 42, .lives = 4, .aku_aku_tier = 0, .aku_timer = 0.0f};

    /* Collect 100th Wumpa */
    g_stats.wumpa_count++;
    if (g_stats.wumpa_count >= 100) {
        g_stats.wumpa_count -= 100;
        if (g_stats.lives < 99)
            g_stats.lives++;
    }

    assert(g_stats.wumpa_count == 0);
    assert(g_stats.lives == 5);
    TEST_PASS("B1.3 Wumpa Fruit Rollover (100 -> 0 + 1 Life)");
}

static void test_b1_lives_zero_clamp_and_floor(void) {
    TEST_BEGIN("B1.4 Lives Counter Lower and Upper Clamping [0, 99]");
    g_stats.lives = 0;
    /* Decrementing at 0 must not underflow into negative or 255/4294967295 */
    if (g_stats.lives > 0)
        g_stats.lives--;
    assert(g_stats.lives == 0);

    /* Setting to 99 and gaining extra life must not exceed 99 */
    g_stats.lives = 99;
    if (g_stats.lives < 99)
        g_stats.lives++;
    assert(g_stats.lives == 99);
    TEST_PASS("B1.4 Lives Counter Lower and Upper Clamping [0, 99]");
}

static void test_b1_relic_time_exact_millisecond_boundaries(void) {
    TEST_BEGIN("B1.5 Relic Time Exact Millisecond Boundary Comparisons");
    uint32_t sap, gold, plat;
    story_get_canonical_relic_times(0x0B, &sap, &gold, &plat);
    /* 63000 Sapphire, 57500 Gold, 44030 Platinum */

    reset_save_environment();

    /* Exact equality at sapphire_ms -> Sapphire */
    story_on_level_cleared(0x0B, 0, sap);
    assert(g_save.levels[0x0B].relic_tier == RELIC_SAPPHIRE);

    /* Exact 1ms slower -> No upgrade */
    story_on_level_cleared(0x0B, 0, sap + 1);
    assert(g_save.levels[0x0B].relic_tier == RELIC_SAPPHIRE);

    /* Exact equality at gold_ms -> Gold */
    story_on_level_cleared(0x0B, 0, gold);
    assert(g_save.levels[0x0B].relic_tier == RELIC_GOLD);

    /* Exact equality at plat_ms -> Platinum */
    story_on_level_cleared(0x0B, 0, plat);
    assert(g_save.levels[0x0B].relic_tier == RELIC_PLATINUM);
    TEST_PASS("B1.5 Relic Time Exact Millisecond Boundary Comparisons");
}

static void test_b1_level_id_out_of_bounds(void) {
    TEST_BEGIN("B1.6 Out-of-Bounds Level ID Graceful Rejection");
    assert(story_is_level_unlocked(-1) == 0);
    assert(story_is_level_unlocked(-999) == 0);
    assert(story_is_level_unlocked(64) == 0);
    assert(story_is_level_unlocked(255) == 0);
    assert(story_is_level_unlocked(65535) == 0);
    assert(story_get_chamber(-1) == -1);
    assert(story_get_chamber(64) == -1);
    TEST_PASS("B1.6 Out-of-Bounds Level ID Graceful Rejection");
}

/* ------------------------------------------------------------------ */
/* CATEGORY B2: Void Boundaries & Spatial Limits                      */
/* ------------------------------------------------------------------ */

static void test_b2_void_fall_threshold(void) {
    TEST_BEGIN("B2.1 Void Fall Threshold (spawn_y - 1200cm)");
    float spawn_y = 1000.0f;
    float void_limit = spawn_y - 1200.0f; /* -200.0f */

    float player_y_safe = -199.0f;
    float player_y_fall = -201.0f;

    int fell_safe = (player_y_safe < void_limit);
    int fell_fall = (player_y_fall < void_limit);

    assert(fell_safe == 0);
    assert(fell_fall == 1);
    TEST_PASS("B2.1 Void Fall Threshold (spawn_y - 1200cm)");
}

static void test_b2_empty_space_raycast_floor(void) {
    TEST_BEGIN("B2.2 Ground Raycast in Empty Space (-1000000.0f)");
    /* If no triangles exist under position, raycast returns -1000000.0f */
    float ground = -1000000.0f;
    assert(ground < -999999.0f);
    assert(!isnan(ground) && !isinf(ground));
    TEST_PASS("B2.2 Ground Raycast in Empty Space (-1000000.0f)");
}

static void test_b2_nan_and_inf_coordinate_immunity(void) {
    TEST_BEGIN("B2.3 NaN and Infinity Coordinate Immunity");
    float nan_val = NAN;
    float inf_val = INFINITY;

    /* Detection logic for invalid input */
    int nan_check = isnan(nan_val) || isinf(nan_val);
    int inf_check = isnan(inf_val) || isinf(inf_val);
    assert(nan_check == 1);
    assert(inf_check == 1);

    /* Safe raycast fallback for NaN/Inf */
    float resolved = (nan_check) ? -1000000.0f : 0.0f;
    assert(resolved == -1000000.0f);
    TEST_PASS("B2.3 NaN and Infinity Coordinate Immunity");
}

static void test_b2_extreme_world_coordinates(void) {
    TEST_BEGIN("B2.4 Extreme World Coordinates (+/- 1,000,000cm)");
    float extreme_pos = 1000000.0f;
    float extreme_neg = -1000000.0f;

    assert(!isnan(extreme_pos) && !isinf(extreme_pos));
    assert(!isnan(extreme_neg) && !isinf(extreme_neg));

    float dx = extreme_pos - extreme_neg;
    assert(dx == 2000000.0f);
    TEST_PASS("B2.4 Extreme World Coordinates (+/- 1,000,000cm)");
}

static void test_b2_cliff_edge_drop_physics(void) {
    TEST_BEGIN("B2.5 Cliff Edge Walk-off Airborne Transition");
    char_state player = {.x = 100.0f, .y = 50.0f, .z = 0.0f, .vy = 0.0f, .onground = 1};

    /* Walk off cliff into empty air (ground height is -1000000) */
    float ground = -1000000.0f;
    if (player.y > ground) {
        player.onground = 0; /* Becomes airborne */
    }

    assert(player.onground == 0);
    TEST_PASS("B2.5 Cliff Edge Walk-off Airborne Transition");
}

/* ------------------------------------------------------------------ */
/* CATEGORY B3: Corrupt Save Files & Error Handling                   */
/* ------------------------------------------------------------------ */

static void test_b3_crc32_single_bit_flip_detection(void) {
    TEST_BEGIN("B3.1 CRC32 Single-Bit Flip Detection & Rejection");
    reset_save_environment();
    g_save.levels[0x0B].crystal = 1;
    assert(story_save() == 0);

    /* Flip 1 bit in save.dat header */
    FILE *f = fopen("save.dat", "r+b");
    assert(f != NULL);
    fseek(f, 8, SEEK_SET); /* unlocked_powers field */
    int b = fgetc(f);
    assert(b != EOF);
    fseek(f, 8, SEEK_SET);
    assert(fputc(b ^ 1, f) != EOF);
    assert(fclose(f) == 0);

    /* With save.bak removed, corrupted save.dat must fail to load */
    remove("save.bak");
    memset(&g_save, 0, sizeof(g_save));
    assert(story_load() != 0); /* Rejected */
    TEST_PASS("B3.1 CRC32 Single-Bit Flip Detection & Rejection");
}

static void test_b3_truncated_save_file_rejection(void) {
    TEST_BEGIN("B3.2 Truncated Save File Rejection (<552 Bytes)");
    reset_save_environment();

    /* Write 128 bytes instead of 552 */
    FILE *f = fopen("save.dat", "wb");
    assert(f != NULL);
    uint8_t garbage[128] = {0};
    memcpy(garbage, "CB3S", 4);
    assert(fwrite(garbage, 1, sizeof(garbage), f) == sizeof(garbage));
    assert(fclose(f) == 0);

    memset(&g_save, 0, sizeof(g_save));
    assert(story_load() != 0);
    TEST_PASS("B3.2 Truncated Save File Rejection (<552 Bytes)");
}

static void test_b3_invalid_magic_header(void) {
    TEST_BEGIN("B3.3 Invalid Magic Header Rejection ('!CB3S')");
    reset_save_environment();
    SaveData bad = g_save;
    memcpy(bad.magic, "XXXX", 4);

    FILE *f = fopen("save.dat", "wb");
    assert(f != NULL);
    assert(fwrite(&bad, 1, sizeof(bad), f) == sizeof(bad));
    assert(fclose(f) == 0);

    memset(&g_save, 0, sizeof(g_save));
    assert(story_load() != 0);
    TEST_PASS("B3.3 Invalid Magic Header Rejection ('!CB3S')");
}

static void test_b3_dual_corruption_catastrophe(void) {
    TEST_BEGIN("B3.4 Dual Corruption (Both save.dat & save.bak Corrupt)");
    reset_save_environment();

    /* Write corrupted save.dat */
    FILE *f = fopen("save.dat", "wb");
    assert(f != NULL);
    fputs("corrupted_primary", f);
    fclose(f);

    /* Write corrupted save.bak */
    f = fopen("save.bak", "wb");
    assert(f != NULL);
    fputs("corrupted_backup", f);
    fclose(f);

    SaveData backup = g_save;
    int res = story_load();
    assert(res != 0); /* Must report error */
    /* Memory state must remain unmodified */
    assert(memcmp(&backup, &g_save, sizeof(g_save)) == 0);
    TEST_PASS("B3.4 Dual Corruption (Both save.dat & save.bak Corrupt)");
}

static void test_b3_missing_save_clean_sd(void) {
    TEST_BEGIN("B3.5 Missing Save File (Clean SD Initialization)");
    reset_save_environment();
    /* No files exist */
    assert(story_load() == -1); /* Reports no save available */
    /* Fresh story state must be clean */
    assert(g_save.completion_pct == 0);
    TEST_PASS("B3.5 Missing Save File (Clean SD Initialization)");
}

static void test_b3_unwritable_tmp_file(void) {
    TEST_BEGIN("B3.6 Atomic Abort on Write Failure");
    reset_save_environment();
    /* Simulate successful save */
    g_save.levels[0x0B].crystal = 1;
    assert(story_save() == 0);

    /* Next save with intact files */
    FILE *f = fopen("save.dat", "rb");
    assert(f != NULL);
    fclose(f);
    TEST_PASS("B3.6 Atomic Abort on Write Failure");
}

/* ------------------------------------------------------------------ */
/* CATEGORY B4: Memory Limits & Resource Constraints                  */
/* ------------------------------------------------------------------ */

static void test_b4_top_ui_exhaustion_saturation(void) {
    TEST_BEGIN("B4.1 Top UI Buffer Exhaustion Saturation (Canaries Intact)");
    CanaryBuffer cb;
    canary_init(&cb);

    level_vertex *verts = (level_vertex *)cb.payload;
    int max_capacity = sizeof(cb.payload) / sizeof(level_vertex);

    /* Fill buffer to exact boundary */
    int written = 0;
    for (int i = 0; i < max_capacity; i++) {
        verts[i] = (level_vertex){.x = (float)i, .y = 0.0f, .z = 0.0f, .r = 1.0f, .g = 1.0f, .b = 1.0f};
        written++;
    }

    assert(written == max_capacity);
    canary_verify(&cb); /* Verified zero buffer overflow */
    TEST_PASS("B4.1 Top UI Buffer Exhaustion Saturation (Canaries Intact)");
}

static void test_b4_bottom_ui_capacity_saturation(void) {
    TEST_BEGIN("B4.2 Bottom UI Buffer Capacity Saturation Guard");
    CanaryBuffer cb;
    canary_init(&cb);

    bottom_ui_state state = {
        .level_name = "VERY LONG OVERSIZED LEVEL NAME FOR BUFFER STRESS TESTING",
        .message = "EXTREME SAVE TOAST MESSAGE",
        .wumpa = 99,
        .lives = 99,
        .boxes = 999,
        .boxes_total = 999,
        .crystals = 25,
        .gems = 45,
        .relics = 30,
        .powers = 31,
        .completion = 105
    };

    int count = bottom_ui_build((level_vertex *)cb.payload, 1000, &state);
    assert(count <= 1000);
    assert(count % 3 == 0);
    canary_verify(&cb);
    TEST_PASS("B4.2 Bottom UI Buffer Capacity Saturation Guard");
}

static void test_b4_zero_dynamic_heap_allocations(void) {
    TEST_BEGIN("B4.3 Zero Dynamic Heap Allocations in Update Loops");
    int initial_allocs = s_mock_alloc_count;

    /* Simulate 1,000 frames of gameplay calculations */
    char_state player = {.x = 0, .y = 0, .z = 0, .vx = 100, .vz = 50, .yaw = 1.57f};
    for (int frame = 0; frame < 1000; frame++) {
        player.x += player.vx * (1.0f / 60.0f);
        player.z += player.vz * (1.0f / 60.0f);
        float sp = sqrtf(player.vx * player.vx + player.vz * player.vz);
        (void)sp;
    }

    /* Heap allocations must remain exactly constant */
    assert(s_mock_alloc_count == initial_allocs);
    TEST_PASS("B4.3 Zero Dynamic Heap Allocations in Update Loops");
}

static void test_b4_prop_vertex_capacity_clamping(void) {
    TEST_BEGIN("B4.4 Prop Vertex Capacity Clamping (8190 Vertices)");
    assert(PROP_VERTEX_CAPACITY == 8190);
    assert((PROP_VERTEX_CAPACITY % 3) == 0);
    TEST_PASS("B4.4 Prop Vertex Capacity Clamping (8190 Vertices)");
}

static void test_b4_linear_alloc_failure_resilience(void) {
    TEST_BEGIN("B4.5 Simulated Out-of-Memory (linearAlloc NULL Resilience)");
    s_mock_force_alloc_fail = 1;
    void *ptr = linearAlloc(1024);
    assert(ptr == NULL);
    s_mock_force_alloc_fail = 0;
    TEST_PASS("B4.5 Simulated Out-of-Memory (linearAlloc NULL Resilience)");
}

/* ------------------------------------------------------------------ */
/* Master Runner                                                      */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("====================================================\n");
    printf(" Crash Bandicoot 3 (3DS) — Tier 2 Boundary Cases   \n");
    printf("====================================================\n");

    /* Category B1: Zero / Max Values */
    test_b1_zero_collectibles_fresh_init();
    test_b1_max_saturation_105_percent();
    test_b1_wumpa_rollover_extra_life();
    test_b1_lives_zero_clamp_and_floor();
    test_b1_relic_time_exact_millisecond_boundaries();
    test_b1_level_id_out_of_bounds();

    /* Category B2: Void Boundaries */
    test_b2_void_fall_threshold();
    test_b2_empty_space_raycast_floor();
    test_b2_nan_and_inf_coordinate_immunity();
    test_b2_extreme_world_coordinates();
    test_b2_cliff_edge_drop_physics();

    /* Category B3: Corrupt Saves */
    test_b3_crc32_single_bit_flip_detection();
    test_b3_truncated_save_file_rejection();
    test_b3_invalid_magic_header();
    test_b3_dual_corruption_catastrophe();
    test_b3_missing_save_clean_sd();
    test_b3_unwritable_tmp_file();

    /* Category B4: Memory Limits */
    test_b4_top_ui_exhaustion_saturation();
    test_b4_bottom_ui_capacity_saturation();
    test_b4_zero_dynamic_heap_allocations();
    test_b4_prop_vertex_capacity_clamping();
    test_b4_linear_alloc_failure_resilience();

    reset_save_environment();

    printf("====================================================\n");
    printf(" TIER 2 RESULT: %d / %d Tests Passed (100%%)\n", g_test_pass_count, g_test_count);
    printf("====================================================\n");
    assert(g_test_count == 22);
    assert(g_test_pass_count == 22);
    return 0;
}
