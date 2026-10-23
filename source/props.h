/* Crash Bandicoot: Warped - Interactive Props (Crates & Wumpa Fruit).
 *
 * Prototype entities generated around the spawn; not a GOOL interpreter:
 *   - Wooden regular & bounce crates (break on jump/spin, drop Wumpa fruit)
 *   - TNT crates (ignite and count down to explosion on jump)
 *   - Nitro crates (hazardous, explosive)
 *   - Checkpoint crates
 *   - Floating, spinning Wumpa fruit with pickup physics
 *   - Shatter particles when crates break
 */
#ifndef PROPS_H
#define PROPS_H

#include "level.h"
#include "char.h"

typedef enum {
    PROP_NONE = 0,
    PROP_CRATE_WOOD,
    PROP_CRATE_TNT,
    PROP_CRATE_NITRO,
    PROP_CRATE_CHECKPOINT,
    PROP_CRATE_AKU,
    PROP_CRATE_SPRING,
    PROP_CRATE_PICKUP,
    PROP_CRATE_BOUNCE,
    PROP_CRATE_SLOT,
    PROP_CRATE_LIFE,
    PROP_CRATE_SWITCH,
    PROP_WUMPA,
    PROP_CRYSTAL,
    PROP_TIME_CLOCK,
    PROP_EXIT_PORTAL,
    PROP_WARP_PORTAL,
    PROP_BOSS_ENTITY,
    PROP_ENEMY_KNIGHT,
    PROP_ENEMY_CHICKEN,
    PROP_ENEMY_GOAT,
    PROP_ENEMY_FROG,
    PROP_ENEMY_SWORD,
    PROP_ENEMY_DIVER,
    PROP_ENEMY_MUMMY,
    PROP_ENEMY_SCORPION,
    PROP_ENEMY_WIZARD,
    PROP_ENEMY_ROBOT,
    PROP_AKU_MASK          /* visual companion, not a placed pickup/crate */
} PropType;

/* Index in the original CWT surfaces; other objects retain their mesh. */
static inline int props_textured_asset_index(unsigned type) {
    switch(type) {
        case PROP_CRATE_WOOD: return 0;
        case PROP_CRATE_TNT: return 1;
        case PROP_CRATE_NITRO: return 2;
        case PROP_CRATE_CHECKPOINT: return 3;
        case PROP_WUMPA: return 4;
        case PROP_CRATE_SPRING: return 5;
        case PROP_CRATE_BOUNCE: return 6;
        case PROP_AKU_MASK: return 7;
        default: return -1;
    }
}

typedef struct {
    PropType type;
    float x, y, z;
    float sx, sy, sz;       /* size */
    int active;             /* 1 = alive, 0 = broken/collected */
    float timer;            /* TNT countdown or anim timer */
    int armed;              /* TNT countdown only advances after ignition */
    float rot_y;            /* visual spin */
    int value;              /* wumpas contained */
    int hp;                 /* for boss or durable entities */
    float attack_timer;     /* for boss behavior */
} prop_item;

#define MAX_PROPS 256
#define MAX_PARTICLES 256
#define MAX_PROJECTILES 16

typedef struct {
    int active;
    float x, y, z;
    float vx, vy, vz;
    float life;
} prop_projectile;

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    float r, g, b;
    float life;             /* remaining lifetime */
    float size;
} prop_particle;

/* Game inventory stats */
typedef struct {
    int wumpa_count;
    int crates_broken;
    int crates_total;
    int lives;
    int aku_aku_tier;       /* 0=none, 1=normal, 2=gold, 3=invincible */
    float aku_timer;        /* invincibility countdown */
} game_stats;

extern game_stats g_stats;

/* Initialize props for the current level & spawn location */
void props_init_level(int level_id, float player_x, float player_y, float player_z);
void props_init_level_facing(int level_id, float x, float y, float z, float yaw);
int props_checkpoint_point(float *x,float *y,float *z);

/* Update props physics, animations, and player interactions */
void props_update(float dt, char_state *player);

/* Spawn dust shockwave puff when Crash lands from warp or high jump */
void props_spawn_landing_dust(float x, float y, float z);

/* Activate/update the Time Twister entrance vortex */
void props_set_vortex(int active, float x, float y, float z, float intensity);

/* Fruit Bazooka projectile */
void props_spawn_projectile(float x, float y, float z, float dx, float dy, float dz);

/* Level goal queries */
int  props_check_exit(float px, float py, float pz);
int  props_is_secret_exit(void);
int  props_get_broken_crates(void);
int  props_get_total_crates(void);
int  props_crystal_collected(void);
int  props_is_time_trial(void);
uint32_t props_get_time_trial_ms(void);
int  props_get_warp_target(float px, float py, float pz);
const prop_item *props_get_nearest_portal(float px, float py, float pz, float *out_dist);

/* Boss entity queries */
int  props_boss_hp(void);
int  props_boss_active(void);
void props_damage_boss(int amount);

/* Builds all active props and particles into out vertex buffer. Returns vertex count. */
int props_build_mesh(level_vertex *out, int cap);
/* Read-only instance access for the original textured object renderer. */
int props_count(void);
const prop_item *props_get(int index);
/* Read-only visual placement for protection; does not occupy an entity slot.
 * Placement is port behavior, not a decoded GOOL follow controller. */
int props_aku_visual(const char_state *player,float view_yaw,prop_item *out);
int props_build_effects_mesh(level_vertex *out, int cap);
int props_build_untextured_mesh(level_vertex *out,int cap);
int props_build_missing_mesh(level_vertex *out,int cap,uint32_t textured_types);

#ifndef WARP_ROOM_STUBS_DEFINED
static inline void trigger_level_clear(void) {}
static inline void update_warp_room(float dt) { (void)dt; }
static inline void draw_warp_room_hud(void) {}
#endif

#endif /* PROPS_H */

