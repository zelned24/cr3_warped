/* Prepared C behavior checks. Do not compile while the user pause is active.
 * Link story.c and provide STORY_SAVE_ROOT as an isolated temporary directory. */
#include <assert.h>
#include <string.h>
#include "story.h"
#include "audio.h"

void audio_play_sfx(SfxId id) { (void)id; }

int main(void)
{
    memset(&g_save, 0, sizeof(g_save));
    const StoryPortal *portal = NULL;
    assert(story_get_chamber_portal_count(-1) == 0);
    assert(story_get_chamber_portal_count(6) == 0);
    assert(story_get_chamber_portal_count(5) == 5);
    assert(story_get_portal(5, 5) == NULL);
    assert(story_get_portal_by_id(-1) == NULL);
    assert(story_get_portal_by_id(35) == NULL);
    assert(story_get_chamber(0) == -1); /* no implicit sixth boss ID */
    assert(story_get_boss_level_id(5) == -1);
    assert(!story_is_boss_unlocked(5));
    SaveData before = g_save;
    story_on_boss_defeated(5); /* reject before modifying memory or saving */
    assert(memcmp(&before, &g_save, sizeof(before)) == 0);

    for (int chamber = 0; chamber < TOTAL_CHAMBERS; chamber++) {
        int count = story_get_chamber_portal_count(chamber);
        for (int slot = 0; slot < count; slot++) {
            const StoryPortal *p = story_get_portal(chamber, slot);
            assert(p && p->chamber_idx == chamber && p->slot_idx == slot);
            assert(p == story_get_portal_by_id(p->portal_id));
        }
        assert(story_get_portal(chamber, -1) == NULL);
        assert(story_get_portal(chamber, count) == NULL);
    }

    /* Invalid and locked resolutions must clear an old successful output. */
    assert(story_resolve_portal(0, 0, &portal) == STORY_PORTAL_READY);
    assert(portal && portal->level_id == 0x0B);
    assert(story_resolve_portal(5, 5, &portal) == STORY_PORTAL_INVALID);
    assert(portal == NULL);
    portal = story_get_portal(0, 0);
    assert(story_resolve_portal(1, 0, &portal) == STORY_PORTAL_LOCKED);
    assert(portal == NULL);
    portal = story_get_portal(0, 0);
    assert(story_resolve_portal_id(35, &portal) == STORY_PORTAL_INVALID);
    assert(portal == NULL);

    /* All bosses defeated must not shadow basement relic requirements. */
    for (int bosses = 0; bosses <= 1; bosses++) {
        memset(g_save.bosses_defeated, bosses, sizeof(g_save.bosses_defeated));
        for (int slot = 0; slot < LEVELS_PER_CHAMBER; slot++) {
            const StoryPortal *p = story_get_portal(5, slot);
            g_save.relics_count = p->required_relics - 1;
            assert(!story_is_chamber_level_unlocked(5, slot));
            assert(story_resolve_portal_id(p->portal_id, &portal) == STORY_PORTAL_LOCKED);
            assert(portal == NULL);
            g_save.relics_count++;
            assert(story_resolve_portal_id(p->portal_id, &portal) == STORY_PORTAL_READY);
            assert(portal == p);
            g_save.relics_count++;
            assert(story_is_chamber_level_unlocked(5, slot));
        }
    }

    /* The normal entrance never inherits access from its alternate route. */
    memset(&g_save, 0, sizeof(g_save));
    g_save.relics_count = 10;
    assert(story_resolve_portal(1, 1, &portal) == STORY_PORTAL_LOCKED);
    assert(story_resolve_portal(5, 1, &portal) == STORY_PORTAL_READY);
    assert(portal->level_id == 0x16);
    assert(portal->entry_kind == STORY_ENTRY_HANG_EM_HIGH_SECRET);
    g_save.bosses_defeated[0] = 1;
    g_save.relics_count = 0;
    assert(story_resolve_portal(1, 1, &portal) == STORY_PORTAL_READY);
    assert(portal->entry_kind == STORY_ENTRY_NORMAL);
    assert(story_resolve_portal(5, 1, &portal) == STORY_PORTAL_LOCKED);

    memset(&g_save, 0, sizeof(g_save));
    g_save.relics_count = 20;
    assert(story_resolve_portal(3, 3, &portal) == STORY_PORTAL_LOCKED);
    assert(story_resolve_portal(5, 3, &portal) == STORY_PORTAL_READY);
    assert(portal->level_id == 0x1B);
    assert(portal->entry_kind == STORY_ENTRY_FUTURE_FRENZY_SECRET);
    g_save.bosses_defeated[2] = 1;
    g_save.relics_count = 0;
    assert(story_resolve_portal(3, 3, &portal) == STORY_PORTAL_READY);
    assert(portal->entry_kind == STORY_ENTRY_NORMAL);
    assert(story_resolve_portal(5, 3, &portal) == STORY_PORTAL_LOCKED);

    /* Each boss requires its own five crystals plus chamber access. */
    for (int chamber = 0; chamber < TOTAL_BOSSES; chamber++) {
        memset(&g_save, 0, sizeof(g_save));
        for (int slot = 0; slot < LEVELS_PER_CHAMBER; slot++)
            g_save.levels[story_get_portal(chamber, slot)->level_id].crystal = 1;
        if (chamber > 0) {
            assert(!story_is_boss_unlocked(chamber));
            g_save.bosses_defeated[chamber - 1] = 1;
        }
        assert(story_resolve_portal(chamber, 5, &portal) == STORY_PORTAL_READY);
        assert(portal->entry_kind == STORY_ENTRY_BOSS);
        g_save.levels[story_get_portal(chamber, 0)->level_id].crystal = 0;
        assert(!story_is_boss_unlocked(chamber));
    }
    assert(story_get_chamber_crystal_count(5) == 0);
    return 0;
}
