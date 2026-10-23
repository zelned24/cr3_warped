#ifndef BOTTOM_UI_H
#define BOTTOM_UI_H

#include <stdint.h>
#include <stddef.h>
#include "level.h"

typedef enum {
    BOTTOM_UI_NONE, BOTTOM_UI_SAVE, BOTTOM_UI_LOAD, BOTTOM_UI_HUB,
    BOTTOM_UI_CAMERA, BOTTOM_UI_RESUME, BOTTOM_UI_RESTART, BOTTOM_UI_TITLE,
    BOTTOM_UI_STATUS_PAGE, BOTTOM_UI_TRIAL_PAGE
} bottom_ui_action;

typedef enum { BOTTOM_PAGE_STATUS, BOTTOM_PAGE_TRIAL } bottom_ui_page;

typedef struct {
    const char *level_name;
    const char *message;
    int wumpa, boxes, boxes_total, lives;
    int aku_aku, time_trial, boss_active, boss_hp, boss_max_hp;
    int paused, pause_index, aiming;
    uint32_t time_trial_ms;
    bottom_ui_page page;
    int trial_targets_available, saved_relic_tier;
    uint32_t trial_targets_ms[3], best_time_ms;
    const char *context;
    const char *context_detail;
    uint32_t crystals, gems, relics, powers, completion;
} bottom_ui_state;

/* Hit boxes share the same geometry as the rendered buttons. */
bottom_ui_action bottom_ui_hit_test(int x, int y, int paused);

/* Missing values use --:--.--; zero is a valid running timer when available. */
void bottom_ui_format_time(char *out, size_t capacity, uint32_t ms, int available);

/* Pure CPU mesh builder. Pixel coordinates map directly onto the 320x240
 * bottom framebuffer; the same vertices drive the host preview. */
int bottom_ui_build(level_vertex *vertices, int capacity, const bottom_ui_state *state);

#endif
