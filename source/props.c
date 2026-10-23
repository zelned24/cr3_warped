/* Crash Bandicoot: Warped - Interactive Props Implementation.
 * High-fidelity native implementation with ZDAT disc entities,
 * authentic crate mechanics, enemies, and Time Trial clock safeguards.
 */
#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "props.h"
#include "audio.h"
#include "story.h"
#include "level_entities.h"

game_stats g_stats = {
    .wumpa_count = 0,
    .crates_broken = 0,
    .crates_total = 0,
    .lives = 4,
    .aku_aku_tier = 0,
    .aku_timer = 0.0f
};

static prop_item s_props[MAX_PROPS];
static int s_prop_count = 0;
static int s_checkpoint_valid;
static float s_checkpoint[3];

int props_checkpoint_point(float *x,float *y,float *z)
{
    if (!s_checkpoint_valid || !x || !y || !z) return 0;
    *x=s_checkpoint[0]; *y=s_checkpoint[1]; *z=s_checkpoint[2];
    return 1;
}

static prop_particle s_particles[MAX_PARTICLES];
static int s_particle_count = 0;

static prop_projectile s_projectiles[MAX_PROJECTILES];
static int s_projectile_count = 0;

static int s_time_trial_active = 0;
static float s_time_trial_timer = 0.0f;
static int s_crystal_collected = 0;
static int s_exit_reached = 0;
static int s_secret_exit_reached = 0;

static int s_boss_level = 0;
static int s_boss_idx = -1;
static int s_boss_hp = 0;
static int s_boss_max_hp = 0;
static float s_boss_invuln = 0.0f;

static struct {
    int active;
    float x, y, z;
    float time;
    float intensity;
} s_vortex = { 0 };

void props_set_vortex(int active, float x, float y, float z, float intensity)
{
    s_vortex.active = active;
    s_vortex.x = x;
    s_vortex.y = y;
    s_vortex.z = z;
    s_vortex.intensity = intensity;
}

void props_spawn_landing_dust(float x, float y, float z)
{
    for (int i = 0; i < 20; i++) {
        if (s_particle_count >= MAX_PARTICLES)
            break;
        float ang = ((float)i / 20.0f) * 6.2831853f;
        float spd = 60.0f + (float)(rand() % 60);
        prop_particle *p = &s_particles[s_particle_count++];
        p->x = x + cosf(ang) * 6.0f;
        p->y = y + 2.0f;
        p->z = z + sinf(ang) * 6.0f;
        p->vx = cosf(ang) * spd;
        p->vy = 25.0f + (float)(rand() % 35);
        p->vz = sinf(ang) * spd;
        p->r = 0.88f; p->g = 0.82f; p->b = 0.70f;
        p->life = 0.45f + ((float)(rand() % 20) / 100.0f);
        p->size = 5.0f;
    }
}

static void spawn_particle(float x, float y, float z, float r, float g, float b, float size)
{
    if (s_particle_count >= MAX_PARTICLES)
        return;
    prop_particle *p = &s_particles[s_particle_count++];
    p->x = x; p->y = y; p->z = z;
    p->vx = ((float)(rand() % 200) - 100.0f) * 1.5f;
    p->vy = 180.0f + (float)(rand() % 160);
    p->vz = ((float)(rand() % 200) - 100.0f) * 1.5f;
    p->r = r; p->g = g; p->b = b;
    p->life = 0.8f + ((float)(rand() % 40) / 100.0f);
    p->size = size;
}

static void break_crate(prop_item *item)
{
    item->active = 0;
    g_stats.crates_broken++;
    if (item->type == PROP_CRATE_CHECKPOINT) {
        s_checkpoint_valid=1;
        s_checkpoint[0]=item->x;
        s_checkpoint[1]=item->y;
        s_checkpoint[2]=item->z;
    }

    if (item->type == PROP_CRATE_TNT || item->type == PROP_CRATE_NITRO)
        audio_play_sfx(SFX_EXPLODE);
    else
        audio_play_sfx(SFX_CRATE_BREAK);

    /* Splinters color */
    float pr = 0.85f, pg = 0.50f, pb = 0.15f;
    if (item->type == PROP_CRATE_TNT) { pr = 0.95f; pg = 0.15f; pb = 0.10f; }
    else if (item->type == PROP_CRATE_NITRO) { pr = 0.15f; pg = 0.95f; pb = 0.20f; }
    else if (item->type == PROP_CRATE_CHECKPOINT) { pr = 0.75f; pg = 0.80f; pb = 0.90f; }
    else if (item->type == PROP_CRATE_AKU) {
        pr = 0.95f; pg = 0.65f; pb = 0.15f;
        if (g_stats.aku_aku_tier < 3) g_stats.aku_aku_tier++;
        if (g_stats.aku_aku_tier == 3) g_stats.aku_timer = 15.0f;
        audio_play_sfx(SFX_AKU_AKU);
    }
    else if (item->type == PROP_CRATE_SPRING) { pr = 0.98f; pg = 0.90f; pb = 0.20f; }
    else if (item->type == PROP_CRATE_PICKUP) { pr = 0.95f; pg = 0.75f; pb = 0.15f; }
    else if (item->type == PROP_CRATE_BOUNCE) { pr = 0.90f; pg = 0.55f; pb = 0.18f; }
    else if (item->type == PROP_CRATE_SLOT) { pr = 0.80f; pg = 0.40f; pb = 0.85f; }
    else if (item->type == PROP_CRATE_LIFE) {
        pr = 0.95f; pg = 0.50f; pb = 0.60f;
        g_stats.lives++;
        audio_play_sfx(SFX_WUMPA);
    }

    for (int i = 0; i < 14; i++) {
        spawn_particle(item->x, item->y + 12.0f, item->z, pr, pg, pb, 4.0f);
    }

    /* Spawn dropped Wumpa */
    if (item->value > 0 && s_prop_count < MAX_PROPS) {
        prop_item *w = &s_props[s_prop_count++];
        w->type = PROP_WUMPA;
        w->x = item->x;
        w->y = item->y + 20.0f;
        w->z = item->z;
        w->sx = w->sy = w->sz = 14.0f;
        w->active = 1;
        w->timer = 0.0f;
        w->armed = 0;
        w->rot_y = 0.0f;
        w->value = 1;
    }
}

