/* Crash Bandicoot (PS1) real character mesh & SVTX animation player for 3DS.
 *
 * Geometry (Cr10G) and animations (Cr10V, CrW0V, CJF1V, CrS1V) are extracted
 * directly from the original disc archives and interpolated smoothly via
 * lerp across keyframes.
 */
#include <math.h>
#include <string.h>

#include "char.h"
#include "crash_model.h"

int char_build(level_vertex *out, int cap, const char_state *st)
{
    if (cap < CRASH_CORNER_COUNT || !out || !st)
        return 0;

    const crash_pt (*clip_a)[CRASH_NUM_VERTS];
    int frame_count;
    float frame;
    int loop = 1;
    float speed = sqrtf(st->vx * st->vx + st->vz * st->vz);

    if (st->spin_timer > 0.0f) {
        clip_a = crash_anim_spin;
        frame_count = CRASH_FRAMES_SPIN;
        float duration = st->spin_duration > 0.0f ? st->spin_duration : 0.35f;
        float progress = 1.0f - st->spin_timer / duration;
        if (progress < 0.0f) progress = 0.0f;
        frame = progress * (float)frame_count;
    } else if (!st->onground) {
        if (st->air_time < (float)CRASH_TICKS_TAKEOFF / CHAR_LOGIC_HZ && st->vy > 0.0f) {
            clip_a = crash_anim_takeoff;
            frame_count = CRASH_FRAMES_TAKEOFF;
            frame = st->air_time * CHAR_LOGIC_HZ * frame_count / CRASH_TICKS_TAKEOFF;
            loop = 0;
        } else if (st->vy >= -50.0f && !st->is_slamming) {
            clip_a = crash_anim_jump_up;
            frame_count = CRASH_FRAMES_JUMP_UP;
            frame = st->air_time * CHAR_LOGIC_HZ * frame_count / CRASH_TICKS_JUMP_UP;
        } else {
            clip_a = crash_anim_jump;
            frame_count = CRASH_FRAMES_JUMP;
            frame = st->air_time * CHAR_LOGIC_HZ * frame_count / CRASH_TICKS_JUMP;
            loop = 0;
        }
    } else if (speed > 15.0f) {
        clip_a = speed > 180.0f ? crash_anim_run : crash_anim_walk;
        frame_count = speed > 180.0f ? CRASH_FRAMES_RUN : CRASH_FRAMES_WALK;
        frame = st->walk / 6.28318531f * (float)frame_count;
    } else if (st->landing_timer > 0.0f) {
        clip_a = crash_anim_land;
        frame_count = CRASH_FRAMES_LAND;
        frame = (1.0f - st->landing_timer / CHAR_LAND_DURATION) * (float)(frame_count - 1);
        loop = 0;
    } else {
        clip_a = crash_anim_idle;
        frame_count = CRASH_FRAMES_IDLE;
        frame = st->anim_time * CHAR_LOGIC_HZ * frame_count / CRASH_TICKS_IDLE;
    }

    if (frame < 0.0f) frame = 0.0f;
    if (loop) frame = fmodf(frame, (float)frame_count);
    else if (frame > (float)(frame_count - 1)) frame = (float)(frame_count - 1);
    int frame_a = (int)frame;
    int frame_b = frame_a + 1;
    if (frame_b >= frame_count) frame_b = loop ? 0 : frame_a;
    float t = frame - (float)frame_a;

    /* 1. Lerp unique base vertices between frame_a and frame_b */
    crash_pt lerped[CRASH_NUM_VERTS];
    const crash_pt *ptsA = clip_a[frame_a];
    const crash_pt *ptsB = clip_a[frame_b];

    for (int i = 0; i < CRASH_NUM_VERTS; i++) {
        lerped[i].x = ptsA[i].x + (ptsB[i].x - ptsA[i].x) * t;
        lerped[i].y = ptsA[i].y + (ptsB[i].y - ptsA[i].y) * t;
        lerped[i].z = ptsA[i].z + (ptsB[i].z - ptsA[i].z) * t;
    }

    /* 2. Assemble corners into level_vertex buffer with yaw rotation and translation */
    float cy = cosf(st->yaw);
    float sy = sinf(st->yaw);
    float ox = st->x;
    float oy = st->y;
    float oz = st->z;

    for (int v = 0; v < CRASH_CORNER_COUNT; v++) {
        uint16_t vi = crash_indices[v];
        if (vi >= CRASH_NUM_VERTS)
            vi = 0;

        float px = lerped[vi].x;
        float py = lerped[vi].y;
        float pz = lerped[vi].z;

        /* Rotate around Y by player yaw */
        float rx = px * cy + pz * sy;
        float rz = -px * sy + pz * cy;

        level_vertex *out_v = &out[v];
        out_v->x = rx + ox;
        out_v->y = py + oy;
        out_v->z = rz + oz;

        out_v->r = crash_colors[v].r;
        out_v->g = crash_colors[v].g;
        out_v->b = crash_colors[v].b;
    }

    return CRASH_CORNER_COUNT;
}
