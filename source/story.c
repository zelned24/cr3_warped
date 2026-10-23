/* Crash Bandicoot: Warped (PS1) -> Nintendo 3DS Port
 * Story Mode, Progression, Save Data & Warp Room State Machine.
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stddef.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include "story.h"
#include "audio.h"

#ifndef STORY_SAVE_ROOT
#define STORY_SAVE_ROOT "sdmc:/3ds/crash3"
#endif
#define SAVE_PATH_DIR   STORY_SAVE_ROOT
#define SAVE_FILE_PATH  STORY_SAVE_ROOT "/save.dat"
#define SAVE_TMP_PATH   STORY_SAVE_ROOT "/save.tmp"
#define SAVE_BACKUP_PATH STORY_SAVE_ROOT "/save.bak"
#define SAVE_LEGACY_PATH STORY_SAVE_ROOT "/crash3_save.dat"
#define SAVE_LEGACY_BACKUP_PATH STORY_SAVE_ROOT "/crash3_save.bak"

SaveData g_save;

static int read_save_file(const char *path, SaveData *result);

/* One descriptor per hub entrance. IDs are port-local, not original GOOL
 * placement IDs. The five basement thresholds retain the previous policy;
 * their provenance still needs verification against original scripts.
 * There is no sixth basement boss/Hot Coco portal in this table. */
static const StoryPortal s_portals[] = {
    {  0, 0x0B, 0, 0, STORY_ENTRY_NORMAL, 0 },
    {  1, 0x0E, 0, 1, STORY_ENTRY_NORMAL, 0 },
    {  2, 0x0A, 0, 2, STORY_ENTRY_NORMAL, 0 },
    {  3, 0x0C, 0, 3, STORY_ENTRY_NORMAL, 0 },
    {  4, 0x19, 0, 4, STORY_ENTRY_NORMAL, 0 },
    {  5, 0x06, 0, 5, STORY_ENTRY_BOSS, 0 },
    {  6, 0x0F, 1, 0, STORY_ENTRY_NORMAL, 0 },
    {  7, 0x16, 1, 1, STORY_ENTRY_NORMAL, 0 },
    {  8, 0x15, 1, 2, STORY_ENTRY_NORMAL, 0 },
    {  9, 0x12, 1, 3, STORY_ENTRY_NORMAL, 0 },
    { 10, 0x11, 1, 4, STORY_ENTRY_NORMAL, 0 },
    { 11, 0x03, 1, 5, STORY_ENTRY_BOSS, 0 },
    { 12, 0x10, 2, 0, STORY_ENTRY_NORMAL, 0 },
    { 13, 0x1C, 2, 1, STORY_ENTRY_NORMAL, 0 },
    { 14, 0x1A, 2, 2, STORY_ENTRY_NORMAL, 0 },
    { 15, 0x14, 2, 3, STORY_ENTRY_NORMAL, 0 },
    { 16, 0x1D, 2, 4, STORY_ENTRY_NORMAL, 0 },
    { 17, 0x04, 2, 5, STORY_ENTRY_BOSS, 0 },
    { 18, 0x1E, 3, 0, STORY_ENTRY_NORMAL, 0 },
    { 19, 0x13, 3, 1, STORY_ENTRY_NORMAL, 0 },
    { 20, 0x0D, 3, 2, STORY_ENTRY_NORMAL, 0 },
    { 21, 0x1B, 3, 3, STORY_ENTRY_NORMAL, 0 },
    { 22, 0x18, 3, 4, STORY_ENTRY_NORMAL, 0 },
    { 23, 0x05, 3, 5, STORY_ENTRY_BOSS, 0 },
    { 24, 0x23, 4, 0, STORY_ENTRY_NORMAL, 0 },
    { 25, 0x20, 4, 1, STORY_ENTRY_NORMAL, 0 },
    { 26, 0x22, 4, 2, STORY_ENTRY_NORMAL, 0 },
    { 27, 0x17, 4, 3, STORY_ENTRY_NORMAL, 0 },
    { 28, 0x24, 4, 4, STORY_ENTRY_NORMAL, 0 },
    { 29, 0x07, 4, 5, STORY_ENTRY_BOSS, 0 },
    { 30, 0x21, 5, 0, STORY_ENTRY_NORMAL, 5 },
    { 31, 0x16, 5, 1, STORY_ENTRY_HANG_EM_HIGH_SECRET, 10 },
    { 32, 0x25, 5, 2, STORY_ENTRY_NORMAL, 15 },
    { 33, 0x1B, 5, 3, STORY_ENTRY_FUTURE_FRENZY_SECRET, 20 },
    { 34, 0x1F, 5, 4, STORY_ENTRY_NORMAL, 25 },
};