void props_init_level_facing(int level_id, float px, float py, float pz, float yaw)
{
    s_prop_count = 0;
    s_checkpoint_valid = 0;
    s_particle_count = 0;
    s_projectile_count = 0;
    s_time_trial_active = 0;
    s_time_trial_timer = 0.0f;
    s_crystal_collected = 0;
    s_exit_reached = 0;
    s_secret_exit_reached = 0;
    s_boss_invuln = 0.0f;
    g_stats.crates_broken = 0;

    /* Check if this is a boss level (0x06 Tiny, 0x03 Dingo, 0x04 Tropy, 0x05 NGin, 0x07 Cortex) */
    if (level_id == 0x06 || level_id == 0x03 || level_id == 0x04 || level_id == 0x05 || level_id == 0x07) {
        s_boss_level = 1;
        s_boss_idx = story_get_chamber(level_id);
        s_boss_hp = (level_id == 0x05 || level_id == 0x07) ? 5 : 3;
        s_boss_max_hp = s_boss_hp;
        g_stats.crates_total = 0;

        /* Spawn Boss Entity */
        float bx = px + sinf(yaw) * 220.0f;
        float bz = pz + cosf(yaw) * 220.0f;
        int gh = level_height_below(bx, bz, py + 48.0f);
        float by = (gh > -999999) ? (float)gh : py;

        prop_item *boss = &s_props[s_prop_count++];
        boss->type = PROP_BOSS_ENTITY;
        boss->x = bx;
        boss->y = by;
        boss->z = bz;
        boss->sx = boss->sz = 48.0f;
        boss->sy = 64.0f;
        boss->active = 1;
        boss->hp = s_boss_hp;
        boss->timer = 0.0f;
        boss->attack_timer = 0.0f;
        boss->rot_y = yaw + 3.14159f;
        return;
    }

    s_boss_level = 0;
    s_boss_idx = -1;
    s_boss_hp = 0;

    /* Check if authentic ZDAT entities are available for this level */
    const level_entity_table_entry *table_entry = level_entities_find(level_id);
    int has_crystal = (level_id < 64 && g_save.levels[level_id].crystal);

    if (table_entry && table_entry->count > 0) {
        int total_crates = 0;

        for (int i = 0; i < table_entry->count && s_prop_count < MAX_PROPS; i++) {
            const level_entity_def *ent = &table_entry->entities[i];

            /* CANONICAL TIME TRIAL STOPWATCH RULE:
             * The clock ONLY spawns if the player has ALREADY obtained the Power Crystal.
             * On a player's first playthrough of the level, the stopwatch NEVER spawns! */
            if (ent->type == PROP_TIME_CLOCK) {
                if (!has_crystal) {
                    continue; /* Do not spawn clock */
                }
            }

            prop_item *it = &s_props[s_prop_count++];
            it->type = ent->type;
            it->x = ent->x;
            it->y = ent->y;
            it->z = ent->z;
            it->active = 1;
            it->rot_y = 0.0f;
            it->timer = 0.0f;
            it->armed = 0;
            it->value = ent->value;
            it->hp = (ent->type >= PROP_ENEMY_KNIGHT) ? 1 : 0;
            it->attack_timer = 0.0f;

            /* ZDAT heights include stacks, suspended fruit and bonus paths.
             * Snapping each entity to WGEO collapses those authored layouts. */

            if (it->type == PROP_WUMPA) {
                it->sx = it->sy = it->sz = 14.0f;
                it->y += 14.0f;
            } else if (it->type == PROP_CRYSTAL) {
                it->sx = it->sy = it->sz = 22.0f;
                it->y += 18.0f;
            } else if (it->type == PROP_TIME_CLOCK) {
                it->sx = it->sy = it->sz = 20.0f;
                it->y += 14.0f;
            } else if (it->type == PROP_EXIT_PORTAL) {
                it->sx = it->sz = 52.0f; it->sy = 12.0f;
            } else if (it->type >= PROP_ENEMY_KNIGHT) {
                it->sx = it->sz = 28.0f; it->sy = 40.0f;
            } else {
                it->sx = it->sy = it->sz = 24.0f;
                total_crates++;
            }
        }

        g_stats.crates_total = (total_crates > 0) ? total_crates : story_get_canonical_crate_count(level_id);
        return;
    }

    /* Fallback procedural layout for custom/unmapped levels */
    g_stats.crates_total = story_get_canonical_crate_count(level_id);

    static const struct {
        float ox, oy, oz;
        PropType type;
        int value;
    } defs[] = {
        {  40.0f,  0.0f,  180.0f, PROP_CRATE_WOOD, 1 },
        { -40.0f,  0.0f,  180.0f, PROP_CRATE_WOOD, 1 },
        {   0.0f, 18.0f,  240.0f, PROP_WUMPA,      1 },
        {   0.0f, 18.0f,  280.0f, PROP_WUMPA,      1 },
        {  50.0f,  0.0f,  340.0f, PROP_CRATE_AKU,  1 },
        { -50.0f,  0.0f,  340.0f, PROP_CRATE_TNT,  1 },
        {   0.0f,  0.0f,  420.0f, PROP_CRATE_CHECKPOINT, 1 },
        {  45.0f,  0.0f,  500.0f, PROP_CRATE_SPRING, 1 },
        { -45.0f,  0.0f,  500.0f, PROP_CRATE_NITRO,  0 },
        {   0.0f, 24.0f,  640.0f, PROP_CRYSTAL,    0 },
        {  40.0f,  0.0f,  720.0f, PROP_CRATE_WOOD, 1 },
        { -40.0f,  0.0f,  720.0f, PROP_CRATE_WOOD, 1 },
        {  35.0f,  0.0f,  220.0f, PROP_TIME_CLOCK, 0 },
        {   0.0f,  0.0f,  860.0f, PROP_EXIT_PORTAL, 0 }
    };

    int total_defs = sizeof(defs) / sizeof(defs[0]);

    for (int i = 0; i < total_defs && s_prop_count < MAX_PROPS; i++) {
        if (defs[i].type == PROP_TIME_CLOCK && !has_crystal)
            continue;

        float tx = px + defs[i].ox * cosf(yaw) + defs[i].oz * sinf(yaw);
        float tz = pz - defs[i].ox * sinf(yaw) + defs[i].oz * cosf(yaw);
        int gh = level_height_below(tx, tz, py + 48.0f);
        if (gh <= -999999)
            continue;

        float gy = (float)gh + defs[i].oy;
        prop_item *it = &s_props[s_prop_count++];
        it->type = defs[i].type;
        it->x = tx;
        it->y = gy;
        it->z = tz;
        it->active = 1;
        it->rot_y = 0.0f;
        it->timer = 0.0f;
        it->armed = 0;
        it->value = defs[i].value;

        if (it->type == PROP_WUMPA) {
            it->sx = it->sy = it->sz = 14.0f;
        } else if (it->type == PROP_CRYSTAL) {
            it->sx = it->sy = it->sz = 22.0f;
        } else if (it->type == PROP_TIME_CLOCK) {
            it->sx = it->sy = it->sz = 20.0f;
        } else if (it->type == PROP_EXIT_PORTAL) {
            it->sx = it->sz = 52.0f; it->sy = 12.0f;
        } else {
            it->sx = it->sy = it->sz = 24.0f;
        }
    }
}

