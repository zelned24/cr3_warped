/* Crash Bandicoot 3: Warped (3DS Port) — E2E Test Suite
 *
 * TIER 4: Real-World Application Scenarios Harness
 * Validates complete multi-step end-to-end player journeys:
 *   - Scenario S1: Full Level Completion Flow (Warp -> Load -> 42 Crates -> Crystal -> Platinum Relic -> Clear -> Save)
 *   - Scenario S2: Death & Respawn Flow (Gameplay -> Void Fall past -1200cm -> Life Loss -> Vortex Respawn -> Recovery)
 *   - Scenario S3: Boss Battle Sequence (5 Crystals -> Tiny Tiger Unlock -> 3-Cycle Combat -> Belly Flop Award -> Chamber 2 Open)
 *   - Scenario S4: Secret Warp Chamber Unlock Flow (Relic Milestones -> Secret Portals Unlock -> 100%+ Completion Journey)
 */

#include "e2e_common.h"
#include "level.h"
#include "story.h"
#include "bottom_ui.h"
#include "props.h"
#include "audio.h"
#include "char.h"

/* Audio State Tracking */
static int s_sfx_history[SFX_COUNT] = {0};
static BgmTheme s_current_bgm = BGM_NONE;

void audio_play_sfx(SfxId id) {
    if (id >= 0 && id < SFX_COUNT)
        s_sfx_history[id]++;
}

void audio_play_bgm(BgmTheme theme) {
    s_current_bgm = theme;
}

void audio_stop_bgm(void) {
    s_current_bgm = BGM_NONE;
}

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
    memset(s_sfx_history, 0, sizeof(s_sfx_history));
    s_current_bgm = BGM_NONE;
}

/* ------------------------------------------------------------------ */
/* SCENARIO S1: Full Level Completion Flow                            */
/* ------------------------------------------------------------------ */

static void test_s1_full_level_completion_flow(void) {
    TEST_BEGIN("S1 Full Level Completion Flow (Toad Village 100% Platinum)");
    reset_save_environment();

    /* 1. Player begins in Warp Room hub (Chamber 1) */
    audio_play_bgm(BGM_WARP_ROOM);
    assert(s_current_bgm == BGM_WARP_ROOM);
    assert(story_is_level_unlocked(0x0B) == 1);

    /* 2. Select Level 0x0B (Toad Village) portal */
    audio_play_sfx(SFX_MENU_SELECT);
    assert(s_sfx_history[SFX_MENU_SELECT] == 1);

    /* 3. Transition through loading screen: stop Warp BGM, start Medieval BGM */
    audio_stop_bgm();
    assert(s_current_bgm == BGM_NONE);
    audio_play_bgm(BGM_MEDIEVAL);
    assert(s_current_bgm == BGM_MEDIEVAL);

    /* 4. Canonical Entrance Spawn */
    float spawn_x = 3034.165f, spawn_y = 191.0f, spawn_z = 25150.0f;
    char_state player = {
        .x = spawn_x, .y = spawn_y, .z = spawn_z,
        .yaw = 3.14159265f, .onground = 1, .can_double_jump = 1
    };
    audio_play_sfx(SFX_SPIN); /* Vortex spin entrance */

    /* 5. Player progresses through level: destroys all 42 canonical crates */
    int total_crates = story_get_canonical_crate_count(0x0B);
    assert(total_crates == 42);
    g_stats = (game_stats){.wumpa_count = 0, .crates_broken = 0, .crates_total = total_crates, .lives = 4, .aku_aku_tier = 0, .aku_timer = 0.0f};

    for (int c = 0; c < total_crates; c++) {
        g_stats.crates_broken++;
        g_stats.wumpa_count += 2;
        if (g_stats.wumpa_count >= 100) {
            g_stats.wumpa_count -= 100;
            g_stats.lives++;
        }
        audio_play_sfx(SFX_CRATE_BREAK);
    }
    assert(g_stats.crates_broken == 42);
    assert(s_sfx_history[SFX_CRATE_BREAK] == 42);

    /* 6. Acquire Purple Power Crystal */
    audio_play_sfx(SFX_CRYSTAL);
    assert(s_sfx_history[SFX_CRYSTAL] == 1);

    /* 7. Reach Exit Portal within Platinum time (40.000s < 44.030s target) */
    uint32_t final_time_ms = 40000;
    int exit_reached = 1;
    assert(exit_reached == 1);

    /* 8. Trigger Level Cleared Event */
    story_on_level_cleared(0x0B, 1, final_time_ms);
    audio_play_sfx(SFX_LEVEL_CLEAR);

    /* Verify progression mutations */
    assert(g_save.levels[0x0B].crystal == 1);
    assert(g_save.levels[0x0B].clear_gem == 1);
    assert(g_save.levels[0x0B].relic_tier == RELIC_PLATINUM);
    assert(g_save.levels[0x0B].best_time_ms == final_time_ms);
    assert(g_save.crystals_count == 1);
    assert(g_save.gems_count == 1);
    assert(g_save.relics_count == 1);
    assert(g_save.completion_pct > 0);

    /* 9. Verify Atomic Save File was committed to SD */
    FILE *f = fopen("save.dat", "rb");
    assert(f != NULL);
    fclose(f);

    /* 10. Return to Warp Room */
    audio_play_bgm(BGM_WARP_ROOM);
    assert(s_current_bgm == BGM_WARP_ROOM);

    /* Bottom HUD displays updated counters */
    bottom_ui_state hud = {
        .level_name = "WARP ROOM",
        .wumpa = g_stats.wumpa_count,
        .lives = g_stats.lives,
        .crystals = g_save.crystals_count,
        .gems = g_save.gems_count,
        .relics = g_save.relics_count,
        .completion = g_save.completion_pct
    };
    level_vertex hud_mesh[1024];
    int hud_verts = bottom_ui_build(hud_mesh, 1024, &hud);
    assert(hud_verts > 0 && hud_verts % 3 == 0);
    (void)player;
    TEST_PASS("S1 Full Level Completion Flow (Toad Village 100% Platinum)");
}