/* Boss level IDs */
static const int s_boss_level_ids[TOTAL_BOSSES] = {
    0x06, /* Boss 1: Tiny Tiger */
    0x03, /* Boss 2: Dingodile */
    0x04, /* Boss 3: Dr. N. Tropy */
    0x05, /* Boss 4: Dr. N. Gin */
    0x07  /* Boss 5: Dr. Neo Cortex */
};

/* Powers awarded per boss defeat */
static const uint32_t s_boss_powers[TOTAL_BOSSES] = {
    POWER_BELLY_FLOP,
    POWER_DOUBLE_JUMP,
    POWER_TORNADO_SPIN,
    POWER_BAZOOKA,
    POWER_CRASH_DASH
};

/* ------------------------------------------------------------------ */
/* CRC32 calculation                                                   */
/* ------------------------------------------------------------------ */

static uint32_t compute_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1));
        }
    }
    return ~crc;
}

/* ------------------------------------------------------------------ */
/* Progression stats                                                   */
/* ------------------------------------------------------------------ */


typedef struct {
    int level_id;
    int crate_count;
    uint32_t sapphire_ms;
    uint32_t gold_ms;
    uint32_t platinum_ms;
} CanonicalLevelData;

static const CanonicalLevelData s_canonical_levels[] = {
    /* Chamber 1: Medieval */
    { 0x0B /* Toad Village */,   42,  63000,  57500,  44030 },
    { 0x0E /* Under Pressure */, 92, 106000,  77920,  70500 },
    { 0x0A /* Orient Express */, 51,  41000,  27800,  18100 },
    { 0x0C /* Bone Yard */,      66, 125000, 101000,  81000 },
    { 0x19 /* Makin' Waves */,   37,  68000,  58260,  53230 },
    /* Chamber 2: Arabian */
    { 0x0F /* Gee Wiz */,       100,  95000,  82730,  65900 },
    { 0x16 /* Hang'em High */,   96,  84000,  52630,  43800 },
    { 0x15 /* Hog Ride */,       86,  48000,  44430,  35030 },
    { 0x12 /* Tomb Time */,      95, 102000,  70260,  53900 },
    { 0x11 /* Midnight Run */,   35,  51000,  38230,  18200 },
    /* Chamber 3: Oriental */
    { 0x10 /* Dino Might! */,   112,  94000,  85760,  63000 },
    { 0x1C /* Deep Trouble */,   89,  97000,  85130,  78330 },
    { 0x1A /* High Time */,      85, 132000,  64100,  56930 },
    { 0x14 /* Road Crash */,     25,  85000,  80720,  77100 },
    { 0x1D /* Double Header */,  89,  87000,  81120,  59400 },
    /* Chamber 4: Egyptian */
    { 0x1E /* Sphynxinator */,  105, 102000,  82630,  56700 },
    { 0x13 /* Bye Bye Blimps */, 11,  68000,  58000,  51500 },
    { 0x0D /* Tell No Tales */,  61,  88000,  73000,  65230 },
    { 0x1B /* Future Frenzy */, 133, 121000,  94000,  79630 },
    { 0x18 /* Tomb Wader */,     88, 164000, 105030,  84000 },
    /* Chamber 5: Future */
    { 0x23 /* Gone Tomorrow */,  87, 125000,  85600,  62100 },
    { 0x20 /* Orange Asphalt */, 20,  98000,  93300,  81800 },
    { 0x22 /* Flaming Passion */,75, 103000,  73100,  59400 },
    { 0x17 /* Mad Bombers */,    11, 138000, 105000,  98130 },
    { 0x24 /* Bug Lite */,      120, 109000,  94860,  74900 },
    /* Secret Chamber (Neo-Cor) */
    { 0x21 /* Ski Crazed */,    100,  76000,  50500,  33300 },
    { 0x1F /* Rings of Power */, 33,  82000,  63440,  51730 },
    { 0x25 /* Area 51? */,        0, 113000, 109800, 104500 },
    { 0x26 /* Eggipus Rex */,     0,  85000,  78000,  77060 },
    { 0x27 /* Hot Coco */,       70,  75000,  67000,  62960 }
};