void props_init_level(int level_id, float x, float y, float z)
{
    props_init_level_facing(level_id, x, y, z, 0.0f);
}

void props_spawn_projectile(float x, float y, float z, float dx, float dy, float dz)
{
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!s_projectiles[i].active) {
            prop_projectile *p = &s_projectiles[i];
            p->active = 1;
            p->x = x;
            p->y = y;
            p->z = z;
            float spd = 1400.0f;
            p->vx = dx * spd;
            p->vy = dy * spd;
            p->vz = dz * spd;
            p->life = 1.8f;
            audio_play_sfx(SFX_BAZOOKA);
            break;
        }
    }
}

void props_damage_boss(int damage)
{
    if (s_boss_idx < 0 || s_boss_hp <= 0 || s_boss_invuln > 0.0f)
        return;

    s_boss_hp -= damage;
    s_boss_invuln = 1.2f;
    audio_play_sfx(SFX_BOSS_HIT);

    for (int i = 0; i < s_prop_count; i++) {
        if (s_props[i].type == PROP_BOSS_ENTITY) {
            for (int k = 0; k < 20; k++) {
                spawn_particle(s_props[i].x, s_props[i].y + 30.0f, s_props[i].z,
                               1.0f, 0.2f, 0.1f, 5.0f);
            }
            break;
        }
    }

    if (s_boss_hp <= 0) {
        s_boss_hp = 0;
        audio_play_sfx(SFX_EXPLODE);
        audio_play_sfx(SFX_LEVEL_CLEAR);
        if (s_boss_idx >= 0) {
            story_on_boss_defeated(s_boss_idx);
        }
        s_exit_reached = 1;
    }
}