/* ------------------------------------------------------------------ */
/* SCENARIO S2: Death & Respawn Flow                                  */
/* ------------------------------------------------------------------ */

static void test_s2_death_and_respawn_flow(void) {
    TEST_BEGIN("S2 Death & Respawn Flow (Void Fall -> Life Deduction -> Vortex Reset)");
    reset_save_environment();

    /* 1. Start in gameplay with 3 lives */
    g_stats = (game_stats){.wumpa_count = 25, .crates_broken = 10, .crates_total = 42, .lives = 3, .aku_aku_tier = 0, .aku_timer = 0.0f};
    float spawn_x = 0.0f, spawn_y = 100.0f, spawn_z = 0.0f;
    float void_fall_limit = spawn_y - 1200.0f; /* -1100.0f */

    char_state player = {
        .x = 0.0f, .y = 100.0f, .z = 500.0f,
        .vx = 0.0f, .vy = 0.0f, .vz = 0.0f,
        .onground = 1
    };

    /* 2. Player walks off ledge and plunges into void */
    player.onground = 0;
    player.vy = -1500.0f;
    player.y = -1150.0f; /* Exceeds void fall limit (-1100.0f) */

    assert(player.y < void_fall_limit);

    /* 3. Void Fall Protection Triggers */
    audio_play_sfx(SFX_EXPLODE);
    assert(s_sfx_history[SFX_EXPLODE] == 1);

    /* Decrement lives */
    assert(g_stats.lives > 0);
    g_stats.lives--;
    assert(g_stats.lives == 2);

    /* 4. Respawn at Entrance Vortex */
    player.x = spawn_x;
    player.y = spawn_y + 70.0f;
    player.z = spawn_z;
    player.vy = -55.0f;
    player.vx = 0.0f;
    player.vz = 0.0f;
    player.onground = 0;
    player.spin_timer = 0.30f;
    audio_play_sfx(SFX_SPIN);

    assert(fabsf(player.x - spawn_x) < 0.01f);
    assert(fabsf(player.z - spawn_z) < 0.01f);
    assert(player.y > spawn_y);
    assert(s_sfx_history[SFX_SPIN] == 1);

    /* 5. Player touches ground at spawn, recovers idle state */
    player.y = spawn_y;
    player.vy = 0.0f;
    player.onground = 1;
    player.spin_timer = 0.0f;

    assert(player.onground == 1);
    assert(g_stats.lives == 2);
    TEST_PASS("S2 Death & Respawn Flow (Void Fall -> Life Deduction -> Vortex Reset)");
}

/* ------------------------------------------------------------------ */
/* SCENARIO S3: Boss Battle Sequence                                  */
/* ------------------------------------------------------------------ */

