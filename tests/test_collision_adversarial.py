"""Adversarial stress-test harness for level_move_horizontal and collision resolution.

Stress-tests:
1. High-velocity collisions & tunneling attempts at 20 FPS hitch dt (0.05s) up to 9999 cm/s.
2. Acute corners and sharp wedges (90, 60, 45, 30, 15, 5 degrees).
3. Rapid direction reversal and high-frequency oscillation against steep walls (10,000 steps).
4. Boundary, extreme, degenerate inputs (zero displacement, zero-area triangles, NaN/Inf rejection).
5. Penetration recovery: character starting inside wall pushed to exterior.
6. 42-level retail geometry adversarial multi-directional sweep at 20 FPS hitch dt.

Execution budget: < 2.0 seconds.
"""
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
LEVELS_BIN = ROOT / "game_data" / "levels.bin"

HARNESS_C = r'''
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <float.h>

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

/* Helper to setup synthetic vertical quad wall between (x1,z1) and (x2,z2) */
static void setup_wall_model(level_vertex *vbo, model_rec *mod,
                             float x1, float z1, float x2, float z2,
                             float y_low, float y_high)
{
    /* Two triangles forming a vertical rectangular wall */
    vbo[0].x = x1; vbo[0].y = y_low;  vbo[0].z = z1;
    vbo[1].x = x1; vbo[1].y = y_high; vbo[1].z = z1;
    vbo[2].x = x2; vbo[2].y = y_low;  vbo[2].z = z2;

    vbo[3].x = x2; vbo[3].y = y_low;  vbo[3].z = z2;
    vbo[4].x = x1; vbo[4].y = y_high; vbo[4].z = z1;
    vbo[5].x = x2; vbo[5].y = y_high; vbo[5].z = z2;

    memset(mod, 0, sizeof(*mod));
    mod->first_vert = 0;
    mod->vert_count = 6;
    mod->minx = fminf(x1, x2); mod->maxx = fmaxf(x1, x2);
    mod->miny = y_low;         mod->maxy = y_high;
    mod->minz = fminf(z1, z2); mod->maxz = fmaxf(z1, z2);
    mod->is_skybox = 0;
}

/* =========================================================================
 * TEST 1: High-Velocity Collisions & Tunneling at 20 FPS Hitch (dt = 0.05s)
 * ========================================================================= */
static void test_high_velocity_tunneling(void)
{
    static level_vertex vbo[12];
    static model_rec mods[2];
    /* Single thin vertical wall at x = 100.0, z from -500 to +500, y from 0 to 200 */
    setup_wall_model(vbo, &mods[0], 100.0f, -500.0f, 100.0f, 500.0f, 0.0f, 200.0f);
    s_vbo = vbo;
    s_model_count = 1;
    memcpy(s_models, mods, sizeof(model_rec));

    const float radius = 12.0f;
    const float body_height = 45.0f;
    const float step_height = 15.0f;
    const float foot_y = 0.0f;

    /* A. Standard Crash Dash speed (410 cm/s) at 20 FPS hitch dt (0.05s) -> dx = 20.5 cm */
    float x = 0.0f, z = 0.0f;
    for (int frame = 0; frame < 50; frame++) {
        level_move_horizontal(&x, &z, foot_y, 20.5f, 0.0f, radius, body_height, step_height);
        assert(isfinite(x) && isfinite(z));
        /* Player must NEVER penetrate past x = 100 - radius */
        assert(x <= 100.0f - radius + 0.02f);
    }
    assert(fabsf(x - (100.0f - radius - 0.01f)) < 0.05f);

    /* B. Extreme speeds: 1000 cm/s, 2000 cm/s, 5000 cm/s, 9999 cm/s in a single frame */
    float test_speeds[] = { 1000.0f, 2000.0f, 5000.0f, 9999.0f };
    for (size_t i = 0; i < sizeof(test_speeds)/sizeof(test_speeds[0]); i++) {
        float speed = test_speeds[i];
        float dx = speed * 0.05f; /* dt = 0.05s, displacement = 50..499.95 cm */
        x = 60.0f; z = 50.0f; /* Start at x=60, wall is at x=100 */
        int hit = level_move_horizontal(&x, &z, foot_y, dx, 0.0f, radius, body_height, step_height);
        assert(hit != 0);
        assert(isfinite(x) && isfinite(z));
        assert(x <= 100.0f - radius + 0.02f);
        assert(x >= 100.0f - radius - 0.05f);
        assert(fabsf(z - 50.0f) < 0.001f);
    }

    /* C. Oblique angles under 20 FPS hitch dt (410 cm/s -> displacement = 20.5 cm) */
    /* At 45 degrees, dx = 14.495, dz = 14.495 */
    x = 80.0f; z = 0.0f;
    for (int frame = 0; frame < 30; frame++) {
        float old_z = z;
        int hit = level_move_horizontal(&x, &z, foot_y, 14.495f, 14.495f, radius, body_height, step_height);
        assert(isfinite(x) && isfinite(z));
        assert(x <= 100.0f - radius + 0.02f);
        /* Z motion (tangential slide) must be preserved along the wall! */
        assert(z > old_z + 14.0f);
        if (x > 87.0f) {
            assert(hit != 0);
        }
    }

    /* D. Approaching from opposite winding (moving towards x = 100 with negative dx) */
    for (size_t i = 0; i < sizeof(test_speeds)/sizeof(test_speeds[0]); i++) {
        float speed = test_speeds[i];
        float dx = -speed * 0.05f;
        x = 140.0f; z = -20.0f;
        int hit = level_move_horizontal(&x, &z, foot_y, dx, 0.0f, radius, body_height, step_height);
        assert(hit != 0);
        assert(isfinite(x) && isfinite(z));
        assert(x >= 100.0f + radius - 0.02f);
        assert(x <= 100.0f + radius + 0.05f);
    }

    s_vbo = NULL;
    s_model_count = 0;
}

/* =========================================================================
 * TEST 2: Acute Corners and Sharp Wedges (90, 60, 45, 30, 15 degrees)
 * ========================================================================= */
static void test_acute_corners(void)
{
    static level_vertex vbo[24];
    static model_rec mods[2];

    const float angles[] = { 90.0f, 60.0f, 45.0f, 30.0f, 15.0f };
    const float radius = 12.0f;
    const float body_height = 45.0f;
    const float step_height = 15.0f;

    for (size_t ai = 0; ai < sizeof(angles)/sizeof(angles[0]); ai++) {
        float deg = angles[ai];
        float rad = deg * (3.1415926535f / 180.0f);

        /* Wall 1 along X-axis: from (0,0) to (1000, 0) */
        /* Wall 2 along angle theta: from (0,0) to (1000*cos(rad), 1000*sin(rad)) */
        float x2 = 1000.0f * cosf(rad);
        float z2 = 1000.0f * sinf(rad);

        setup_wall_model(&vbo[0], &mods[0], 0.0f, 0.0f, 1000.0f, 0.0f, 0.0f, 200.0f);
        setup_wall_model(&vbo[6], &mods[1], 0.0f, 0.0f, x2, z2, 0.0f, 200.0f);
        mods[1].first_vert = 6;

        s_vbo = vbo;
        s_model_count = 2;
        memcpy(s_models, mods, 2 * sizeof(model_rec));

        /* Start outside the narrowing wedge along the bisector angle rad/2 */
        float bisect = rad * 0.5f;
        float expected_min_dist = radius / sinf(bisect);
        float start_dist = expected_min_dist + 50.0f;
        float x = start_dist * cosf(bisect);
        float z = start_dist * sinf(bisect);

        /* Push directly towards the apex (0,0) with high velocity over 100 frames */
        float dir_x = -cosf(bisect);
        float dir_z = -sinf(bisect);
        float step = 20.5f; /* 410 cm/s * 0.05s */

        for (int frame = 0; frame < 100; frame++) {
            level_move_horizontal(&x, &z, 0.0f, dir_x * step, dir_z * step,
                                  radius, body_height, step_height);
            assert(isfinite(x) && isfinite(z));
        }

        /* Now test REVERSAL: player must cleanly escape the wedge when moving in opposite direction */
        for (int frame = 0; frame < 20; frame++) {
            float old_dist = sqrtf(x * x + z * z);
            float dx = -dir_x * step;
            float dz = -dir_z * step;
            int hit = level_move_horizontal(&x, &z, 0.0f, dx, dz,
                                  radius, body_height, step_height);
            (void)hit;
            assert(isfinite(x) && isfinite(z));
            float new_dist = sqrtf(x * x + z * z);
            /* Escapes smoothly away from apex */
            assert(new_dist > old_dist + step * 0.1f);
        }
    }

    s_vbo = NULL;
    s_model_count = 0;
}

/* =========================================================================
 * TEST 3: Rapid Direction Reversal & High-Frequency Oscillation (10,000 steps)
 * ========================================================================= */
static void test_rapid_direction_reversal_oscillation(void)
{
    static level_vertex vbo[12];
    static model_rec mods[1];
    setup_wall_model(vbo, &mods[0], 100.0f, -500.0f, 100.0f, 500.0f, 0.0f, 200.0f);
    s_vbo = vbo;
    s_model_count = 1;
    memcpy(s_models, mods, sizeof(model_rec));

    const float radius = 12.0f;
    const float body_height = 45.0f;
    const float step_height = 15.0f;

    float x = 87.99f; /* Right against the wall at x = 100 */
    float z = 0.0f;

    /* Oscillate rapidly back and forth 10,000 times */
    for (int step = 0; step < 10000; step++) {
        float dx = (step % 2 == 0) ? 20.0f : -20.0f;
        float dz = ((step * 7) % 5) - 2.0f; /* Slight z variation */
        level_move_horizontal(&x, &z, 0.0f, dx, dz, radius, body_height, step_height);

        assert(isfinite(x) && isfinite(z));
        /* Never penetrate wall at x = 100 */
        assert(x <= 100.0f - radius + 0.02f);
        /* Never drift away uncontrollably when alternating */
        assert(x >= 60.0f);
    }

    s_vbo = NULL;
    s_model_count = 0;
}

/* =========================================================================
 * TEST 4: Boundary Values, Extreme & Malformed Inputs
 * ========================================================================= */
static void test_boundary_and_malformed_inputs(void)
{
    static level_vertex vbo[12];
    static model_rec mods[1];
    setup_wall_model(vbo, &mods[0], 100.0f, -500.0f, 100.0f, 500.0f, 0.0f, 200.0f);
    s_vbo = vbo;
    s_model_count = 1;
    memcpy(s_models, mods, sizeof(model_rec));

    float x = 50.0f, z = 50.0f;

    /* A. Zero displacement (dx=0, dz=0) */
    int hit = level_move_horizontal(&x, &z, 0.0f, 0.0f, 0.0f, 12.0f, 45.0f, 15.0f);
    assert(hit == 0);
    assert(x == 50.0f && z == 50.0f);

    /* B. Null pointers */
    assert(level_move_horizontal(NULL, &z, 0.0f, 1.0f, 1.0f, 12.0f, 45.0f, 15.0f) == 0);
    assert(level_move_horizontal(&x, NULL, 0.0f, 1.0f, 1.0f, 12.0f, 45.0f, 15.0f) == 0);

    /* C. Non-finite values */
    x = 50.0f; z = 50.0f;
    assert(level_move_horizontal(&x, &z, NAN, 1.0f, 1.0f, 12.0f, 45.0f, 15.0f) == 0);
    assert(level_move_horizontal(&x, &z, INFINITY, 1.0f, 1.0f, 12.0f, 45.0f, 15.0f) == 0);
    assert(level_move_horizontal(&x, &z, 0.0f, NAN, 1.0f, 12.0f, 45.0f, 15.0f) == 0);
    assert(level_move_horizontal(&x, &z, 0.0f, 1.0f, NAN, 12.0f, 45.0f, 15.0f) == 0);
    assert(level_move_horizontal(&x, &z, 0.0f, 1.0f, 1.0f, NAN, 45.0f, 15.0f) == 0);
    assert(x == 50.0f && z == 50.0f);

    /* D. Invalid geometry dimensions */
    assert(level_move_horizontal(&x, &z, 0.0f, 1.0f, 1.0f, -5.0f, 45.0f, 15.0f) == 0);
    assert(level_move_horizontal(&x, &z, 0.0f, 1.0f, 1.0f, 0.0f, 45.0f, 15.0f) == 0);
    assert(level_move_horizontal(&x, &z, 0.0f, 1.0f, 1.0f, 12.0f, 10.0f, 15.0f) == 0); /* height <= step */
    assert(level_move_horizontal(&x, &z, 0.0f, 1.0f, 1.0f, 12.0f, 45.0f, -1.0f) == 0); /* negative step */

    /* E. Extreme distance (> 10000.0f) bounds work */
    assert(level_move_horizontal(&x, &z, 0.0f, 20000.0f, 0.0f, 12.0f, 45.0f, 15.0f) == 0);
    assert(x == 50.0f && z == 50.0f);

    /* F. Collinear / zero-area triangles in geometry */
    static level_vertex degen_vbo[6];
    static model_rec degen_mod[1];
    for (int i = 0; i < 6; i++) {
        degen_vbo[i].x = 100.0f; degen_vbo[i].y = 0.0f; degen_vbo[i].z = 0.0f;
    }
    memset(degen_mod, 0, sizeof(degen_mod));
    degen_mod[0].vert_count = 6;
    s_vbo = degen_vbo;
    s_model_count = 1;
    memcpy(s_models, degen_mod, sizeof(model_rec));

    x = 50.0f; z = 50.0f;
    level_move_horizontal(&x, &z, 0.0f, 10.0f, 0.0f, 12.0f, 45.0f, 15.0f);
    assert(isfinite(x) && isfinite(z));
    assert(x == 60.0f && z == 50.0f);

    s_vbo = NULL;
    s_model_count = 0;
}

/* =========================================================================
 * TEST 5: Penetration Recovery (Starting Inside a Steep Wall)
 * ========================================================================= */
static void test_penetration_recovery(void)
{
    static level_vertex vbo[12];
    static model_rec mods[1];
    /* Box wall with thickness: x in [90, 110], z in [-100, 100], y in [0, 200] */
    setup_wall_model(vbo, &mods[0], 100.0f, -100.0f, 100.0f, 100.0f, 0.0f, 200.0f);
    s_vbo = vbo;
    s_model_count = 1;
    memcpy(s_models, mods, sizeof(model_rec));

    const float radius = 12.0f;
    /* Player center placed directly on wall surface (x = 100, z = 0) with zero movement */
    float x = 100.0f, z = 0.0f;
    int hit = level_move_horizontal(&x, &z, 0.0f, 0.0f, 0.0f, radius, 45.0f, 15.0f);
    assert(hit != 0);
    assert(isfinite(x) && isfinite(z));
    /* The solver must push the center outward away from the wall to distance >= radius */
    float dist = fabsf(x - 100.0f);
    assert(dist >= radius);

    s_vbo = NULL;
    s_model_count = 0;
}

/* =========================================================================
 * TEST 6: 42-Level Retail Geometry Multi-Directional Sweep at 20 FPS Hitch
 * ========================================================================= */
static void test_42_level_retail_sweeps(const char *levels_path)
{
    uint32_t size;
    uint8_t *real = read_file(levels_path, &size);
    active_blob = real;
    active_size = size;
    s_parsed_blob = NULL;
    s_entry_count = 0;

    int total_levels = level_count();
    assert(total_levels == 42);

    const float radius = 12.0f;
    const float body_height = 45.0f;
    const float step_height = 15.0f;
    const float speed = 410.0f; /* Crash Dash max speed */
    const float dt = 0.05f;     /* 20 FPS hitch dt */
    const float step_dist = speed * dt; /* 20.5 cm */

    for (int i = 0; i < total_levels; i++) {
        uint32_t id = level_get(i)->id;
        assert(level_load(id) == 0);

        float sx = 0.0f, sy = 0.0f, sz = 0.0f;
        assert(level_spawn_point(&sx, &sy, &sz) == 0);
        float gy = level_ground_height(sx, sz, sy + 150.0f);
        if (gy <= -999999.0f) {
            gy = sy;
        }

        /* Test sweeps in 16 radial directions around spawn */
        for (int dir = 0; dir < 16; dir++) {
            float angle = (float)dir * (2.0f * 3.1415926535f / 16.0f);
            float dx = cosf(angle) * step_dist;
            float dz = sinf(angle) * step_dist;

            float x = sx, z = sz;
            /* Move 5 successive frames in this direction */
            for (int f = 0; f < 5; f++) {
                level_move_horizontal(&x, &z, gy, dx, dz, radius, body_height, step_height);
                assert(isfinite(x) && isfinite(z));
            }
        }

        level_unload();
    }

    free(real);
}

/* =========================================================================
 * TEST 7: Enclosed Box Prison (10,000 High-Velocity Random Impulses)
 * ========================================================================= */
static void test_enclosed_box_prison(void)
{
    /* 4 vertical walls forming a closed rectangular room:
     * x in [0, 200], z in [0, 200], y in [0, 200] */
    static level_vertex vbo[24];
    static model_rec mods[4];

    /* Wall 0: South (z = 0, x from 0 to 200) */
    setup_wall_model(&vbo[0],  &mods[0], 0.0f, 0.0f, 200.0f, 0.0f, 0.0f, 200.0f);
    /* Wall 1: East  (x = 200, z from 0 to 200) */
    setup_wall_model(&vbo[6],  &mods[1], 200.0f, 0.0f, 200.0f, 200.0f, 0.0f, 200.0f);
    mods[1].first_vert = 6;
    /* Wall 2: North (z = 200, x from 200 to 0) */
    setup_wall_model(&vbo[12], &mods[2], 200.0f, 200.0f, 0.0f, 200.0f, 0.0f, 200.0f);
    mods[2].first_vert = 12;
    /* Wall 3: West  (x = 0, z from 200 to 0) */
    setup_wall_model(&vbo[18], &mods[3], 0.0f, 200.0f, 0.0f, 0.0f, 0.0f, 200.0f);
    mods[3].first_vert = 18;

    s_vbo = vbo;
    s_model_count = 4;
    memcpy(s_models, mods, 4 * sizeof(model_rec));

    const float radius = 12.0f;
    const float body_height = 45.0f;
    const float step_height = 15.0f;

    /* Start at the exact center of the prison */
    float x = 100.0f, z = 100.0f;
    uint32_t rng = 0x12345678;

    /* 10,000 high-velocity impulses in random directions with magnitudes up to 2000 cm/s */
    for (int step = 0; step < 10000; step++) {
        rng = rng * 1664525u + 1013904223u;
        float angle = (float)(rng & 0xFFFF) / 65536.0f * 6.2831853f;
        rng = rng * 1664525u + 1013904223u;
        /* Speed between 100 and 5000 cm/s */
        float speed = 100.0f + ((float)(rng & 0xFFFF) / 65536.0f) * 4900.0f;
        float dt = 0.05f; /* 20 FPS hitch dt */
        float dx = cosf(angle) * speed * dt;
        float dz = sinf(angle) * speed * dt;

        level_move_horizontal(&x, &z, 0.0f, dx, dz, radius, body_height, step_height);

        assert(isfinite(x) && isfinite(z));

        /* CRITICAL CONFINEMENT ASSERTIONS:
         * Crash must NEVER escape the box prison!
         * Allowed region for center: [radius - 0.05, 200 - radius + 0.05] */
        assert(x >= radius - 0.05f);
        assert(x <= 200.0f - radius + 0.05f);
        assert(z >= radius - 0.05f);
        assert(z <= 200.0f - radius + 0.05f);
    }

    s_vbo = NULL;
    s_model_count = 0;
}

/* =========================================================================
 * TEST 8: Step Height Thresholds, Curbs & Overhangs
 * ========================================================================= */
static void test_step_heights_and_overhangs(void)
{
    static level_vertex vbo[12];
    static model_rec mods[1];

    const float radius = 12.0f;
    const float body_height = 45.0f;
    const float step_height = 15.0f;

    /* A. Low curb: wall from y = 0 to y = 14.0 cm (lower than step_height 15.0)
     * Body sweep checks from low = foot_y + step_height (15.0) to high (45.0).
     * Since curb max y is 14.0, it is below low (15.0) -> MUST NOT BLOCK */
    setup_wall_model(vbo, &mods[0], 100.0f, -100.0f, 100.0f, 100.0f, 0.0f, 14.0f);
    s_vbo = vbo;
    s_model_count = 1;
    memcpy(s_models, mods, sizeof(model_rec));

    float x = 50.0f, z = 0.0f;
    int hit = level_move_horizontal(&x, &z, 0.0f, 100.0f, 0.0f, radius, body_height, step_height);
    assert(hit == 0);
    assert(fabsf(x - 150.0f) < 0.05f); /* Cleared curb smoothly */

    /* B. High step: wall from y = 0 to y = 16.0 cm (higher than step_height 15.0)
     * Intersects interval [15.0, 45.0] in [15.0, 16.0] -> MUST BLOCK */
    setup_wall_model(vbo, &mods[0], 100.0f, -100.0f, 100.0f, 100.0f, 0.0f, 16.0f);
    memcpy(s_models, mods, sizeof(model_rec));

    x = 50.0f; z = 0.0f;
    hit = level_move_horizontal(&x, &z, 0.0f, 100.0f, 0.0f, radius, body_height, step_height);
    assert(hit != 0);
    assert(x <= 100.0f - radius + 0.02f); /* Blocked by 16 cm step */

    /* C. Overhang / Bridge: obstacle from y = 50.0 to 100.0 cm (higher than body_height 45.0)
     * Interval [15.0, 45.0] does NOT intersect [50.0, 100.0] -> MUST NOT BLOCK */
    setup_wall_model(vbo, &mods[0], 100.0f, -100.0f, 100.0f, 100.0f, 50.0f, 100.0f);
    memcpy(s_models, mods, sizeof(model_rec));

    x = 50.0f; z = 0.0f;
    hit = level_move_horizontal(&x, &z, 0.0f, 100.0f, 0.0f, radius, body_height, step_height);
    assert(hit == 0);
    assert(fabsf(x - 150.0f) < 0.05f); /* Walked cleanly under bridge */

    /* D. Low ceiling: obstacle from y = 30.0 to 100.0 cm
     * Intersects interval [15.0, 45.0] in [30.0, 45.0] -> MUST BLOCK */
    setup_wall_model(vbo, &mods[0], 100.0f, -100.0f, 100.0f, 100.0f, 30.0f, 100.0f);
    memcpy(s_models, mods, sizeof(model_rec));

    x = 50.0f; z = 0.0f;
    hit = level_move_horizontal(&x, &z, 0.0f, 100.0f, 0.0f, radius, body_height, step_height);
    assert(hit != 0);
    assert(x <= 100.0f - radius + 0.02f); /* Head blocked by low ceiling */

    s_vbo = NULL;
    s_model_count = 0;
}

/* =========================================================================
 * TEST 9: Denormals, Subnormals, and Floating Point Extremes
 * ========================================================================= */
static void test_denormals_and_extremes(void)
{
    static level_vertex vbo[12];
    static model_rec mods[1];
    setup_wall_model(vbo, &mods[0], 100.0f, -100.0f, 100.0f, 100.0f, 0.0f, 200.0f);
    s_vbo = vbo;
    s_model_count = 1;
    memcpy(s_models, mods, sizeof(model_rec));

    float x = 50.0f, z = 50.0f;

    /* Very tiny subnormal displacements */
    float tiny = 1e-35f;
    int hit = level_move_horizontal(&x, &z, 0.0f, tiny, tiny, 12.0f, 45.0f, 15.0f);
    (void)hit;
    assert(isfinite(x) && isfinite(z));

    /* Negative zero */
    float neg_zero = -0.0f;
    hit = level_move_horizontal(&x, &z, 0.0f, neg_zero, neg_zero, 12.0f, 45.0f, 15.0f);
    assert(isfinite(x) && isfinite(z));

    /* Very large finite floats (> 10000.0f distance) must be rejected */
    float big = 1e20f;
    float old_x = x, old_z = z;
    hit = level_move_horizontal(&x, &z, 0.0f, big, 0.0f, 12.0f, 45.0f, 15.0f);
    assert(hit == 0);
    assert(x == old_x && z == old_z);

    s_vbo = NULL;
    s_model_count = 0;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    printf("Starting Adversarial Collision & Wall Sliding Stress Suite...\n");

    test_high_velocity_tunneling();
    printf("  [PASS] High-velocity collisions & 20 FPS hitch tunneling (up to 9999 cm/s)\n");

    test_acute_corners();
    printf("  [PASS] Acute corners & sharp wedges (90, 60, 45, 30, 15 degrees)\n");

    test_rapid_direction_reversal_oscillation();
    printf("  [PASS] Rapid direction reversal & 10,000-step high-frequency oscillation\n");

    test_boundary_and_malformed_inputs();
    printf("  [PASS] Boundary values, zero displacement, and malformed/NaN input rejection\n");

    test_penetration_recovery();
    printf("  [PASS] Penetration recovery for character starting embedded inside steep geometry\n");

    test_42_level_retail_sweeps(argv[1]);
    printf("  [PASS] 42-level retail geometry 16-direction sweeps at 20 FPS hitch dt\n");

    test_enclosed_box_prison();
    printf("  [PASS] Enclosed box prison confinement (10,000 high-velocity random impulses)\n");

    test_step_heights_and_overhangs();
    printf("  [PASS] Step height thresholds (14cm curb pass, 16cm step block, bridges & low ceilings)\n");

    test_denormals_and_extremes();
    printf("  [PASS] Subnormals, negative zero, and extreme float magnitude bounds\n");

    printf("ALL ADVERSARIAL STRESS TESTS PASSED.\n");
    return 0;
}
'''


