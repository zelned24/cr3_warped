/* Crash Bandicoot: Warped (PSX) -> 3DS port - level data handling. */
#ifndef LEVEL_H
#define LEVEL_H

#include <stdint.h>
#include "audio.h"

/* Returns authentic BGM theme for a level ID (0x02..0x3C).
 * Unmapped or invalid IDs return BGM_NONE. */
BgmTheme level_get_bgm_theme(int level_id);

/* One vertex of the ported geometry, in 1/100 m (centimetres).
 * The layout matches what the PICA vertex buffer wants: two float3
 * attributes (position, colour), 24 bytes per vertex. */
typedef struct {
    float x, y, z;
    float r, g, b;
} level_vertex;

typedef struct {
    uint32_t id;              /* Crash 3 level id (0x02 .. 0x33)        */
    const uint8_t *data;      /* raw 'CBW3' blob                        */
    uint32_t size;
} level_info;

/* Index of the level inside the embedded blob. */
int  level_count(void);
const level_info *level_get(int index);
int  level_find(uint32_t id);

/* Load a level.  Returns 0 on success. */
int  level_load(uint32_t id);
void level_unload(void);

/* The zone ("world model") that is currently decoded into VRAM. */
int  level_vertex_count(void);
const level_vertex *level_vertices(void);
int  level_activate_zone(int index);

/* Individual world models ("zones") inside the level. */
typedef struct {
    uint32_t first;      /* first vertex in the shared vertex array */
    uint32_t count;      /* number of vertices                     */
    float ox, oy, oz;    /* model origin, cm                       */
    float minx, miny, minz;
    float maxx, maxy, maxz;
    int is_skybox;       /* background geometry, excluded from collision */
} level_model_info;

int  level_model_count(void);
const level_model_info *level_model(int index);
/* Index of the model whose bounds contain the position, or -1. */
int  level_model_at(float x, float z);

/* Ground height (cm) at the given world position, or a negative value if
 * there is no ground there. */
int  level_height(float x, float z);
/* Highest triangle surface at or below max_y, or -1000000 when absent.
 * Use the player's previous foot height (plus allowed step height) so an
 * overlapping roof cannot pull a player onto the upper floor. */
int  level_height_below(float x, float z, float max_y);
/* Same query without integer rounding; -1000000.0f means no surface. */
float level_ground_height(float x, float z, float max_y);
/* Sweep a vertical body through real steep WGEO surfaces. The displacement is
 * subdivided to prevent tunnelling; contact slides along walls. No artificial
 * perimeter or floor is added. Parameters are port controller dimensions,
 * not decoded PS1 collision parameters. Returns nonzero on wall contact. */
int level_move_horizontal(float *x, float *z, float foot_y,
                          float dx, float dz, float radius,
                          float body_height, float step_height);
void level_bounds(float *minx, float *miny, float *minz,
                  float *maxx, float *maxy, float *maxz);

/* Renders only models within radius of (px, py, pz) for Old 3DS performance.
 * Returns total vertices rendered. */
int  level_render_visible(float px, float py, float pz, float radius);
/* Same distance admission with conservative homogeneous side-plane culling.
 * clip is projection*modelView in named x/y/z/w row order; NULL disables it.
 * Partially visible models remain intact for hardware triangle clipping. */
int level_render_visible_frustum(float px,float py,float pz,float radius,
                                 const double clip[4][4]);

/* The NSD checkpoint identity is preserved independently of the model index.
 * No orientation/camera rail is inferred from the checkpoint record index. */
typedef struct {
    float x, y, z;
    uint32_t zdat_eid;
    uint32_t record_index;
} level_entry_checkpoint;
/* Returns 0 only when original checkpoint metadata exists. Output is unchanged
 * on failure; synthetic geometry fallbacks are deliberately not reported here. */
int level_entry_checkpoint_get(level_entry_checkpoint *out);

/* Highest walkable surface near y, within [y-max_drop, y+max_rise].
 * Returns -1000000.0f for no support or invalid parameters. This is an explicit
 * port-controller safety query, not decoded PS1 checkpoint collision logic. */
float level_ground_near(float x, float z, float y, float max_drop, float max_rise);

/* Uses the first NSD checkpoint when exported metadata is available.
 * Older metadata-free blobs fall back to a walkable triangle.
 * Returns 0 on success, -1 on failure. */
int  level_spawn_point(float *out_x, float *out_y, float *out_z);

/* Level code letter, e.g. 'a' for 0x0A (used by the original game). */
char level_code(uint32_t id);

/* Debug counters: 0=vbo valid, 1=vbo address, 2=zone switches, 3=alloc fails,
 * 4=vertices in the current zone. */
int  level_debug(int what);

#endif