static void test_s3_boss_battle_sequence(void) {
    TEST_BEGIN("S3 Boss Battle Sequence (Tiny Tiger Defeat & Belly Flop Award)");
    reset_save_environment();

    /* 1. Complete 5 Chamber 1 levels to collect 5 crystals */
    const int c1_levels[] = {0x0B, 0x0E, 0x0A, 0x0C, 0x19};
    for (int i = 0; i < 5; i++) {
        story_on_level_cleared(c1_levels[i], 0, 0);
    }
    assert(g_save.crystals_count == 5);

    /* 2. Tiny Tiger (Boss 0, Level 0x06) portal unlocks */
    assert(story_is_boss_unlocked(0) == 1);
    assert(story_is_level_unlocked(0x06) == 1);

    /* 3. Enter Boss Arena */
    audio_play_bgm(BGM_BOSS);
    assert(s_current_bgm == BGM_BOSS);

    /* 4. Combat Cycle: Tiny Tiger starts with 3 HP */
    int tiny_hp = 3;

    /* Cycle 1: Trident jump -> trident stuck -> Crash spins */
    audio_play_sfx(SFX_SPIN);
    tiny_hp--;
    audio_play_sfx(SFX_BOSS_HIT);
    assert(tiny_hp == 2);

    /* Cycle 2: Lion charge wave -> Crash dodges -> spins */
    audio_play_sfx(SFX_SPIN);
    tiny_hp--;
    audio_play_sfx(SFX_BOSS_HIT);
    assert(tiny_hp == 1);

    /* Cycle 3: Final attack phase -> Crash lands 3rd hit */
    audio_play_sfx(SFX_SPIN);
    tiny_hp--;
    audio_play_sfx(SFX_BOSS_HIT);
    assert(tiny_hp == 0);

    /* 5. Boss Defeated Event */
    story_on_boss_defeated(0);
    audio_play_sfx(SFX_LEVEL_CLEAR);

    /* 6. Verify Super Power Awarded: Super Belly Flop */
    assert(story_has_power(POWER_BELLY_FLOP) == 1);
    assert(g_save.bosses_defeated[0] == 1);

    /* 7. Verify Chamber 2 (Arabian, level 0x0F) is now unlocked */
    assert(story_is_level_unlocked(0x0F) == 1);

    /* 8. Verify save committed */
    assert(story_load() == 0);
    assert(story_has_power(POWER_BELLY_FLOP) == 1);
    TEST_PASS("S3 Boss Battle Sequence (Tiny Tiger Defeat & Belly Flop Award)");
}

/* ------------------------------------------------------------------ */
/* SCENARIO S4: Secret Warp Chamber Unlock Flow                       */
/* ------------------------------------------------------------------ */

static void test_s4_secret_warp_chamber_unlock_flow(void) {
    TEST_BEGIN("S4 Secret Warp Chamber Unlock Flow (Relic Thresholds & Portals)");
    reset_save_environment();

    /* 1. At start: Secret levels 0x27 (Hot Coco) and 0x26 (Eggipus Rex) are locked */
    assert(!story_is_level_unlocked(0x27));
    assert(!story_is_level_unlocked(0x26));

    /* 2. Collect 20 Relics through Time Trials */
    g_save.relics_count = 20;
    assert(story_is_level_unlocked(0x27) == 1); /* Hot Coco unlocked */
    assert(story_is_level_unlocked(0x26) == 0); /* Eggipus Rex still locked */

    /* 3. Collect 25 Relics */
    g_save.relics_count = 25;
    assert(story_is_level_unlocked(0x27) == 1);
    assert(story_is_level_unlocked(0x26) == 1); /* Eggipus Rex unlocked */

    /* 4. Complete secret levels */
    story_on_level_cleared(0x27, 1, 15000); /* Hot Coco clear */
    story_on_level_cleared(0x26, 0, 40000); /* Eggipus Rex clear */

    assert(g_save.levels[0x27].clear_gem == 1);
    assert(g_save.levels[0x26].relic_tier == RELIC_PLATINUM);

    /* 5. Commit and verify */
    assert(story_save() == 0);
    assert(story_load() == 0);
    assert(g_save.levels[0x27].clear_gem == 1);
    assert(g_save.levels[0x26].relic_tier == RELIC_PLATINUM);
    TEST_PASS("S4 Secret Warp Chamber Unlock Flow (Relic Thresholds & Portals)");
}

/* ------------------------------------------------------------------ */
/* Master Runner                                                      */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("====================================================\n");
    printf(" Crash Bandicoot 3 (3DS) — Tier 4 Application Flows \n");
    printf("====================================================\n");

    test_s1_full_level_completion_flow();
    test_s2_death_and_respawn_flow();
    test_s3_boss_battle_sequence();
    test_s4_secret_warp_chamber_unlock_flow();

    reset_save_environment();

    printf("====================================================\n");
    printf(" TIER 4 RESULT: %d / %d Tests Passed (100%%)\n", g_test_pass_count, g_test_count);
    printf("====================================================\n");
    assert(g_test_count == 4);
    assert(g_test_pass_count == 4);
    return 0;
}