int story_get_canonical_crate_count(int level_id)
{
    for (size_t i = 0; i < sizeof(s_canonical_levels)/sizeof(s_canonical_levels[0]); i++) {
        if (s_canonical_levels[i].level_id == level_id)
            return s_canonical_levels[i].crate_count;
    }
    return 42;
}

/* Availability differs from the legacy getter's generic fallback. */
int story_has_relic_targets(int level_id)
{
    for(size_t i=0;i<sizeof(s_canonical_levels)/sizeof(s_canonical_levels[0]);i++)
        if(s_canonical_levels[i].level_id==level_id)
            return 1;
    return 0;
}

void story_get_canonical_relic_times(int level_id, uint32_t *out_sapphire, uint32_t *out_gold, uint32_t *out_platinum)
{
    for (size_t i = 0; i < sizeof(s_canonical_levels)/sizeof(s_canonical_levels[0]); i++) {
        if (s_canonical_levels[i].level_id == level_id) {
            if (out_sapphire) *out_sapphire = s_canonical_levels[i].sapphire_ms;
            if (out_gold) *out_gold = s_canonical_levels[i].gold_ms;
            if (out_platinum) *out_platinum = s_canonical_levels[i].platinum_ms;
            return;
        }
    }
    if (out_sapphire) *out_sapphire = 90000;
    if (out_gold) *out_gold = 70000;
    if (out_platinum) *out_platinum = 50000;
}

RelicTier story_get_relic_tier(int level_id, uint32_t time_ms)
{
    if (time_ms == 0)
        return RELIC_NONE;

    uint32_t s_ms = 0, g_ms = 0, p_ms = 0;
    story_get_canonical_relic_times(level_id, &s_ms, &g_ms, &p_ms);

    if (time_ms <= p_ms)
        return RELIC_PLATINUM;
    if (time_ms <= g_ms)
        return RELIC_GOLD;
    if (time_ms <= s_ms)
        return RELIC_SAPPHIRE;

    return RELIC_NONE;
}

uint32_t story_get_total_relics(void)
{
    return g_save.relics_count;
}

uint32_t story_get_relic_count_by_tier(RelicTier tier)
{
    if (tier == RELIC_NONE)
        return 0;
    uint32_t count = 0;
    for (int i = 0; i < 64; i++) {
        if (g_save.levels[i].relic_tier == (uint8_t)tier)
            count++;
    }
    return count;
}

void story_update_stats(void)
{
    uint32_t crystals = 0;
    uint32_t gems = 0;
    uint32_t relics = 0;

    for (int i = 0; i < 64; i++) {
        if (g_save.levels[i].crystal)
            crystals++;
        if (g_save.levels[i].clear_gem)
            gems++;
        if (g_save.levels[i].bonus_gem)
            gems++;
        if (g_save.levels[i].relic_tier != RELIC_NONE)
            relics++;
    }

    g_save.crystals_count = crystals;
    g_save.gems_count = gems;
    g_save.relics_count = relics;

    /* Prototype score: one point per collectible and boss.
     * Original percentage rules and per-level targets are not decoded yet. */
    uint32_t total_points = crystals + gems + relics;
    for (int b = 0; b < TOTAL_BOSSES; b++) {
        if (g_save.bosses_defeated[b])
            total_points++;
    }
    g_save.completion_pct = total_points;
    if (g_save.completion_pct > 105)
        g_save.completion_pct = 105;
}

int story_get_chamber_portal_count(int chamber_idx)
{
    if (chamber_idx < 0 || chamber_idx >= TOTAL_CHAMBERS)
        return 0;
    return chamber_idx < TOTAL_BOSSES ? LEVELS_PER_CHAMBER + 1 : LEVELS_PER_CHAMBER;
}