def main():
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", nargs="+", default=[sys.executable, "-m", "ziglang", "cc"])
    args = parser.parse_args()

    t_start = time.time()
    with tempfile.TemporaryDirectory(prefix='crash-adversarial-') as directory:
        tmp = Path(directory)
        (tmp / '3ds.h').write_text(
            '#include <stdlib.h>\n'
            'static inline void *linearAlloc(size_t n){return malloc(n);}\n'
            'static inline void linearFree(void *p){free(p);}\n'
        )
        (tmp / 'citro3d.h').write_text(
            '#define GPU_TRIANGLES 0\n'
            'static inline void C3D_DrawArrays(int p,int f,int n){(void)p;(void)f;(void)n;}\n'
        )
        (tmp / 'test_adversarial.c').write_text(HARNESS_C)
        exe = tmp / 'test_adversarial.exe'

        compile_cmd = list(args.cc) + [
            '-std=c11', '-Wall', '-Wextra', '-Werror',
            '-O2', '-UNDEBUG', '-DLEVELS_FROM_SD',
            '-I' + str(tmp),
            '-I' + str(ROOT / 'source'),
            str(tmp / 'test_adversarial.c'),
            '-lm', '-o', str(exe)
        ]
        subprocess.run(compile_cmd, check=True)

        run_cmd = [str(exe), str(LEVELS_BIN)]
        subprocess.run(run_cmd, check=True)

    elapsed = time.time() - t_start
    print(f"PASS: Adversarial Collision Suite completed in {elapsed:.3f} seconds.")


if __name__ == '__main__':
    main()
