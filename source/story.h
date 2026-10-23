/* Crash Bandicoot: Warped (PS1) -> Nintendo 3DS Port
 * Story Mode, Progression, Save Data & Warp Room State Machine.
 */
#ifndef STORY_H
#define STORY_H

#include <stdint.h>

#define TOTAL_CHAMBERS      6
#define TOTAL_BOSSES        5
#define LEVELS_PER_CHAMBER  5
#define TOTAL_CRYSTALS      25
#define TOTAL_GEMS          45
#define TOTAL_RELICS        30

/* Unlocked Super Powers (awarded after each boss defeat) */
#define POWER_BELLY_FLOP    (1u << 0)  /* Tiny Tiger: enhanced body slam */
#define POWER_DOUBLE_JUMP   (1u << 1)  /* Dingodile: mid-air extra jump */
#define POWER_TORNADO_SPIN  (1u << 2)  /* Dr. N. Tropy: extended hover spin */
#define POWER_BAZOOKA       (1u << 3)  /* Dr. N. Gin: Wumpa bazooka */
#define POWER_CRASH_DASH    (1u << 4)  /* Dr. Neo Cortex: high-speed run */

/* Time Trial Relic Tiers */
typedef enum {
    RELIC_NONE = 0,
    RELIC_SAPPHIRE = 1,
    RELIC_GOLD = 2,
    RELIC_PLATINUM = 3
} RelicTier;

/* Authentic Crash 3 Colored Gems */
typedef enum {
    GEM_COLOR_NONE = 0,
    GEM_COLOR_BLUE = 1,    /* Level 20: Tomb Wader */
    GEM_COLOR_RED = 2,     /* Level 12: Deep Trouble */
    GEM_COLOR_GREEN = 3,   /* Level 23: Flaming Passion */
    GEM_COLOR_YELLOW = 4,  /* Level 7: Hang'em High */
    GEM_COLOR_PURPLE = 5   /* Level 13: High Time */
} GemColor;

/* Identity of an entrance, independently of the archive it loads. A secret
 * entrance shares its level's collectibles, but not its spawn/route. */
typedef enum {
    STORY_ENTRY_NORMAL = 0,
    STORY_ENTRY_BOSS,
    STORY_ENTRY_HANG_EM_HIGH_SECRET,
    STORY_ENTRY_FUTURE_FRENZY_SECRET
} StoryEntryKind;

typedef struct {
    int portal_id;             /* chamber * 6 + slot; stable port-local ID */
    int level_id;              /* NSF/NSD archive ID */
    int chamber_idx;
    int slot_idx;
    StoryEntryKind entry_kind;
    uint8_t required_relics;    /* existing policy; native verification pending */
} StoryPortal;

typedef enum {
    STORY_PORTAL_INVALID = -1,
    STORY_PORTAL_LOCKED = 0,
    STORY_PORTAL_READY = 1
} StoryPortalStatus;

/* Level completion & collectible status */
typedef struct {
    uint8_t crystal;            /* 1 if purple power crystal collected */
    uint8_t clear_gem;          /* 1 if all crates destroyed */
    uint8_t bonus_gem;          /* 1 if secret / death route gem collected */
    uint8_t relic_tier;         /* RelicTier */
    uint32_t best_time_ms;      /* best time in milliseconds */
} LevelProgress;

/* Save data structure stored in sdmc:/3ds/crash3/save.dat */
typedef struct {
    char     magic[4];          /* "CB3S" */
    uint32_t version;           /* 1 */
    uint32_t unlocked_powers;   /* bitmask of POWER_* */
    uint32_t crystals_count;
    uint32_t gems_count;
    uint32_t relics_count;
    uint8_t  bosses_defeated[TOTAL_BOSSES];/* Tiny, Dingo, Tropy, NGin, Cortex */
    LevelProgress levels[64];   /* indexed by level id (0x00 .. 0x3F) */
    uint32_t completion_pct;    /* 0 .. 105% */
    uint32_t crc32;             /* data integrity checksum */
} SaveData;

extern SaveData g_save;

/* Story & Save subsystem */
void story_init(void);
int  story_save(void);
int  story_load(void);
void story_update_stats(void);

/* Chamber and Level queries */
int  story_get_chamber(int level_id);
int  story_is_chamber_unlocked(int chamber_idx);
int  story_is_chamber_level_unlocked(int chamber_idx, int slot_idx);
/* Legacy archive-only policy; does not authorize a specific secret entrance. */
int  story_is_level_unlocked(int level_id);
int  story_is_boss_unlocked(int boss_idx);
int  story_get_boss_level_id(int boss_idx);
int  story_get_chamber_crystal_count(int chamber_idx);
int  story_get_chamber_portal_count(int chamber_idx);
const StoryPortal *story_get_portal(int chamber_idx, int slot_idx);
const StoryPortal *story_get_portal_by_id(int portal_id);
/* A successful query returns the entire identity to retain through loading.
 * LOCKED/INVALID clears *out_portal; it must never leave a stale destination. */
StoryPortalStatus story_resolve_portal(int chamber_idx, int slot_idx,
                                     const StoryPortal **out_portal);
StoryPortalStatus story_resolve_portal_id(int portal_id,
                                        const StoryPortal **out_portal);

/* Progression events */
void story_on_level_cleared(int level_id, int all_crates, uint32_t time_ms);
void story_on_boss_defeated(int boss_idx);
void story_award_bonus_gem(int level_id);
GemColor story_get_level_colored_gem(int level_id);
const char *story_get_colored_gem_name(GemColor color);
int  story_has_colored_gem(GemColor color);

/* Canonical PS1 105% targets */
int  story_get_canonical_crate_count(int level_id);
int story_has_relic_targets(int level_id);
void story_get_canonical_relic_times(int level_id, uint32_t *out_sapphire, uint32_t *out_gold, uint32_t *out_platinum);

/* Time Trial & Relic queries */
RelicTier story_get_relic_tier(int level_id, uint32_t time_ms);
uint32_t  story_get_total_relics(void);
uint32_t  story_get_relic_count_by_tier(RelicTier tier);

/* Power query */
int  story_has_power(uint32_t power_flag);

#endif /* STORY_H */
