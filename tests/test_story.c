/* Host regressions for progression and recoverable save generations. */
#include <assert.h>
#ifdef NDEBUG
#error Story tests require enabled assertions
#endif
#include <stdio.h>
#include <string.h>
#include "story.h"
#include "audio.h"

void audio_play_sfx(SfxId id) { (void)id; }

static void fresh(void)
{
    remove("save.dat");
    remove("save.tmp");
    remove("save.bak");
    remove("crash3_save.dat");
    remove("crash3_save.bak");
    story_init();
}

int main(void)
{
    fresh();
    assert(story_is_level_unlocked(0x0B));
    assert(!story_is_level_unlocked(-1));
    assert(!story_is_level_unlocked(64));
    assert(!story_is_level_unlocked(0x28));
    assert(!story_is_level_unlocked(0x06));
    assert(!story_is_level_unlocked(0x0F));
    const int first_chamber[] = {0x0B, 0x0E, 0x0A, 0x0C, 0x19};
    for (unsigned i = 0; i < sizeof(first_chamber)/sizeof(first_chamber[0]); i++)
        story_on_level_cleared(first_chamber[i], 0, 0);
    assert(g_save.crystals_count == 5);
    assert(story_is_level_unlocked(0x06));
    story_on_boss_defeated(0);
    assert(story_has_power(POWER_BELLY_FLOP));
    assert(story_is_level_unlocked(0x0F));
    story_on_level_cleared(0x06, 1, 50000);
    assert(g_save.crystals_count == 5);
    assert(!g_save.levels[0x06].clear_gem);
    story_on_level_cleared(0x27, 0, 0);
    assert(g_save.crystals_count == 5);
    story_on_level_cleared(0x0B, 1, 44030);
    assert(g_save.levels[0x0B].relic_tier == RELIC_PLATINUM);
    story_on_level_cleared(0x0B, 0, 80000);
    assert(g_save.levels[0x0B].relic_tier == RELIC_PLATINUM);
    assert(g_save.levels[0x0B].best_time_ms == 44030);
    assert(g_save.gems_count == 1);
    assert(g_save.relics_count == 1);
    assert(story_get_total_relics() == 1);
    assert(story_get_relic_count_by_tier(RELIC_PLATINUM) == 1);
    assert(story_get_relic_count_by_tier(RELIC_GOLD) == 0);
    assert(story_get_relic_count_by_tier(RELIC_SAPPHIRE) == 0);
    assert(story_save() == 0);
    SaveData expected = g_save;
    memset(&g_save, 0, sizeof(g_save));
    assert(story_load() == 0);
    assert(memcmp(&g_save, &expected, sizeof(g_save)) == 0);

    /* Legacy filenames remain readable and migrate on the next save. */
    assert(rename("save.dat", "crash3_save.dat") == 0);
    remove("save.bak");
    memset(&g_save, 0, sizeof(g_save));
    assert(story_load() == 0);
    assert(g_save.crystals_count == 5);
    assert(story_save() == 0);
    remove("crash3_save.dat");
    assert(story_save() == 0);

    /* The backup is retained until the next successful commit. */
    assert(rename("save.dat", "save.tmp") == 0);
    memset(&g_save, 0, sizeof(g_save));
    assert(story_load() == 0);
    assert(g_save.crystals_count == 5);
    remove("save.tmp");
    assert(story_save() == 0);
    FILE *file = fopen("save.dat", "r+b");
    assert(file);
    assert(fseek(file, 20, SEEK_SET) == 0);
    int byte = fgetc(file);
    assert(byte != EOF);
    assert(fseek(file, 20, SEEK_SET) == 0);
    assert(fputc(byte ^ 1, file) != EOF);
    assert(fclose(file) == 0);
    assert(story_load() == 0); /* A changed payload fails CRC, then recovers. */
    assert(g_save.crystals_count == 5);
    assert(story_save() == 0); /* Repair must preserve the valid backup. */
    file = fopen("save.dat", "wb");
    assert(file);
    fputs("broken", file);
    assert(fclose(file) == 0);
    assert(story_load() == 0);
    assert(g_save.crystals_count == 5);
    file = fopen("save.bak", "wb");
    assert(file);
    fputs("broken", file);
    assert(fclose(file) == 0);
    SaveData before = g_save;
    assert(story_load() != 0);
    assert(memcmp(&before, &g_save, sizeof(g_save)) == 0);

    /* ------------------------------------------------------------------ */
    /* M3 Unit Tests: Relics, Chamber 6, Progression & Colored Gems       */
    /* ------------------------------------------------------------------ */
    fresh();

    /* 1. Exact boundaries for Toad Village */
    assert(story_get_relic_tier(0x0B, 0) == RELIC_NONE);
    assert(story_get_relic_tier(0x0B, 44030) == RELIC_PLATINUM);
    assert(story_get_relic_tier(0x0B, 44031) == RELIC_GOLD);
    assert(story_get_relic_tier(0x0B, 57500) == RELIC_GOLD);
    assert(story_get_relic_tier(0x0B, 57501) == RELIC_SAPPHIRE);
    assert(story_get_relic_tier(0x0B, 63000) == RELIC_SAPPHIRE);
    assert(story_get_relic_tier(0x0B, 63001) == RELIC_NONE);

    /* 2. All 30 time-trial levels: hierarchy and grading boundaries */
    const int all_30_levels[30] = {
        0x0B, 0x0E, 0x0A, 0x0C, 0x19, /* Chamber 1 */
        0x0F, 0x16, 0x15, 0x12, 0x11, /* Chamber 2 */
        0x10, 0x1C, 0x1A, 0x14, 0x1D, /* Chamber 3 */
        0x1E, 0x13, 0x0D, 0x1B, 0x18, /* Chamber 4 */
        0x23, 0x20, 0x22, 0x17, 0x24, /* Chamber 5 */
        0x21, 0x1F, 0x25, 0x26, 0x27  /* Secret / Chamber 6 */
    };
    for (int i = 0; i < 30; i++) {
        int lid = all_30_levels[i];
        uint32_t s_ms = 0, g_ms = 0, p_ms = 0;
        story_get_canonical_relic_times(lid, &s_ms, &g_ms, &p_ms);
        assert(s_ms > 0 && g_ms > 0 && p_ms > 0);
        assert(p_ms < g_ms && g_ms < s_ms);
        assert(story_get_relic_tier(lid, p_ms) == RELIC_PLATINUM);
        assert(story_get_relic_tier(lid, p_ms + 1) == RELIC_GOLD);
        assert(story_get_relic_tier(lid, g_ms) == RELIC_GOLD);
        assert(story_get_relic_tier(lid, g_ms + 1) == RELIC_SAPPHIRE);
        assert(story_get_relic_tier(lid, s_ms) == RELIC_SAPPHIRE);
        assert(story_get_relic_tier(lid, s_ms + 1) == RELIC_NONE);
    }

    /* 3. Sequential tier upgrade chain: None -> Sapphire -> Gold -> Platinum */
    int test_lid = 0x0A; /* Orient Express: 41000 sap, 27800 gold, 18100 plat */
    story_on_level_cleared(test_lid, 0, 50000); /* slower than sapphire */
    assert(g_save.levels[test_lid].relic_tier == RELIC_NONE);
    assert(g_save.levels[test_lid].best_time_ms == 50000);
    story_on_level_cleared(test_lid, 0, 35000); /* sapphire run */
    assert(g_save.levels[test_lid].relic_tier == RELIC_SAPPHIRE);
    assert(g_save.levels[test_lid].best_time_ms == 35000);
    assert(story_get_total_relics() == 1);
    story_on_level_cleared(test_lid, 0, 38000); /* slower run preserves sapphire */
    assert(g_save.levels[test_lid].relic_tier == RELIC_SAPPHIRE);
    assert(g_save.levels[test_lid].best_time_ms == 35000);
    story_on_level_cleared(test_lid, 0, 25000); /* gold upgrade */
    assert(g_save.levels[test_lid].relic_tier == RELIC_GOLD);
    assert(g_save.levels[test_lid].best_time_ms == 25000);
    assert(story_get_total_relics() == 1);
    story_on_level_cleared(test_lid, 0, 18000); /* platinum upgrade */
    assert(g_save.levels[test_lid].relic_tier == RELIC_PLATINUM);
    assert(g_save.levels[test_lid].best_time_ms == 18000);
    assert(story_get_total_relics() == 1);

    /* 4. Chamber 6 unlock & anti-shadowing check */
    fresh();
    assert(story_is_chamber_unlocked(0) == 1);
    assert(story_is_chamber_unlocked(5) == 0);
    assert(story_is_level_unlocked(0x21) == 0);
    assert(story_is_level_unlocked(0x25) == 0);
    assert(story_is_level_unlocked(0x1F) == 0);

    /* Defeat all 5 bosses (Cortex) with 0 relics: Chamber 6 MUST remain locked */
    for (int b = 0; b < 5; b++) {
        g_save.bosses_defeated[b] = 1;
    }
    assert(story_is_chamber_unlocked(5) == 0);
    assert(story_is_level_unlocked(0x21) == 0);
    assert(story_is_level_unlocked(0x25) == 0);
    assert(story_is_level_unlocked(0x1F) == 0);
    assert(story_is_chamber_level_unlocked(5, 0) == 0);
    assert(story_is_chamber_level_unlocked(5, 2) == 0);
    assert(story_is_chamber_level_unlocked(5, 4) == 0);

    /* 5. Progressive Relic Milestones */
    fresh();
    g_save.relics_count = 4;
    assert(story_is_chamber_unlocked(5) == 0);
    assert(story_is_level_unlocked(0x21) == 0);
    assert(story_is_chamber_level_unlocked(5, 0) == 0);

    g_save.relics_count = 5;
    assert(story_is_chamber_unlocked(5) == 1);
    assert(story_is_level_unlocked(0x21) == 1);
    assert(story_is_chamber_level_unlocked(5, 0) == 1);
    assert(story_is_chamber_level_unlocked(5, 1) == 0);

    g_save.relics_count = 9;
    assert(story_is_chamber_level_unlocked(5, 1) == 0);
    g_save.relics_count = 10;
    assert(story_is_chamber_level_unlocked(5, 1) == 1);
    assert(story_is_level_unlocked(0x16) == 1);
    assert(story_is_chamber_level_unlocked(5, 2) == 0);

    g_save.relics_count = 14;
    assert(story_is_level_unlocked(0x25) == 0);
    assert(story_is_chamber_level_unlocked(5, 2) == 0);
    g_save.relics_count = 15;
    assert(story_is_level_unlocked(0x25) == 1);
    assert(story_is_chamber_level_unlocked(5, 2) == 1);
    assert(story_is_chamber_level_unlocked(5, 3) == 0);

    g_save.relics_count = 19;
    assert(story_is_chamber_level_unlocked(5, 3) == 0);
    assert(story_is_level_unlocked(0x27) == 0);
    g_save.relics_count = 20;
    assert(story_is_chamber_level_unlocked(5, 3) == 1);
    assert(story_is_level_unlocked(0x1B) == 1);
    assert(story_is_level_unlocked(0x27) == 1);
    assert(story_is_level_unlocked(0x1F) == 0);

    g_save.relics_count = 24;
    assert(story_is_level_unlocked(0x1F) == 0);
    assert(story_is_chamber_level_unlocked(5, 4) == 0);
    assert(story_is_level_unlocked(0x26) == 0);
    g_save.relics_count = 25;
    assert(story_is_level_unlocked(0x1F) == 1);
    assert(story_is_chamber_level_unlocked(5, 4) == 1);
    assert(story_is_level_unlocked(0x26) == 1);

    /* 6. Verify Chamber 6 levels award clear gems and relics, but ZERO crystals */
    fresh();
    story_on_level_cleared(0x21 /* Ski Crazed */, 1, 0);
    assert(g_save.crystals_count == 0);
    assert(g_save.levels[0x21].crystal == 0);
    assert(g_save.levels[0x21].clear_gem == 1);
    assert(g_save.gems_count == 1);

    /* 7. Verify Colored Gem Mapping and Awarding */
    assert(story_get_level_colored_gem(0x18) == GEM_COLOR_BLUE);
    assert(story_get_level_colored_gem(0x1C) == GEM_COLOR_RED);
    assert(story_get_level_colored_gem(0x22) == GEM_COLOR_GREEN);
    assert(story_get_level_colored_gem(0x16) == GEM_COLOR_YELLOW);
    assert(story_get_level_colored_gem(0x1A) == GEM_COLOR_PURPLE);
    assert(story_get_level_colored_gem(0x0B) == GEM_COLOR_NONE);

    assert(!story_has_colored_gem(GEM_COLOR_BLUE));
    story_award_bonus_gem(0x18);
    assert(story_has_colored_gem(GEM_COLOR_BLUE));
    assert(g_save.levels[0x18].bonus_gem == 1);
    assert(g_save.gems_count == 2); /* 1 clear_gem from 0x21 + 1 bonus_gem from 0x18 */

    fresh();
    puts("story regressions passed");
    return 0;
}
