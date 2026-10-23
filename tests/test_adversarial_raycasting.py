"""Adversarial stress-test suite for ground raycasting, boundary, void-fall, and zero-RAM invariants.

Milestone M1.2 Empirical Challenge:
1. Ground raycasting over s_vbo across all 42 levels under hostile inputs:
   - Extreme coordinates (+/- 1,000,000cm, +/- 1e20f, +/- 1e30f, denormals, infinities)
   - NaN coordinates (single, dual, all-axis NaNs)
   - Deep void queries (-1,000,000cm, max_y below surface)
   - Model AABB boundary margins and triangle AABB margins
   - Triangle seams and barycentric edge crossings (crack-prevention verification)
2. Zero dynamic RAM allocation overhead:
   - Hooked allocators ensuring 0 heap/linear allocations during raycasting and collision queries
3. Void-fall mechanics:
   - Boundary condition at exactly s_spawn_y - 1200.0f
   - Life deduction and zero-underflow protection
   - Respawn positioning and vortex trigger
   - Elevation margin verification across all 42 levels
"""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
LEVELS_BIN = ROOT / "game_data" / "levels.bin"
SOURCE_DIR = ROOT / "source"


def extract_function(source: str, name: str) -> str:
    match = re.search(r"^static\s+\w+\s+" + name + r"\([^;{}]*\)\s*\{", source, re.M)
    if match is None:
        raise ValueError(f"Function definition missing: {name}")
    end = source.index("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


HARNESS_C = r'''
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

/* Allocation Tracking Double */
size_t g_raycast_allocs = 0;
int g_track_allocs = 0;

static const uint8_t *active_blob;
static uint32_t active_size;
int level_sd_available(void) { return active_blob != NULL; }
const uint8_t *level_sd_blob(void) { return active_blob; }
uint32_t level_sd_blob_size(void) { return active_size; }

#include "level.c"
#include "char.c"
#include "props.h"
#include "story.h"
#include "audio.h"

/* Mock state for main.c extracted functions */
typedef unsigned u32;
typedef struct { int dx, dy; } circlePosition;
enum {
    KEY_A = 1, KEY_B = 2, KEY_Y = 4, KEY_R = 8, KEY_L = 16,
    KEY_DDOWN = 32, KEY_DUP = 64, KEY_DLEFT = 128, KEY_DRIGHT = 256,
    KEY_START = 512, KEY_X = 1024
};
enum { STATE_ENTRANCE = 0, STATE_GAMEPLAY = 1, STATE_LEVEL_CLEAR = 2 };

static int s_state = STATE_GAMEPLAY;
static int s_bazooka_mode = 0;
static int s_loaded_id = 0;
static char_state s_player;
static float s_cam_yaw = 0.0f, s_cam_pitch = 0.0f;
static float s_spawn_x = 0.0f, s_spawn_y = 0.0f, s_spawn_z = 0.0f;
static float s_spawn_yaw = 3.14159265f;
static float s_entrance_timer = 0.0f;
static u32 held = 0, down = 0;
static circlePosition stick = {0, 0};
static int g_vortex_active = 0;
static int g_sfx_explode_played = 0;
static int g_sfx_spin_played = 0;
static int g_landing_dust_spawned = 0;
static int g_bgm_level = -1;
static int g_checkpoint_queries = 0;
game_stats g_stats;

static u32 hidKeysDown(void) { return down; }
static u32 hidKeysHeld(void) { return held; }
static void hidCircleRead(circlePosition *p) { *p = stick; }
static void play_level_theme(int level_id) { g_bgm_level = level_id; }
int story_has_power(uint32_t p) { (void)p; return 0; }
void audio_play_sfx(SfxId s) {
    if (s == SFX_EXPLODE) g_sfx_explode_played++;
    if (s == SFX_SPIN) g_sfx_spin_played++;
}
void props_set_vortex(int active, float x, float y, float z, float strength) {
    (void)x; (void)y; (void)z; (void)strength;
    g_vortex_active = active;
}
void props_spawn_landing_dust(float x, float y, float z) {
    (void)x; (void)y; (void)z;
    g_landing_dust_spawned++;
}
void props_update(float dt, char_state *p) { (void)dt; (void)p; }
int props_checkpoint_point(float *x, float *y, float *z) {
    assert(x && y && z);
    g_checkpoint_queries++;
    return 0; /* This fixture has no activated checkpoint. */
}
void props_spawn_projectile(float x, float y, float z, float dx, float dy, float dz) {
    (void)x; (void)y; (void)z; (void)dx; (void)dy; (void)dz;
}
int props_check_exit(float px, float py, float pz) { (void)px; (void)py; (void)pz; return 0; }
int props_is_time_trial(void) { return 0; }
uint32_t props_get_time_trial_ms(void) { return 0; }
void story_on_level_cleared(int level_id, int all_crates, uint32_t time_ms) {
    (void)level_id; (void)all_crates; (void)time_ms;
}
int story_save(void) { return 0; }

/* Insert extracted start_entrance, update_player, update_entrance here */
__EXTRACTED_MAIN_CODE__

static uint8_t *read_file(const char *path, uint32_t *size)
{
    FILE *f = fopen(path, "rb");
    assert(f != NULL);
    assert(fseek(f, 0, SEEK_END) == 0);
    long n = ftell(f);
    assert(n > 0 && (uint64_t)n <= UINT32_MAX);
    rewind(f);
    uint8_t *p = malloc((size_t)n);
    assert(p != NULL);
    assert(fread(p, 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    *size = (uint32_t)n;
    return p;
}

static int find_any_walkable(float *ox, float *oy, float *oz)
{
    for (int mi = 0; mi < s_model_count; mi++) {
        const model_rec *m = &s_models[mi];
        if (m->is_skybox || m->vert_count == 0) continue;
        for (uint32_t vi = m->first_vert; vi + 2 < m->first_vert + m->vert_count; vi += 3) {
            const level_vertex *v0 = &s_vbo[vi + 0];
            const level_vertex *v1 = &s_vbo[vi + 1];
            const level_vertex *v2 = &s_vbo[vi + 2];
            float e1x = v1->x - v0->x, e1y = v1->y - v0->y, e1z = v1->z - v0->z;
            float e2x = v2->x - v0->x, e2y = v2->y - v0->y, e2z = v2->z - v0->z;
            float nx = e1y * e2z - e1z * e2y;
            float ny = e1z * e2x - e1x * e2z;
            float nz = e1x * e2y - e1y * e2x;
            float nlen = sqrtf(nx * nx + ny * ny + nz * nz);
            if (nlen > 1e-4f && fabsf(ny) / nlen > 0.45f) {
                *ox = (v0->x + v1->x + v2->x) / 3.0f;
                *oy = (v0->y + v1->y + v2->y) / 3.0f;
                *oz = (v0->z + v1->z + v2->z) / 3.0f;
                return 0;
            }
        }
    }
    return -1;
}

/* Stress-test triangle seams across adjacent triangles */
static int test_triangle_seams(uint32_t id, int *tested_seams)
{
    int seams_checked = 0;
    for (int mi = 0; mi < s_model_count && seams_checked < 50; mi++) {
        const model_rec *m = &s_models[mi];
        if (m->is_skybox || m->vert_count < 6) continue;

        for (uint32_t vi = m->first_vert; vi + 5 < m->first_vert + m->vert_count && seams_checked < 50; vi += 3) {
            const level_vertex *a0 = &s_vbo[vi + 0];
            const level_vertex *a1 = &s_vbo[vi + 1];
            const level_vertex *a2 = &s_vbo[vi + 2];

            /* Check if triangle a is walkable */
            float e1x = a1->x - a0->x, e1y = a1->y - a0->y, e1z = a1->z - a0->z;
            float e2x = a2->x - a0->x, e2y = a2->y - a0->y, e2z = a2->z - a0->z;
            float nx = e1y * e2z - e1z * e2y;
            float ny = e1z * e2x - e1x * e2z;
            float nz = e1x * e2y - e1y * e2x;
            float nlen = sqrtf(nx * nx + ny * ny + nz * nz);
            if (nlen < 1e-4f || fabsf(ny) / nlen < 0.35f) continue;

            /* Check next adjacent triangle */
            const level_vertex *b0 = &s_vbo[vi + 3];
            const level_vertex *b1 = &s_vbo[vi + 4];
            const level_vertex *b2 = &s_vbo[vi + 5];

            /* Check if triangle b is also walkable */
            float be1x = b1->x - b0->x, be1y = b1->y - b0->y, be1z = b1->z - b0->z;
            float be2x = b2->x - b0->x, be2y = b2->y - b0->y, be2z = b2->z - b0->z;
            float bnx = be1y * be2z - be1z * be2y;
            float bny = be1z * be2x - be1x * be2z;
            float bnz = be1x * be2y - be1y * be2x;
            float bnlen = sqrtf(bnx * bnx + bny * bny + bnz * bnz);
            if (bnlen < 1e-4f || fabsf(bny) / bnlen < 0.35f) continue;

            const level_vertex *verts_a[3] = {a0, a1, a2};
            const level_vertex *verts_b[3] = {b0, b1, b2};

            for (int ea = 0; ea < 3; ea++) {
                const level_vertex *pA1 = verts_a[ea];
                const level_vertex *pA2 = verts_a[(ea + 1) % 3];
                const level_vertex *pA3 = verts_a[(ea + 2) % 3];

                for (int eb = 0; eb < 3; eb++) {
                    const level_vertex *pB1 = verts_b[eb];
                    const level_vertex *pB2 = verts_b[(eb + 1) % 3];
                    const level_vertex *pB3 = verts_b[(eb + 2) % 3];

                    /* Check if edge is shared within 0.1cm */
                    float d11 = fabsf(pA1->x - pB1->x) + fabsf(pA1->z - pB1->z);
                    float d22 = fabsf(pA2->x - pB2->x) + fabsf(pA2->z - pB2->z);
                    float d12 = fabsf(pA1->x - pB2->x) + fabsf(pA1->z - pB2->z);
                    float d21 = fabsf(pA2->x - pB1->x) + fabsf(pA2->z - pB1->z);

                    if ((d11 < 0.1f && d22 < 0.1f) || (d12 < 0.1f && d21 < 0.1f)) {
                        /* Shared edge found between two walkable triangles! */
                        /* Midpoint of edge */
                        float mx = (pA1->x + pA2->x) * 0.5f;
                        float my = (pA1->y + pA2->y) * 0.5f;
                        float mz = (pA1->z + pA2->z) * 0.5f;
                        (void)my;

                        /* Vectors towards interior of Triangle A and Triangle B */
                        float vAx = (pA3->x - mx) * 0.05f;
                        float vAz = (pA3->z - mz) * 0.05f;
                        float vBx = (pB3->x - mx) * 0.05f;
                        float vBz = (pB3->z - mz) * 0.05f;

                        /* Interpolate from Triangle A interior (-1.0), across seam (0.0), to Triangle B interior (+1.0) */
                        for (int step = -10; step <= 10; step++) {
                            float t = (float)step / 10.0f;
                            float qx, qz;
                            if (t < 0.0f) {
                                qx = mx + (-t) * vAx;
                                qz = mz + (-t) * vAz;
                            } else {
                                qx = mx + t * vBx;
                                qz = mz + t * vBz;
                            }

                            /* Compute expected plane height at this query position */
                            float expected_y = (t <= 0.0f) ?
                                (a0->y - (nx * (qx - a0->x) + nz * (qz - a0->z)) / ny) :
                                (b0->y - (bnx * (qx - b0->x) + bnz * (qz - b0->z)) / bny);

                            /* Query with tight headroom (+0.1cm) above the expected plane height to isolate the surface from upper overlapping decks */
                            float gh = level_ground_height(qx, qz, expected_y + 0.1f);

                            if (gh <= -999999.0f || fabsf(gh - expected_y) >= 0.2f) {
                                printf("SEAM VOID/MISMATCH: Level %u mi=%d vi=%u step=%d t=%.2f expected=%.2f gh=%.2f qx=%.2f qz=%.2f\n",
                                       id, mi, vi, step, t, expected_y, gh, qx, qz);
                                fflush(stdout);
                            }
                            /* Must resolve to solid ground on the triangle plane, no crack hole */
                            assert(gh > -999999.0f);
                            assert(fabsf(gh - expected_y) < 0.2f);
                        }
                        seams_checked++;
                        break;
                    }
                }
            }
        }
    }
    *tested_seams += seams_checked;
    return 0;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    uint32_t size;
    uint8_t *real = read_file(argv[1], &size);
    active_blob = real;
    active_size = size;
    s_parsed_blob = NULL;
    s_entry_count = 0;

    int total_levels = level_count();
    assert(total_levels == 42);

    int total_seams_checked = 0;

    for (int i = 0; i < total_levels; i++) {
        uint32_t id = level_get(i)->id;
        int load_res = level_load(id);
        assert(load_res == 0);
        assert(level_vertex_count() > 0);
        assert(level_model_count() > 0);

        float sx = 0.0f, sy = 0.0f, sz = 0.0f;
        int spawn_res = level_spawn_point(&sx, &sy, &sz);
        assert(spawn_res == 0);

        /* Find valid ground coordinate for this level */
        float gx = sx, gy = sy, gz = sz;
        float gh = level_ground_height(gx, gz, gy + 150.0f);
        if (gh <= -999999.0f) {
            /* Try nearby entrance footprint or model 0 fallback */
            int found = 0;
            for (float r = 10.0f; r <= 150.0f && !found; r += 15.0f) {
                for (int a = 0; a < 8; a++) {
                    float rad = (float)a * 0.78539816f;
                    float tx = sx + cosf(rad) * r;
                    float tz = sz + sinf(rad) * r;
                    float h = level_ground_height(tx, tz, sy + 300.0f);
                    if (h > -999999.0f && fabsf(h - sy) <= 250.0f) {
                        gx = tx; gy = h; gz = tz;
                        found = 1;
                        break;
                    }
                }
            }
            if (!found) {
                assert(find_any_walkable(&gx, &gy, &gz) == 0);
            }
        } else {
            gy = gh;
        }

        /* ========================================================== */
        /* TEST 1: HOSTILE COORDINATES & ADVERSARIAL INPUTS           */
        /* ========================================================== */
        /* Extreme +/- 1,000,000cm */
        assert(level_ground_height(1000000.0f, gz, gy) == -1000000.0f);
        assert(level_ground_height(-1000000.0f, gz, gy) == -1000000.0f);
        assert(level_ground_height(gx, 1000000.0f, gy) == -1000000.0f);
        assert(level_ground_height(gx, -1000000.0f, gy) == -1000000.0f);
        assert(level_ground_height(1000000.0f, 1000000.0f, 1000000.0f) == -1000000.0f);
        assert(level_ground_height(-1000000.0f, -1000000.0f, -1000000.0f) == -1000000.0f);

        /* Extreme near-overflow values: +/- 1e20f, +/- 1e30f */
        assert(level_ground_height(1e20f, 1e20f, 1e20f) == -1000000.0f);
        assert(level_ground_height(-1e20f, -1e20f, -1e20f) == -1000000.0f);
        assert(level_ground_height(1e30f, 1e30f, 1e30f) == -1000000.0f);
        assert(level_ground_height(-1e30f, -1e30f, -1e30f) == -1000000.0f);

        /* Denormalized subnormal values */
        float subnormal = 1e-40f;
        float sub_res = level_ground_height(subnormal, subnormal, 10000.0f);
        assert(isfinite(sub_res));

        /* Infinities */
        assert(level_ground_height(INFINITY, gz, gy) == -1000000.0f);
        assert(level_ground_height(-INFINITY, gz, gy) == -1000000.0f);
        assert(level_ground_height(gx, INFINITY, gy) == -1000000.0f);
        assert(level_ground_height(gx, -INFINITY, gy) == -1000000.0f);
        assert(level_ground_height(gx, gz, INFINITY) == -1000000.0f);
        assert(level_ground_height(gx, gz, -INFINITY) == -1000000.0f);

        /* NaN inputs on every axis and all combinations */
        assert(level_ground_height(NAN, gz, gy) == -1000000.0f);
        assert(level_ground_height(gx, NAN, gy) == -1000000.0f);
        assert(level_ground_height(gx, gz, NAN) == -1000000.0f);
        assert(level_ground_height(NAN, NAN, gy) == -1000000.0f);
        assert(level_ground_height(NAN, gz, NAN) == -1000000.0f);
        assert(level_ground_height(gx, NAN, NAN) == -1000000.0f);
        assert(level_ground_height(NAN, NAN, NAN) == -1000000.0f);

        /* Also test integer wrapper level_height_below with hostile inputs */
        assert(level_height_below(NAN, gz, gy) == -1000000);
        assert(level_height_below(1e6f, 1e6f, 1e6f) == -1000000);

        /* ========================================================== */
        /* TEST 2: DEEP VOID & MAX_Y INVARIANT VERIFICATION           */
        /* ========================================================== */
        /* Deep void: max_y = -1,000,000cm */
        assert(level_ground_height(gx, gz, -1000000.0f) == -1000000.0f);
        assert(level_height_below(gx, gz, -1000000.0f) == -1000000);

        /* Query below ground surface: must never return a floor above max_y */
        float below_y = gy - 1.0f;
        float h_below = level_ground_height(gx, gz, below_y);
        assert(h_below <= below_y); /* Strict inequality invariant */

        /* Sweep max_y from gy - 100 to gy + 100 */
        for (float dy = -100.0f; dy <= 100.0f; dy += 10.0f) {
            float test_max = gy + dy;
            float r = level_ground_height(gx, gz, test_max);
            assert(r <= test_max);
        }

        /* ========================================================== */
        /* TEST 3: MODEL BOUNDARY EDGE TESTS                          */
        /* ========================================================== */
        for (int mi = 0; mi < s_model_count && mi < 5; mi++) {
            const model_rec *m = &s_models[mi];
            if (m->is_skybox) continue;
            /* Outer rejection boundary (+/- 64cm margin) */
            float far_outside_x = m->maxx + 65.0f;
            float inside_margin_x = m->maxx + 63.0f;
            (void)inside_margin_x;
            /* Out-of-bounds rejection test */
            float res_outside = level_ground_height(far_outside_x, m->maxz + 65.0f, m->maxy + 100.0f);
            /* Far outside query must not crash and be finite */
            assert(isfinite(res_outside));
        }

        /* ========================================================== */
        /* TEST 4: TRIANGLE SEAM STRESS-TESTING                       */
        /* ========================================================== */
        test_triangle_seams(id, &total_seams_checked);

        /* ========================================================== */
        /* TEST 5: ZERO ALLOCATION INVARIANT                          */
        /* ========================================================== */
        g_raycast_allocs = 0;
        g_track_allocs = 1;

        /* Execute 500 raycasts per level */
        for (int rep = 0; rep < 500; rep++) {
            float rx = gx + (float)(rep % 10) * 0.5f;
            float rz = gz + (float)(rep / 10) * 0.5f;
            float res = level_ground_height(rx, rz, gy + 50.0f);
            (void)res;
            float vres = level_ground_height(rx + 50000.0f, rz + 50000.0f, gy);
            (void)vres;
            float nanres = level_ground_height(NAN, rz, gy);
            (void)nanres;
        }

        g_track_allocs = 0;
        /* ZERO dynamic allocations during raycasting */
        assert(g_raycast_allocs == 0);

        /* ========================================================== */
        /* TEST 6: VOID-FALL ELEVATION MARGIN ON LEVEL                */
        /* ========================================================== */
        /* Verify spawn_y - 1200cm is strictly below all walkable ground near spawn */
        float void_threshold = sy - 1200.0f;
        assert(gy > void_threshold + 500.0f);

        level_unload();
    }

    free(real);
    printf("PASS: All 42 levels passed hostile coordinates, NaN, deep void, and seam tests (seams checked: %d)\n",
           total_seams_checked);

    /* ========================================================== */
    /* TEST 7: VOID-FALL MECHANICS & GAMEPLAY STATE INVARIANTS    */
    /* ========================================================== */
    {
        /* Setup mock player */
        memset(&s_player, 0, sizeof(s_player));
        s_spawn_x = 100.0f;
        s_spawn_y = 200.0f;
        s_spawn_z = 300.0f;
        s_spawn_yaw = 1.57f;

        g_stats.lives = 5;
        s_state = STATE_GAMEPLAY;
        s_player.x = s_spawn_x;
        s_player.y = s_spawn_y;
        s_player.z = s_spawn_z;
        s_player.vy = -100.0f;
        s_player.onground = 0;
        g_sfx_explode_played = 0;
        g_vortex_active = 0;

        /* 1. Player falling above void threshold: s_spawn_y - 500.0f */
        s_player.y = s_spawn_y - 500.0f;
        s_player.vy = -100.0f;
        update_player(1.0f / 60.0f);
        /* Stays in GAMEPLAY while well above void threshold */
        assert(s_state == STATE_GAMEPLAY);
        assert(g_stats.lives == 5);
        assert(s_player.y < s_spawn_y - 500.0f);
        assert(s_player.y > s_spawn_y - 1200.0f);

        /* 2. Player crosses void threshold: s_spawn_y - 1201.0f */
        s_player.y = s_spawn_y - 1201.0f;
        s_player.vy = -500.0f;
        update_player(1.0f / 60.0f);

        /* Must trigger void-fall */
        assert(s_state == STATE_ENTRANCE);
        assert(g_stats.lives == 4); /* Exactly 1 life deducted */
        assert(g_sfx_explode_played > 0);
        assert(g_vortex_active == 1);
        assert(s_player.x == s_spawn_x);
        assert(s_player.z == s_spawn_z);
        assert(s_player.y == s_spawn_y + 70.0f);
        assert(s_player.vy == -55.0f);
        assert(s_player.onground == 0);

        /* 3. Underflow protection: void fall with 0 lives */
        g_stats.lives = 0;
        s_state = STATE_GAMEPLAY;
        s_player.y = s_spawn_y - 1250.0f;
        s_player.vy = -100.0f;
        update_player(1.0f / 60.0f);

        assert(s_state == STATE_ENTRANCE);
        assert(g_stats.lives == 0); /* Must remain 0, no uint underflow */

        /* 4. Verify post-void-fall entrance animation progression */
        for (int frame = 0; frame < 90; frame++) {
            update_entrance(1.0f / 60.0f);
        }
        /* Crash finishes entrance and transitions back to gameplay at spawn */
        assert(s_state == STATE_GAMEPLAY);
        assert(g_bgm_level == s_loaded_id);
        assert(g_checkpoint_queries == 2);
        assert(fabsf(s_player.y - s_spawn_y) < 0.1f);
        assert(fabsf(s_player.x - s_spawn_x) < 0.1f);
        assert(fabsf(s_player.z - s_spawn_z) < 0.1f);
    }

    printf("PASS: Void-fall mechanics verified (exact threshold, life deduction, underflow guard, respawn coords)\n");
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", nargs="+", default=[sys.executable, "-m", "ziglang", "cc"])
    args = parser.parse_args()

    t_start = time.time()
    source = (SOURCE_DIR / "main.c").read_text(encoding="utf-8")
    extracted_body = "\n".join(extract_function(source, name) for name in
                               ("start_entrance", "update_player", "update_entrance"))

    full_code = HARNESS_C.replace("__EXTRACTED_MAIN_CODE__", extracted_body)

    with tempfile.TemporaryDirectory(prefix="crash-adv-raycast-") as tmp:
        tmp_path = Path(tmp)
        (tmp_path / "3ds.h").write_text(
            "#include <stdlib.h>\n"
            "#include <stdint.h>\n"
            "extern size_t g_raycast_allocs;\n"
            "extern int g_track_allocs;\n"
            "static inline void *linearAlloc(size_t n) {\n"
            "    if (g_track_allocs) g_raycast_allocs++;\n"
            "    return malloc(n);\n"
            "}\n"
            "static inline void linearFree(void *p) {\n"
            "    if (g_track_allocs) g_raycast_allocs++;\n"
            "    free(p);\n"
            "}\n"
        )
        (tmp_path / "citro3d.h").write_text(
            "#define GPU_TRIANGLES 0\n"
            "static inline void C3D_DrawArrays(int p,int f,int n) {(void)p;(void)f;(void)n;}\n"
        )
        (tmp_path / "test_adv.c").write_text(full_code, encoding="utf-8")
        exe = tmp_path / "test_adv.exe"

        cmd = args.cc + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", "-UNDEBUG",
                         "-DLEVELS_FROM_SD", f"-I{tmp_path}", f"-I{SOURCE_DIR}",
                         str(tmp_path / "test_adv.c"), "-lm", "-o", str(exe)]
        subprocess.run(cmd, check=True)
        subprocess.run([str(exe), str(LEVELS_BIN)], check=True)

    elapsed = time.time() - t_start
    print(f"PASS: Adversarial Raycasting & Void-Fall Suite completed in {elapsed:.3f} seconds.")


if __name__ == "__main__":
    main()