const StoryPortal *story_get_portal(int chamber_idx, int slot_idx)
{
    int count = story_get_chamber_portal_count(chamber_idx);
    if (slot_idx < 0 || slot_idx >= count)
        return NULL;
    return &s_portals[chamber_idx * (LEVELS_PER_CHAMBER + 1) + slot_idx];
}

const StoryPortal *story_get_portal_by_id(int portal_id)
{
    if (portal_id < 0 || (size_t)portal_id >= sizeof(s_portals)/sizeof(s_portals[0]))
        return NULL;
    return &s_portals[portal_id];
}

StoryPortalStatus story_resolve_portal(int chamber_idx, int slot_idx,
                                     const StoryPortal **out_portal)
{
    const StoryPortal *portal = story_get_portal(chamber_idx, slot_idx);
    if (out_portal)
        *out_portal = NULL;
    if (!portal)
        return STORY_PORTAL_INVALID;
    if (portal->entry_kind == STORY_ENTRY_BOSS) {
        if (!story_is_boss_unlocked(chamber_idx))
            return STORY_PORTAL_LOCKED;
    } else if (portal->required_relics) {
        if (g_save.relics_count < portal->required_relics)
            return STORY_PORTAL_LOCKED;
    } else if (!story_is_chamber_unlocked(chamber_idx)) {
        return STORY_PORTAL_LOCKED;
    }
    if (out_portal)
        *out_portal = portal;
    return STORY_PORTAL_READY;
}

StoryPortalStatus story_resolve_portal_id(int portal_id,
                                        const StoryPortal **out_portal)
{
    const StoryPortal *portal = story_get_portal_by_id(portal_id);
    if (!portal) {
        if (out_portal)
            *out_portal = NULL;
        return STORY_PORTAL_INVALID;
    }
    return story_resolve_portal(portal->chamber_idx, portal->slot_idx, out_portal);
}

int story_get_chamber(int level_id)
{
    /* First descriptor is the normal entrance when a level is shared. Use the
     * retained StoryPortal when returning from a secret entrance instead. */
    for (size_t i = 0; i < sizeof(s_portals)/sizeof(s_portals[0]); i++) {
        if (s_portals[i].level_id == level_id)
            return s_portals[i].chamber_idx;
    }
    return -1;
}

int story_get_chamber_crystal_count(int chamber_idx)
{
    if (chamber_idx < 0 || chamber_idx >= TOTAL_BOSSES)
        return 0;
    int count = 0;
    for (int l = 0; l < LEVELS_PER_CHAMBER; l++) {
        const StoryPortal *portal = story_get_portal(chamber_idx, l);
        if (g_save.levels[portal->level_id].crystal)
            count++;
    }
    return count;
}

int story_is_chamber_unlocked(int chamber_idx)
{
    if (chamber_idx < 0 || chamber_idx >= TOTAL_CHAMBERS)
        return 0;
    if (chamber_idx == 0)
        return 1; /* Chamber 1 (Medieval) is always unlocked */
    if (chamber_idx >= 1 && chamber_idx <= 4) {
        /* Chambers 2..5: previous chamber boss must be defeated */
        return g_save.bosses_defeated[chamber_idx - 1] != 0;
    }
    if (chamber_idx == 5) {
        /* Existing policy: basement access at 5 relics; native evidence pending. */
        return g_save.relics_count >= 5;
    }
    return 0;
}

int story_is_chamber_level_unlocked(int chamber_idx, int slot_idx)
{
    return story_resolve_portal(chamber_idx, slot_idx, NULL) == STORY_PORTAL_READY;
}