void props_update(float dt, char_state *player)
{
    /* Update particles */
    for (int i = 0; i < s_particle_count; i++) {
        prop_particle *p = &s_particles[i];
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->z += p->vz * dt;
        p->vy -= 980.0f * dt;
        p->life -= dt;
        if (p->life <= 0.0f) {
            s_particles[i] = s_particles[--s_particle_count];
            i--;
        }
    }

    /* Update Time Trial clock */
    if (s_time_trial_active) {
        s_time_trial_timer += dt;
    }

    /* Update Aku Aku invincibility timer */
    if (g_stats.aku_timer > 0.0f) {
        g_stats.aku_timer -= dt;
        if (g_stats.aku_timer <= 0.0f) {
            g_stats.aku_timer = 0.0f;
            g_stats.aku_aku_tier = 2;
        }
    }

    /* Update Boss invulnerability */
    if (s_boss_invuln > 0.0f) {
        s_boss_invuln -= dt;
        if (s_boss_invuln < 0.0f)
            s_boss_invuln = 0.0f;
    }

    /* Update Fruit Bazooka projectiles */
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        prop_projectile *p = &s_projectiles[i];
        if (!p->active) continue;

        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->z += p->vz * dt;
        p->life -= dt;

        /* Trail particle */
        if (s_particle_count < MAX_PARTICLES) {
            prop_particle *part = &s_particles[s_particle_count++];
            part->x = p->x; part->y = p->y; part->z = p->z;
            part->vx = 0; part->vy = 10.0f; part->vz = 0;
            part->r = 0.98f; part->g = 0.85f; part->b = 0.20f;
            part->life = 0.20f;
            part->size = 2.5f;
        }

        /* Check collision with crates and enemies */
        for (int k = 0; k < s_prop_count; k++) {
            prop_item *it = &s_props[k];
            if (!it->active) continue;
            float dx = p->x - it->x, dy = p->y - it->y, dz = p->z - it->z;
            float d2 = dx * dx + dy * dy + dz * dz;

            if (it->type == PROP_BOSS_ENTITY) {
                if (d2 < 60.0f * 60.0f) {
                    props_damage_boss(1);
                    p->active = 0;
                    break;
                }
            } else if (it->type >= PROP_CRATE_WOOD && it->type <= PROP_CRATE_SWITCH) {
                if (d2 < 32.0f * 32.0f) {
                    break_crate(it);
                    p->active = 0;
                    break;
                }
            } else if (it->type >= PROP_ENEMY_KNIGHT) {
                if (d2 < 36.0f * 36.0f) {
                    it->active = 0;
                    p->active = 0;
                    audio_play_sfx(SFX_CRATE_BREAK);
                    for (int m = 0; m < 12; m++)
                        spawn_particle(it->x, it->y + 16.0f, it->z, 0.9f, 0.8f, 0.2f, 4.0f);
                    break;
                }
            }
        }

        if (p->life <= 0.0f)
            p->active = 0;
    }

    /* Vortex energy emission */
    if (s_vortex.active && s_vortex.intensity > 0.05f) {
        s_vortex.time += dt;
        for (int k = 0; k < 2; k++) {
            if (s_particle_count >= MAX_PARTICLES)
                break;
            float ang = ((float)(rand() % 100) / 100.0f) * 6.2831853f;
            float rad = (8.0f + (float)(rand() % 24)) * s_vortex.intensity;
            prop_particle *p = &s_particles[s_particle_count++];
            p->x = s_vortex.x + cosf(ang) * rad;
            p->y = s_vortex.y;
            p->z = s_vortex.z + sinf(ang) * rad;
            p->vx = -sinf(ang) * 50.0f;
            p->vy = -60.0f - (float)(rand() % 50);
            p->vz = cosf(ang) * 50.0f;
            int col = rand() % 3;
            if (col == 0) { p->r = 0.2f; p->g = 0.85f; p->b = 1.0f; }
            else if (col == 1) { p->r = 0.95f; p->g = 0.85f; p->b = 0.2f; }
            else { p->r = 0.75f; p->g = 0.25f; p->b = 0.95f; }
            p->life = 0.4f + ((float)(rand() % 20) / 100.0f);
            p->size = 3.5f;
        }
    }

    if (!player)
        return;

    for (int i = 0; i < s_prop_count; i++) {
        prop_item *it = &s_props[i];
        if (!it->active)
            continue;

        it->rot_y += 3.0f * dt;
        if (it->type != PROP_CRATE_TNT || it->armed)
            it->timer += dt;

        float dx = player->x - it->x;
        float dy = player->y - it->y;
        float dz = player->z - it->z;
        float dist_xz = sqrtf(dx * dx + dz * dz);

        /* TNT tick */
        if (it->type == PROP_CRATE_TNT && it->armed) {
            if (it->timer >= 3.0f) {
                break_crate(it);
                if (dist_xz < 90.0f) {
                    player->vx = (dx / (dist_xz + 0.1f)) * 400.0f;
                    player->vz = (dz / (dist_xz + 0.1f)) * 400.0f;
                    player->vy = 300.0f;
                }
                continue;
            }
        }

        /* Wumpa Fruit pickup */
        if (it->type == PROP_WUMPA) {
            if (dist_xz < 26.0f && fabsf(dy) < 35.0f) {
                it->active = 0;
                audio_play_sfx(SFX_WUMPA);
                g_stats.wumpa_count++;
                if (g_stats.wumpa_count >= 100) {
                    g_stats.wumpa_count = 0;
                    g_stats.lives++;
                }
                for (int k = 0; k < 6; k++) {
                    spawn_particle(it->x, it->y, it->z, 1.0f, 0.85f, 0.2f, 3.0f);
                }
            }
            continue;
        }

        /* Power Crystal pickup */
        if (it->type == PROP_CRYSTAL) {
            if (dist_xz < 36.0f && fabsf(dy) < 45.0f) {
                it->active = 0;
                s_crystal_collected = 1;
                audio_play_sfx(SFX_CRYSTAL);
                for (int k = 0; k < 16; k++) {
                    spawn_particle(it->x, it->y + 10.0f, it->z, 0.95f, 0.25f, 0.98f, 4.5f);
                }
            }
            continue;
        }

        /* Time Trial Clock pickup */
        if (it->type == PROP_TIME_CLOCK) {
            if (dist_xz < 28.0f && fabsf(dy) < 36.0f) {
                it->active = 0;
                s_time_trial_active = 1;
                s_time_trial_timer = 0.0f;
                audio_play_sfx(SFX_MENU_SELECT);
                for (int k = 0; k < 14; k++) {
                    spawn_particle(it->x, it->y + 8.0f, it->z, 0.98f, 0.85f, 0.15f, 4.0f);
                }
            }
            continue;
        }

        /* Exit Portal teleporter */
        if (it->type == PROP_EXIT_PORTAL) {
            if (dist_xz < 44.0f && fabsf(dy) < 35.0f) {
                s_exit_reached = 1;
                if (it->value != 0)
                    s_secret_exit_reached = 1;
            }
            continue;
        }

        /* Enemy logic */
        if (it->type >= PROP_ENEMY_KNIGHT) {
            if (dist_xz < 36.0f && fabsf(dy) < 35.0f) {
                /* Attack with spin or slide destroys enemy */
                if (player->spin_timer > 0.0f || player->is_sliding || g_stats.aku_timer > 0.0f) {
                    it->active = 0;
                    audio_play_sfx(SFX_CRATE_BREAK);
                    for (int m = 0; m < 12; m++)
                        spawn_particle(it->x, it->y + 16.0f, it->z, 0.95f, 0.85f, 0.2f, 4.0f);
                } else if (player->vy < -40.0f && dy > 12.0f) {
                    /* Jump attack onto enemy */
                    it->active = 0;
                    player->vy = 480.0f;
                    player->onground = 0;
                    audio_play_sfx(SFX_CRATE_BREAK);
                    for (int m = 0; m < 12; m++)
                        spawn_particle(it->x, it->y + 16.0f, it->z, 0.95f, 0.85f, 0.2f, 4.0f);
                } else {
                    /* Take hit from enemy */
                    if (g_stats.aku_aku_tier > 0) {
                        g_stats.aku_aku_tier--;
                        audio_play_sfx(SFX_BOSS_HIT);
                        player->vx = (dx / (dist_xz + 0.1f)) * 320.0f;
                        player->vz = (dz / (dist_xz + 0.1f)) * 320.0f;
                        player->vy = 260.0f;
                    }
                }
            }
            continue;
        }

        /* Boss Entity logic */
        if (it->type == PROP_BOSS_ENTITY) {
            it->attack_timer += dt;
            it->rot_y = atan2f(player->x - it->x, player->z - it->z);

            /* Boss attack / jump wave */
            if (it->attack_timer > 2.8f) {
                it->attack_timer = 0.0f;
                float ang = (float)(rand() % 100) / 100.0f * 6.2831853f;
                it->x += cosf(ang) * 45.0f;
                it->z += sinf(ang) * 45.0f;
                audio_play_sfx(SFX_JUMP);
            }

            /* Boss collision */
            if (dist_xz < 48.0f && fabsf(dy) < 40.0f) {
                if (player->spin_timer > 0.0f || player->is_sliding) {
                    props_damage_boss(1);
                    player->vx = (dx / (dist_xz + 0.1f)) * 360.0f;
                    player->vz = (dz / (dist_xz + 0.1f)) * 360.0f;
                    player->vy = 320.0f;
                } else if (g_stats.aku_timer > 0.0f) {
                    props_damage_boss(1);
                } else if (s_boss_invuln <= 0.0f) {
                    if (g_stats.aku_aku_tier > 0) {
                        g_stats.aku_aku_tier--;
                        audio_play_sfx(SFX_BOSS_HIT);
                        player->vx = (dx / (dist_xz + 0.1f)) * 350.0f;
                        player->vz = (dz / (dist_xz + 0.1f)) * 350.0f;
                        player->vy = 280.0f;
                    }
                }
            }
            continue;
        }

        /* Crates collision & interaction */
        float crate_radius = it->sx * 0.5f;
        if (dist_xz < crate_radius + 18.0f) {
            /* 1. Spin attack or Slide attack */
            if (player->spin_timer > 0.0f || player->is_sliding || g_stats.aku_timer > 0.0f) {
                if (fabsf(dy) < it->sy * 0.8f) {
                    break_crate(it);
                    if (it->type == PROP_CRATE_SPRING) {
                        player->vy = 660.0f;
                        player->onground = 0;
                    }
                    continue;
                }
            }

            /* 2. Jump bounce from top */
            float top_y = it->y + it->sy;
            if (player->vy < 0.0f && dy >= it->sy - 12.0f && dy <= it->sy + 24.0f) {
                player->y = top_y;
                player->vy = (it->type == PROP_CRATE_SPRING) ? 660.0f : 420.0f;
                player->onground = 0;
                audio_play_sfx(SFX_JUMP);

                if (it->type == PROP_CRATE_TNT) {
                    it->armed = 1;
                    audio_play_sfx(SFX_TNT_TICK);
                    for (int k = 0; k < 6; k++)
                        spawn_particle(it->x, top_y, it->z, 1.0f, 0.4f, 0.1f, 3.5f);
                } else if (it->type == PROP_CRATE_NITRO) {
                    break_crate(it);
                } else if (it->type == PROP_CRATE_BOUNCE) {
                    it->value--;
                    g_stats.wumpa_count += 2;
                    audio_play_sfx(SFX_WUMPA);
                    if (it->value <= 0) break_crate(it);
                } else {
                    break_crate(it);
                }
                continue;
            }

            /* 3. Horizontal obstacle collision */
            if (fabsf(dy) < it->sy * 0.7f && dist_xz > 0.1f) {
                float push = (crate_radius + 16.0f) - dist_xz;
                player->x += (dx / dist_xz) * push;
                player->z += (dz / dist_xz) * push;
            }
        }
    }
}

