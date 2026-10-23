/* Original Crash mesh with interpolated extracted SVTX animation clips. */
#ifndef CHAR_H
#define CHAR_H

#include "level.h"

/* Runtime timing convention; animation lengths come from GOOL groups. */
#define CHAR_LOGIC_HZ 30.0f
#define CHAR_LAND_DURATION (24.0f / CHAR_LOGIC_HZ)

typedef struct {
    float x, y, z;        /* world position, cm */
    float yaw;            /* facing, radians */
    float walk;           /* walk cycle phase */
    float vx, vy, vz;     /* velocity, cm/s */
    float spin_timer;     /* spin attack countdown */
    float spin_duration;  /* full duration of the current spin */
    float anim_time;      /* continuous animation timer */
    float air_time;       /* elapsed time since take-off */
    float landing_timer; /* remaining time of landing animation */
    int   onground;
    int   can_double_jump;/* Double Jump power */
    int   is_slamming;    /* Super Belly Flop power */
    float slam_timer;
    int   is_sliding;
    float slide_timer;
} char_state;

/* Fills `out` with the real Crash Bandicoot mesh interpolated between keyframes.
 * Returns the number of vertices written (0 if cap < CRASH_CORNER_COUNT). */
int char_build(level_vertex *out, int cap, const char_state *st);

#endif