int story_is_level_unlocked(int level_id)
{
    if (level_id < 0 || level_id >= 64)
        return 0;
    if (level_id == 0x02)
        return 1; /* Warp Room hub is always unlocked */

    for (int b = 0; b < TOTAL_BOSSES; b++) {
        if (s_boss_level_ids[b] == level_id)
            return story_is_boss_unlocked(b);
    }

    /* Legacy archive-only access policy, not proof of original entrance rules.
     * Hot Coco/Eggipus discovery and the basement thresholds remain unverified.
     * Campaign hub callers must use story_resolve_portal, preserving identity:
     * - Level 26 (Ski Crazed, 0x21): 5 Relics
     * - Level 27 (Hang'em High, 0x16): 10 Relics (or Boss 0 for Chamber 2)
     * - Level 28 (Area 51?, 0x25): 15 Relics
     * - Level 29 (Future Frenzy, 0x1B): 20 Relics (or Boss 2 for Chamber 4)
     * - Level 30 (Rings of Power, 0x1F): 25 Relics
     * - Level 31 (Hot Coco, 0x27): 20 Relics
     * - Level 32 (Eggipus Rex, 0x26): 25 Relics
     */
    if (level_id == 0x21 /* Ski Crazed */)     return g_save.relics_count >= 5;
    if (level_id == 0x25 /* Area 51? */)       return g_save.relics_count >= 15;
    if (level_id == 0x1F /* Rings of Power */) return g_save.relics_count >= 25;
    if (level_id == 0x27 /* Hot Coco */)       return g_save.relics_count >= 20;
    if (level_id == 0x26 /* Eggipus Rex */)    return g_save.relics_count >= 25;
    if (level_id == 0x16 /* Hang'em High */)   return (g_save.bosses_defeated[0] != 0) || (g_save.relics_count >= 10);
    if (level_id == 0x1B /* Future Frenzy */)  return (g_save.bosses_defeated[2] != 0) || (g_save.relics_count >= 20);

    /* Main chambers 1..5: */
    int chamber = story_get_chamber(level_id);
    if (chamber == 0)
        return 1; /* Chamber 1 is always unlocked */
    if (chamber >= 1 && chamber <= 4)
        return g_save.bosses_defeated[chamber - 1] != 0;

    return 0;
}

int story_is_boss_unlocked(int boss_idx)
{
    if (boss_idx < 0 || boss_idx >= TOTAL_BOSSES)
        return 0;
    /* Boss unlocks once all 5 crystals in its chamber are gathered */
    return (boss_idx == 0 || g_save.bosses_defeated[boss_idx - 1]) &&
           story_get_chamber_crystal_count(boss_idx) >= LEVELS_PER_CHAMBER;
}

int story_get_boss_level_id(int boss_idx)
{
    if (boss_idx < 0 || boss_idx >= TOTAL_BOSSES)
        return -1;
    return s_boss_level_ids[boss_idx];
}

int story_has_power(uint32_t power_flag)
{
    return (g_save.unlocked_powers & power_flag) != 0;
}

void story_on_level_cleared(int level_id, int all_crates, uint32_t time_ms)
{
    if (level_id < 0 || level_id >= 64)
        return;

    /* Bosses, menus and cutscenes do not contain level collectibles. */
    int chamber = story_get_chamber(level_id);
    int is_main_level = 0;
    if (chamber >= 0 && chamber < 5) {
        /* Only Chambers 1..5 contain Power Crystals. Chamber 6 (Secret) has none. */
        for (int l = 0; l < LEVELS_PER_CHAMBER; l++)
            if (story_get_portal(chamber, l)->level_id == level_id)
                is_main_level = 1;
    }
    if (!is_main_level && (level_id < 0x1F || level_id > 0x27))
        return;
    for (int b = 0; b < TOTAL_BOSSES; b++)
        if (s_boss_level_ids[b] == level_id)
            return;

    int was_new_crystal = 0;
    if (is_main_level && !g_save.levels[level_id].crystal) {
        g_save.levels[level_id].crystal = 1;
        was_new_crystal = 1;
        audio_play_sfx(SFX_CRYSTAL);
    }

    if (all_crates) {
        g_save.levels[level_id].clear_gem = 1;
    }

    if (time_ms > 0) {
        if (g_save.levels[level_id].best_time_ms == 0 ||
            time_ms < g_save.levels[level_id].best_time_ms) {
            g_save.levels[level_id].best_time_ms = time_ms;
        }
        /* Preserve the strongest award when a later run is slower. */
        RelicTier earned = story_get_relic_tier(level_id, time_ms);
        if (earned > g_save.levels[level_id].relic_tier)
            g_save.levels[level_id].relic_tier = (uint8_t)earned;
    }

    story_update_stats();
    story_save();

    if (was_new_crystal) {
        int ch = story_get_chamber(level_id);
        if (ch >= 0 && story_is_boss_unlocked(ch)) {
            printf("[story] Chamber %d boss unlocked!\n", ch + 1);
        }
    }
}