/* Helper to append a colored cube */
static void add_cube(level_vertex *out, int *n, int cap,
                     float cx, float cy, float cz,
                     float sx, float sy, float sz,
                     float r, float g, float b)
{
    float hx = sx * 0.5f, hy = sy * 0.5f, hz = sz * 0.5f;
    float corners[8][3] = {
        { cx - hx, cy - hy, cz - hz }, { cx + hx, cy - hy, cz - hz },
        { cx + hx, cy + hy, cz - hz }, { cx - hx, cy + hy, cz - hz },
        { cx - hx, cy - hy, cz + hz }, { cx + hx, cy - hy, cz + hz },
        { cx + hx, cy + hy, cz + hz }, { cx - hx, cy + hy, cz + hz }
    };

    static const int faces[6][4] = {
        {0, 1, 2, 3}, {4, 7, 6, 5}, {0, 4, 5, 1},
        {3, 2, 6, 7}, {0, 3, 7, 4}, {1, 5, 6, 2}
    };

    for (int f = 0; f < 6; f++) {
        float fr = r, fg = g, fb = b;
        if (f == 3) { fr *= 1.25f; fg *= 1.25f; fb *= 1.25f; } /* top */
        else if (f == 2) { fr *= 0.65f; fg *= 0.65f; fb *= 0.65f; } /* bottom */
        else if (f == 4 || f == 5) { fr *= 0.85f; fg *= 0.85f; fb *= 0.85f; }
        if (fr > 1.0f) fr = 1.0f;
        if (fg > 1.0f) fg = 1.0f;
        if (fb > 1.0f) fb = 1.0f;

        const int *q = faces[f];
        const int order[6] = { q[0], q[1], q[2], q[0], q[2], q[3] };
        for (int v = 0; v < 6; v++) {
            if (*n + 3 > cap && v % 3 == 0) return;
            level_vertex *o = &out[(*n)++];
            o->x = corners[order[v]][0];
            o->y = corners[order[v]][1];
            o->z = corners[order[v]][2];
            o->r = fr; o->g = fg; o->b = fb;
        }
    }
}