void story_on_boss_defeated(int boss_idx)
{
    if (boss_idx < 0 || boss_idx >= TOTAL_BOSSES)
        return;

    g_save.bosses_defeated[boss_idx] = 1;
    g_save.unlocked_powers |= s_boss_powers[boss_idx];
    story_update_stats();
    story_save();

    printf("[story] Boss %d defeated! Unlocked power 0x%02lX. Total completion: %lu%%\n",
           boss_idx + 1, (unsigned long)s_boss_powers[boss_idx],
           (unsigned long)g_save.completion_pct);
}

void story_award_bonus_gem(int level_id)
{
    if (level_id < 0 || level_id >= 64)
        return;
    g_save.levels[level_id].bonus_gem = 1;
    story_update_stats();
    story_save();
}

GemColor story_get_level_colored_gem(int level_id)
{
    switch (level_id) {
        case 0x18: return GEM_COLOR_BLUE;    /* Level 20: Tomb Wader */
        case 0x1C: return GEM_COLOR_RED;     /* Level 12: Deep Trouble */
        case 0x22: return GEM_COLOR_GREEN;   /* Level 23: Flaming Passion */
        case 0x16: return GEM_COLOR_YELLOW;  /* Level 7: Hang'em High */
        case 0x1A: return GEM_COLOR_PURPLE;  /* Level 13: High Time */
        default:   return GEM_COLOR_NONE;
    }
}

const char *story_get_colored_gem_name(GemColor color)
{
    switch (color) {
        case GEM_COLOR_BLUE:   return "BLUE GEM";
        case GEM_COLOR_RED:    return "RED GEM";
        case GEM_COLOR_GREEN:  return "GREEN GEM";
        case GEM_COLOR_YELLOW: return "YELLOW GEM";
        case GEM_COLOR_PURPLE: return "PURPLE GEM";
        default:               return "CLEAR GEM";
    }
}

int story_has_colored_gem(GemColor color)
{
    switch (color) {
        case GEM_COLOR_BLUE:   return g_save.levels[0x18].bonus_gem;
        case GEM_COLOR_RED:    return g_save.levels[0x1C].bonus_gem;
        case GEM_COLOR_GREEN:  return g_save.levels[0x22].bonus_gem;
        case GEM_COLOR_YELLOW: return g_save.levels[0x16].bonus_gem;
        case GEM_COLOR_PURPLE: return g_save.levels[0x1A].bonus_gem;
        default:               return 0;
    }
}

/* ------------------------------------------------------------------ */
/* Save / Load persistence                                             */
/* ------------------------------------------------------------------ */

void story_init(void)
{
    memset(&g_save, 0, sizeof(g_save));
    memcpy(g_save.magic, "CB3S", 4);
    g_save.version = 1;

    /* Attempt to load existing save from SD */
    if (story_load() != 0) {
        printf("[story] No save file found on SD, initialized fresh story state.\n");
        story_update_stats();
    }
}

int story_save(void)
{
    /* Ensure save directory exists */
#ifdef _WIN32
    _mkdir(SAVE_PATH_DIR);
#else
    mkdir("sdmc:/3ds", 0777);
    mkdir(SAVE_PATH_DIR, 0777);
#endif

    story_update_stats();

    /* Compute CRC32 over all data before the crc32 field */
    size_t payload_size = offsetof(SaveData, crc32);
    g_save.crc32 = compute_crc32((const uint8_t *)&g_save, payload_size);

    /* Atomic save: write to temporary file, then rename */
    FILE *f = fopen(SAVE_TMP_PATH, "wb");
    if (!f) {
        printf("[story] Warning: cannot open %s for writing\n", SAVE_TMP_PATH);
        return -1;
    }

    size_t written = fwrite(&g_save, 1, sizeof(SaveData), f);
    int flush_failed = fflush(f) != 0;
    /* Commit the temporary generation before rotating recoverable files.
     * fflush only drains stdio; fsync invokes the SD device flush on 3DS. */
    int sync_failed = 0;
    if (written == sizeof(SaveData) && !flush_failed) {
#ifdef _WIN32
        sync_failed = _commit(_fileno(f)) != 0;
#else
        sync_failed = fsync(fileno(f)) != 0;
#endif
    }
    int close_failed = fclose(f) != 0;

    if (written != sizeof(SaveData) || flush_failed || sync_failed || close_failed) {
        remove(SAVE_TMP_PATH);
        printf("[story] Error: save write or flush failed (%u/%u bytes)\n",
               (unsigned)written, (unsigned)sizeof(SaveData));
        return -1;
    }

    /* FAT rename cannot replace an existing file. Keep the last valid
     * generation until the new file has been committed successfully. */
    SaveData previous;
    int had_previous = read_save_file(SAVE_FILE_PATH, &previous) == 0;
    if (had_previous) {
        remove(SAVE_BACKUP_PATH);
        if (rename(SAVE_FILE_PATH, SAVE_BACKUP_PATH) != 0) {
            remove(SAVE_TMP_PATH);
            return -1;
        }
    } else {
        /* A damaged current generation must not replace a valid backup. */
        remove(SAVE_FILE_PATH);
    }
    if (rename(SAVE_TMP_PATH, SAVE_FILE_PATH) != 0) {
        if (had_previous)
            rename(SAVE_BACKUP_PATH, SAVE_FILE_PATH);
        printf("[story] Error: rename failed from tmp to save file\n");
        return -1;
    }

    printf("[story] Saved successfully! Completion: %lu%% (Crystals: %lu, Gems: %lu, Relics: %lu)\n",
           (unsigned long)g_save.completion_pct,
           (unsigned long)g_save.crystals_count,
           (unsigned long)g_save.gems_count,
           (unsigned long)g_save.relics_count);
    return 0;
}

static int read_save_file(const char *path, SaveData *result)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;

    SaveData temp;
    size_t read_bytes = fread(&temp, 1, sizeof(SaveData), f);
    int extra = fgetc(f);
    int read_failed = ferror(f);
    fclose(f);

    if (read_bytes != sizeof(SaveData) || extra != EOF || read_failed) {
        printf("[story] Error reading save file: size mismatch (%u/%u bytes)\n",
               (unsigned)read_bytes, (unsigned)sizeof(SaveData));
        return -1;
    }

    if (memcmp(temp.magic, "CB3S", 4) != 0 || temp.version != 1) {
        printf("[story] Error: invalid save file magic or version\n");
        return -1;
    }

    size_t payload_size = offsetof(SaveData, crc32);
    uint32_t expected_crc = compute_crc32((const uint8_t *)&temp, payload_size);
    if (temp.crc32 != expected_crc) {
        printf("[story] Error: CRC32 mismatch (file: 0x%08lX, calc: 0x%08lX)\n",
               (unsigned long)temp.crc32, (unsigned long)expected_crc);
        return -1;
    }

    memcpy(result, &temp, sizeof(SaveData));
    return 0;
}

int story_load(void)
{
    SaveData loaded;
    if (read_save_file(SAVE_FILE_PATH, &loaded) != 0 &&
        read_save_file(SAVE_BACKUP_PATH, &loaded) != 0 &&
        read_save_file(SAVE_LEGACY_PATH, &loaded) != 0 &&
        read_save_file(SAVE_LEGACY_BACKUP_PATH, &loaded) != 0)
        return -1;
    memcpy(&g_save, &loaded, sizeof(g_save));
    story_update_stats();
    printf("[story] Loaded save file! Completion: %lu%% (Crystals: %lu, Gems: %lu, Relics: %lu)\n",
           (unsigned long)g_save.completion_pct,
           (unsigned long)g_save.crystals_count,
           (unsigned long)g_save.gems_count,
           (unsigned long)g_save.relics_count);
    return 0;
}