/* Helper to append 3D Power Crystal */
static void add_crystal(level_vertex *out, int *n, int cap, float cx, float cy, float cz, float rot_y)
{
    float rad = 11.0f;
    float top_y = cy + 16.0f;
    float bot_y = cy - 16.0f;
    float pts[4][3];
    for (int i = 0; i < 4; i++) {
        float ang = rot_y + (float)i * 1.5707963f;
        pts[i][0] = cx + cosf(ang) * rad;
        pts[i][1] = cy;
        pts[i][2] = cz + sinf(ang) * rad;
    }

    for (int i = 0; i < 4; i++) {
        int next = (i + 1) % 4;
        if (*n + 6 > cap) return;
        out[(*n)++] = (level_vertex){ cx, top_y, cz, 0.98f, 0.35f, 1.0f };
        out[(*n)++] = (level_vertex){ pts[i][0], pts[i][1], pts[i][2], 0.85f, 0.15f, 0.95f };
        out[(*n)++] = (level_vertex){ pts[next][0], pts[next][1], pts[next][2], 0.92f, 0.25f, 0.98f };

        out[(*n)++] = (level_vertex){ cx, bot_y, cz, 0.70f, 0.10f, 0.80f };
        out[(*n)++] = (level_vertex){ pts[next][0], pts[next][1], pts[next][2], 0.85f, 0.15f, 0.95f };
        out[(*n)++] = (level_vertex){ pts[i][0], pts[i][1], pts[i][2], 0.75f, 0.12f, 0.85f };
    }
}

/* Helper to append 3D Wumpa fruit */
static void add_wumpa(level_vertex *out, int *n, int cap,
                      float cx, float cy, float cz, float rot_y, float bob)
{
    float y_pos = cy + sinf(bob) * 3.0f;
    float top_y = y_pos + 7.0f;
    float bot_y = y_pos - 7.0f;
    float rad = 7.0f;
    float pts[4][3];
    for (int i = 0; i < 4; i++) {
        float ang = rot_y + (float)i * 1.5707963f;
        pts[i][0] = cx + cosf(ang) * rad;
        pts[i][1] = y_pos;
        pts[i][2] = cz + sinf(ang) * rad;
    }

    for (int i = 0; i < 4; i++) {
        int next = (i + 1) % 4;
        if (*n + 3 > cap) return;
        out[(*n)++] = (level_vertex){ cx, top_y, cz, 0.98f, 0.65f, 0.15f };
        out[(*n)++] = (level_vertex){ pts[i][0], pts[i][1], pts[i][2], 0.95f, 0.45f, 0.10f };
        out[(*n)++] = (level_vertex){ pts[next][0], pts[next][1], pts[next][2], 0.98f, 0.55f, 0.12f };
    }

    for (int i = 0; i < 4; i++) {
        int next = (i + 1) % 4;
        if (*n + 3 > cap) return;
        out[(*n)++] = (level_vertex){ cx, bot_y, cz, 0.85f, 0.25f, 0.10f };
        out[(*n)++] = (level_vertex){ pts[next][0], pts[next][1], pts[next][2], 0.90f, 0.35f, 0.10f };
        out[(*n)++] = (level_vertex){ pts[i][0], pts[i][1], pts[i][2], 0.88f, 0.30f, 0.10f };
    }

    if (*n + 3 <= cap) {
        out[(*n)++] = (level_vertex){ cx, top_y + 4.0f, cz, 0.20f, 0.85f, 0.25f };
        out[(*n)++] = (level_vertex){ cx + 4.0f, top_y + 2.0f, cz, 0.15f, 0.75f, 0.20f };
        out[(*n)++] = (level_vertex){ cx, top_y, cz, 0.10f, 0.65f, 0.15f };
    }
}

static int build_props_mesh(level_vertex *out, int cap, int include_objects,uint32_t textured_types)
{
    if (!out || cap < 3)
        return 0;
    cap -= cap % 3;
    int n = 0;

    /* Render active props */
    for (int i = 0; include_objects && i < s_prop_count; i++) {
        const prop_item *it = &s_props[i];
        if (!it->active)
            continue;

        if (include_objects==2 && it->type<32 && (textured_types & (1u<<it->type)))
            continue;

        if (it->type == PROP_WUMPA) {
            add_wumpa(out, &n, cap, it->x, it->y, it->z, it->rot_y, it->timer * 4.0f);
        } else if (it->type == PROP_CRYSTAL) {
            add_crystal(out, &n, cap, it->x, it->y + 12.0f, it->z, it->rot_y);
        } else if (it->type == PROP_TIME_CLOCK) {
            add_cube(out, &n, cap, it->x, it->y + 10.0f, it->z, 16.0f, 16.0f, 6.0f, 0.98f, 0.85f, 0.15f);
        } else if (it->type == PROP_EXIT_PORTAL) {
            add_cube(out, &n, cap, it->x, it->y + 4.0f, it->z, 48.0f, 8.0f, 48.0f, 0.25f, 0.75f, 0.98f);
        } else if (it->type == PROP_WARP_PORTAL) {
            add_cube(out, &n, cap, it->x - 20.0f, it->y + 24.0f, it->z, 8.0f, 48.0f, 8.0f, 0.85f, 0.70f, 0.25f);
            add_cube(out, &n, cap, it->x + 20.0f, it->y + 24.0f, it->z, 8.0f, 48.0f, 8.0f, 0.85f, 0.70f, 0.25f);
            add_cube(out, &n, cap, it->x, it->y + 50.0f, it->z, 48.0f, 8.0f, 8.0f, 0.95f, 0.80f, 0.30f);
            add_cube(out, &n, cap, it->x, it->y + 24.0f, it->z, 32.0f, 40.0f, 4.0f, 0.15f, 0.65f, 0.98f);
            if (it->value < 64 && g_save.levels[it->value].crystal) {
                add_crystal(out, &n, cap, it->x, it->y + 64.0f, it->z, it->timer * 2.0f);
            }
        } else if (it->type == PROP_BOSS_ENTITY) {
            float br = (s_boss_invuln > 0.0f) ? 1.0f : 0.85f;
            float bg = (s_boss_invuln > 0.0f) ? 1.0f : 0.25f;
            float bb = (s_boss_invuln > 0.0f) ? 1.0f : 0.15f;
            add_cube(out, &n, cap, it->x, it->y + 28.0f, it->z, 40.0f, 36.0f, 32.0f, br, bg, bb);
            add_cube(out, &n, cap, it->x, it->y + 54.0f, it->z, 22.0f, 22.0f, 22.0f, 0.95f, 0.75f, 0.60f);
        } else if (it->type >= PROP_ENEMY_KNIGHT) {
            float er = 0.85f, eg = 0.35f, eb = 0.25f;
            if (it->type == PROP_ENEMY_CHICKEN) { er = 0.95f; eg = 0.90f; eb = 0.80f; }
            else if (it->type == PROP_ENEMY_GOAT) { er = 0.75f; eg = 0.60f; eb = 0.45f; }
            else if (it->type == PROP_ENEMY_FROG) { er = 0.20f; eg = 0.85f; eb = 0.25f; }
            else if (it->type == PROP_ENEMY_SWORD) { er = 0.65f; eg = 0.65f; eb = 0.70f; }
            else if (it->type == PROP_ENEMY_DIVER) { er = 0.95f; eg = 0.80f; eb = 0.10f; }
            else if (it->type == PROP_ENEMY_MUMMY) { er = 0.85f; eg = 0.80f; eb = 0.65f; }
            else if (it->type == PROP_ENEMY_SCORPION) { er = 0.75f; eg = 0.25f; eb = 0.15f; }
            else if (it->type == PROP_ENEMY_WIZARD) { er = 0.55f; eg = 0.15f; eb = 0.85f; }
            else if (it->type == PROP_ENEMY_ROBOT) { er = 0.20f; eg = 0.70f; eb = 0.85f; }
            add_cube(out, &n, cap, it->x, it->y + it->sy * 0.5f, it->z, it->sx, it->sy, it->sz, er, eg, eb);
        } else {
            /* Crates */
            float cr = 0.82f, cg = 0.48f, cb = 0.16f;
            if (it->type == PROP_CRATE_TNT) {
                if (it->armed && ((int)(it->timer * 8.0f) % 2 == 0)) {
                    cr = 1.0f; cg = 0.9f; cb = 0.9f;
                } else {
                    cr = 0.88f; cg = 0.15f; cb = 0.12f;
                }
            } else if (it->type == PROP_CRATE_NITRO) {
                cr = 0.15f; cg = 0.88f; cb = 0.20f;
            } else if (it->type == PROP_CRATE_CHECKPOINT) {
                cr = 0.75f; cg = 0.80f; cb = 0.88f;
            } else if (it->type == PROP_CRATE_AKU) {
                cr = 0.95f; cg = 0.65f; cb = 0.15f;
            } else if (it->type == PROP_CRATE_SPRING) {
                cr = 0.98f; cg = 0.90f; cb = 0.20f;
            } else if (it->type == PROP_CRATE_PICKUP) {
                cr = 0.95f; cg = 0.75f; cb = 0.15f;
            } else if (it->type == PROP_CRATE_BOUNCE) {
                cr = 0.90f; cg = 0.55f; cb = 0.18f;
            } else if (it->type == PROP_CRATE_SLOT) {
                cr = 0.80f; cg = 0.40f; cb = 0.85f;
            } else if (it->type == PROP_CRATE_LIFE) {
                cr = 0.95f; cg = 0.50f; cb = 0.60f;
            } else if (it->type == PROP_CRATE_SWITCH) {
                cr = 0.70f; cg = 0.70f; cb = 0.75f;
            }

            add_cube(out, &n, cap, it->x, it->y + it->sy * 0.5f, it->z,
                     it->sx, it->sy, it->sz, cr, cg, cb);
        }
    }

    /* Render Fruit Bazooka projectiles */
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        const prop_projectile *p = &s_projectiles[i];
        if (!p->active) continue;
        add_cube(out, &n, cap, p->x, p->y, p->z, 10.0f, 10.0f, 10.0f, 0.98f, 0.45f, 0.10f);
    }

    /* Render particles */
    for (int i = 0; i < s_particle_count; i++) {
        const prop_particle *p = &s_particles[i];
        add_cube(out, &n, cap, p->x, p->y, p->z, p->size, p->size, p->size,
                 p->r, p->g, p->b);
    }

    /* Render Time Twister vortex portal ring */
    if (s_vortex.active && s_vortex.intensity > 0.05f) {
        float r_outer = 40.0f * s_vortex.intensity;
        float r_inner = 8.0f * s_vortex.intensity;
        int segs = 16;
        float rot = s_vortex.time * 6.0f;
        for (int i = 0; i < segs; i++) {
            if (n + 6 > cap) break;
            float a0 = rot + ((float)i / (float)segs) * 6.2831853f;
            float a1 = rot + ((float)(i + 1) / (float)segs) * 6.2831853f;
            float c0 = cosf(a0), s0 = sinf(a0);
            float c1 = cosf(a1), s1 = sinf(a1);

            float x_in0 = s_vortex.x + c0 * r_inner, z_in0 = s_vortex.z + s0 * r_inner;
            float x_out0 = s_vortex.x + c0 * r_outer, z_out0 = s_vortex.z + s0 * r_outer;
            float x_in1 = s_vortex.x + c1 * r_inner, z_in1 = s_vortex.z + s1 * r_inner;
            float x_out1 = s_vortex.x + c1 * r_outer, z_out1 = s_vortex.z + s1 * r_outer;

            float y_pos = s_vortex.y;
            float r_c = 0.25f + 0.4f * sinf(a0 * 2.0f);
            float g_c = 0.70f + 0.25f * cosf(a0);
            float b_c = 0.95f;

            out[n++] = (level_vertex){ x_in0, y_pos, z_in0, 1.0f, 1.0f, 1.0f };
            out[n++] = (level_vertex){ x_out0, y_pos, z_out0, r_c, g_c, b_c };
            out[n++] = (level_vertex){ x_out1, y_pos, z_out1, r_c, g_c, b_c };

            out[n++] = (level_vertex){ x_in0, y_pos, z_in0, 1.0f, 1.0f, 1.0f };
            out[n++] = (level_vertex){ x_out1, y_pos, z_out1, r_c, g_c, b_c };
            out[n++] = (level_vertex){ x_in1, y_pos, z_in1, 1.0f, 1.0f, 1.0f };
        }
    }

    return n;
}

int props_build_mesh(level_vertex *out, int cap) { return build_props_mesh(out, cap, 1,0); }
int props_build_missing_mesh(level_vertex *out,int cap,uint32_t types) { return build_props_mesh(out,cap,2,types); }
int props_build_untextured_mesh(level_vertex *out,int cap) {
    uint32_t types=0;
    for(unsigned i=1;i<32;i++)if(props_textured_asset_index(i)>=0)types|=1u<<i;
    return props_build_missing_mesh(out,cap,types);
}
int props_build_effects_mesh(level_vertex *out, int cap) { return build_props_mesh(out, cap, 0,0); }
int props_count(void) { return s_prop_count; }
const prop_item *props_get(int index) { return index >= 0 && index < s_prop_count ? &s_props[index] : NULL; }
int props_aku_visual(const char_state *player,float view_yaw,prop_item *out) {
    if (!player || !out || g_stats.aku_aku_tier<=0) return 0;
    memset(out,0,sizeof(*out));
    out->type=PROP_AKU_MASK;
    out->active=1;
    out->x=player->x-cosf(view_yaw)*30.0f;
    out->y=player->y+30.0f+sinf(player->anim_time*4.0f)*2.0f;
    out->z=player->z+sinf(view_yaw)*30.0f;
    out->sx=out->sy=out->sz=1.0f; /* WD10G retains original dimensions. */
    out->rot_y=view_yaw;
    out->timer=player->anim_time;
    return 1;
}
int props_is_time_trial(void) { return s_time_trial_active; }
float props_get_time_trial_timer(void) { return s_time_trial_timer; }
uint32_t props_get_time_trial_ms(void) { return (uint32_t)(s_time_trial_timer * 1000.0f); }
int props_crystal_collected(void) { return s_crystal_collected; }
int props_is_crystal_collected(void) { return s_crystal_collected; }
int props_is_secret_exit(void) { return s_secret_exit_reached; }
int props_get_broken_crates(void) { return g_stats.crates_broken; }
int props_get_total_crates(void) { return g_stats.crates_total; }
int props_check_exit(float px, float py, float pz) {
    if (s_exit_reached) return 1;
    for (int i = 0; i < s_prop_count; i++) {
        const prop_item *it = &s_props[i];
        if (!it->active || it->type != PROP_EXIT_PORTAL)
            continue;
        float dx = px - it->x;
        float dz = pz - it->z;
        float dy = py - it->y;
        if (sqrtf(dx * dx + dz * dz) < 44.0f && fabsf(dy) < 35.0f) {
            s_exit_reached = 1;
            if (it->value != 0) s_secret_exit_reached = 1;
            return 1;
        }
    }
    return 0;
}
int props_is_boss_level(void) { return s_boss_level; }
int props_boss_active(void) { return s_boss_level && s_boss_hp > 0; }
int props_boss_hp(void) { return s_boss_hp; }
int props_get_boss_hp(void) { return s_boss_hp; }
int props_get_boss_max_hp(void) { return s_boss_max_hp; }

int props_get_warp_target(float px, float py, float pz)
{
    for (int i = 0; i < s_prop_count; i++) {
        const prop_item *it = &s_props[i];
        if (!it->active || it->type != PROP_WARP_PORTAL)
            continue;
        float dx = px - it->x;
        float dz = pz - it->z;
        float dy = py - it->y;
        if (sqrtf(dx * dx + dz * dz) < 36.0f && fabsf(dy) < 45.0f) {
            return it->value;
        }
    }
    return -1;
}

const prop_item *props_get_nearest_portal(float px, float py, float pz, float *out_dist)
{
    (void)py;
    const prop_item *best = NULL;
    float best_d = 1e9f;
    for (int i = 0; i < s_prop_count; i++) {
        const prop_item *it = &s_props[i];
        if (!it->active || it->type != PROP_WARP_PORTAL)
            continue;
        float dx = px - it->x;
        float dz = pz - it->z;
        float d = sqrtf(dx * dx + dz * dz);
        if (d < best_d) {
            best_d = d;
            best = it;
        }
    }
    if (out_dist) *out_dist = best_d;
    return best;
}
