import os

# 1. Update char.h to include is_sliding and slide_timer
with open('port3ds/source/char.h', 'r', encoding='utf-8') as f:
    text = f.read()

if 'is_sliding' not in text:
    old_slam = '    int   is_slamming;    /* Super Belly Flop power */\n    float slam_timer;'
    new_slam = '    int   is_slamming;    /* Super Belly Flop power */\n    float slam_timer;\n    int   is_sliding;\n    float slide_timer;'
    if old_slam not in text:
        old_slam = old_slam.replace('\n', '\r\n')
        new_slam = new_slam.replace('\n', '\r\n')
    assert old_slam in text, 'old_slam not found in char.h'
    text = text.replace(old_slam, new_slam)
    with open('port3ds/source/char.h', 'w', encoding='utf-8') as f:
        f.write(text)
    print('Patched char.h!')

# 2. Update story.h: TOTAL_CHAMBERS 6
with open('port3ds/source/story.h', 'r', encoding='utf-8') as f:
    text = f.read()

if '#define TOTAL_CHAMBERS      5' in text:
    text = text.replace('#define TOTAL_CHAMBERS      5', '#define TOTAL_CHAMBERS      6')
    with open('port3ds/source/story.h', 'w', encoding='utf-8') as f:
        f.write(text)
    print('Patched story.h!')

# 3. Update story.c: s_chamber_levels 6th row and boss bounds
with open('port3ds/source/story.c', 'r', encoding='utf-8') as f:
    text = f.read()

old_chambers = '''    /* Chamber 5: Future */
    { 0x23 /* Gone Tomorrow */, 0x20 /* Orange Asphalt */, 0x22 /* Flaming Passion */, 0x17 /* Mad Bombers */, 0x24 /* Bug Lite */ }
};'''
new_chambers = '''    /* Chamber 5: Future */
    { 0x23 /* Gone Tomorrow */, 0x20 /* Orange Asphalt */, 0x22 /* Flaming Passion */, 0x17 /* Mad Bombers */, 0x24 /* Bug Lite */ },
    /* Chamber 6: Secret Warp Room (Sótano / Neo-Cor) */
    { 0x21 /* Ski Crazed */, 0x16 /* Hang\'em High Secret */, 0x25 /* Area 51? */, 0x1B /* Future Frenzy Secret */, 0x1F /* Rings of Power */ }
};'''

if old_chambers not in text:
    old_chambers = old_chambers.replace('\n', '\r\n')
    new_chambers = new_chambers.replace('\n', '\r\n')

if old_chambers in text:
    text = text.replace(old_chambers, new_chambers)

old_boss_unlock = '''int story_is_boss_unlocked(int boss_idx)
{
    if (boss_idx < 0 || boss_idx >= TOTAL_CHAMBERS)
        return 0;'''
new_boss_unlock = '''int story_is_boss_unlocked(int boss_idx)
{
    if (boss_idx < 0 || boss_idx >= 5)
        return 0;'''
if old_boss_unlock not in text:
    old_boss_unlock = old_boss_unlock.replace('\n', '\r\n')
    new_boss_unlock = new_boss_unlock.replace('\n', '\r\n')
if old_boss_unlock in text:
    text = text.replace(old_boss_unlock, new_boss_unlock)

old_boss_id = '''int story_get_boss_level_id(int boss_idx)
{
    if (boss_idx < 0 || boss_idx >= TOTAL_CHAMBERS)
        return -1;
    return s_boss_level_ids[boss_idx];
}'''
new_boss_id = '''int story_get_boss_level_id(int boss_idx)
{
    if (boss_idx < 0 || boss_idx >= 5)
        return -1;
    return s_boss_level_ids[boss_idx];
}'''
if old_boss_id not in text:
    old_boss_id = old_boss_id.replace('\n', '\r\n')
    new_boss_id = new_boss_id.replace('\n', '\r\n')
if old_boss_id in text:
    text = text.replace(old_boss_id, new_boss_id)

with open('port3ds/source/story.c', 'w', encoding='utf-8') as f:
    f.write(text)
print('Patched story.c!')

# 4. Update props.c: allow is_sliding to break crates
with open('port3ds/source/props.c', 'r', encoding='utf-8') as f:
    text = f.read()

old_prop_check = 'if (player->spin_timer > 0.0f || player->is_slamming) {'
new_prop_check = 'if (player->spin_timer > 0.0f || player->is_slamming || player->is_sliding) {'
if old_prop_check in text:
    text = text.replace(old_prop_check, new_prop_check)
    with open('port3ds/source/props.c', 'w', encoding='utf-8') as f:
        f.write(text)
    print('Patched props.c!')

# 5. Update bottom_ui.c helper text
with open('port3ds/source/bottom_ui.c', 'r', encoding='utf-8') as f:
    text = f.read()

old_ui_hint = 'text(&c,20,205,1,"PAD MOVE   A JUMP   B/Y SPIN",muted,309);'
new_ui_hint = 'text(&c,20,205,1,"PAD MOVE   A JUMP   B/Y SPIN   R SLIDE",muted,309);'
if old_ui_hint in text:
    text = text.replace(old_ui_hint, new_ui_hint)
    with open('port3ds/source/bottom_ui.c', 'w', encoding='utf-8') as f:
        f.write(text)
    print('Patched bottom_ui.c!')

# 6. Update main.c: chamber_names, s_chamber_levels, slide physics, and Chamber 6 Warp Room UI
with open('port3ds/source/main.c', 'r', encoding='utf-8') as f:
    text = f.read()

old_main_chambers = '''static const char *const chamber_names[TOTAL_CHAMBERS] = {
    "MEDIEVAL", "ARABIAN", "ORIENTAL", "EGYPTIAN", "FUTURE"
};'''
new_main_chambers = '''static const char *const chamber_names[TOTAL_CHAMBERS] = {
    "MEDIEVAL", "ARABIAN", "ORIENTAL", "EGYPTIAN", "FUTURE", "SECRET (NEO-COR)"
};'''
if old_main_chambers not in text:
    old_main_chambers = old_main_chambers.replace('\n', '\r\n')
    new_main_chambers = new_main_chambers.replace('\n', '\r\n')
if old_main_chambers in text:
    text = text.replace(old_main_chambers, new_main_chambers)

old_main_chamber_levels = '''static const int s_chamber_levels[TOTAL_CHAMBERS][LEVELS_PER_CHAMBER] = {
    { 0x0B, 0x0E, 0x0A, 0x0C, 0x19 },
    { 0x0F, 0x16, 0x15, 0x12, 0x11 },
    { 0x10, 0x1C, 0x1A, 0x14, 0x1D },
    { 0x1E, 0x13, 0x0D, 0x1B, 0x18 },
    { 0x23, 0x20, 0x22, 0x17, 0x24 }
};'''
new_main_chamber_levels = '''static const int s_chamber_levels[TOTAL_CHAMBERS][LEVELS_PER_CHAMBER] = {
    { 0x0B, 0x0E, 0x0A, 0x0C, 0x19 },
    { 0x0F, 0x16, 0x15, 0x12, 0x11 },
    { 0x10, 0x1C, 0x1A, 0x14, 0x1D },
    { 0x1E, 0x13, 0x0D, 0x1B, 0x18 },
    { 0x23, 0x20, 0x22, 0x17, 0x24 },
    { 0x21, 0x16, 0x25, 0x1B, 0x1F }
};'''
if old_main_chamber_levels not in text:
    old_main_chamber_levels = old_main_chamber_levels.replace('\n', '\r\n')
    new_main_chamber_levels = new_main_chamber_levels.replace('\n', '\r\n')
if old_main_chamber_levels in text:
    text = text.replace(old_main_chamber_levels, new_main_chamber_levels)

# Add slide logic in update_player
old_ground_jump = '''    if (s_player.onground) {
        s_player.can_double_jump = 1;
        s_player.is_slamming = 0;'''
new_ground_jump = '''    /* Slide attack and Slide-Jump */
    if (s_player.onground && !s_bazooka_mode) {
        if ((kDown & KEY_R) && !s_player.is_sliding) {
            s_player.is_sliding = 1;
            s_player.slide_timer = 0.42f;
            audio_play_sfx(SFX_SPIN);
        }
    }
    if (s_player.is_sliding) {
        s_player.slide_timer -= dt;
        if (s_player.slide_timer <= 0.0f || !s_player.onground) {
            s_player.is_sliding = 0;
        } else {
            float s_fx = sinf(s_player.yaw), s_fz = cosf(s_player.yaw);
            s_player.vx = s_fx * 460.0f;
            s_player.vz = s_fz * 460.0f;
            if (kDown & KEY_A) {
                s_player.is_sliding = 0;
                s_player.vy = 660.0f;
                s_player.onground = 0;
                audio_play_sfx(SFX_JUMP);
                props_spawn_landing_dust(s_player.x, s_player.y, s_player.z);
            }
        }
    }

    if (s_player.onground) {
        s_player.can_double_jump = 1;
        s_player.is_slamming = 0;'''
if old_ground_jump not in text:
    old_ground_jump = old_ground_jump.replace('\n', '\r\n')
    new_ground_jump = new_ground_jump.replace('\n', '\r\n')
if old_ground_jump in text:
    text = text.replace(old_ground_jump, new_ground_jump)

# Update Warp Room Chamber 6 boss button display
old_boss_display = '''    char btxt[64];
    const char *bnm = level_names[boss_lid & 0x3F];
    snprintf(btxt, sizeof(btxt), "BOSS: %s", bnm ? bnm : "LOCKED");
    text_draw(40, boss_y + 5, 2, btxt, 0.98f, 0.35f, 0.25f);

    if (g_save.bosses_defeated[s_active_chamber]) {
        text_draw(SCREEN_W - 65, boss_y + 7, 1, "DEFEATED", 0.25f, 0.95f, 0.35f);
    } else if (boss_unlocked) {
        text_draw(SCREEN_W - 65, boss_y + 7, 1, "READY!", 0.98f, 0.85f, 0.20f);
    } else {
        int left = 5 - story_get_chamber_crystal_count(s_active_chamber);
        char req[32];
        snprintf(req, sizeof(req), "%d CRYSTALS", left);
        text_draw(SCREEN_W - 85, boss_y + 7, 1, req, 0.70f, 0.70f, 0.75f);
    }'''

new_boss_display = '''    char btxt[64];
    if (s_active_chamber == 5) {
        snprintf(btxt, sizeof(btxt), "SUPER SECRET: HOT COCO & EGGIPUS");
        text_draw(40, boss_y + 5, 2, btxt, 0.85f, 0.50f, 0.98f);
        text_draw(SCREEN_W - 65, boss_y + 7, 1, "PORTAL", 0.98f, 0.85f, 0.20f);
    } else {
        const char *bnm = (boss_lid >= 0) ? level_names[boss_lid & 0x3F] : "LOCKED";
        snprintf(btxt, sizeof(btxt), "BOSS: %s", bnm ? bnm : "LOCKED");
        text_draw(40, boss_y + 5, 2, btxt, 0.98f, 0.35f, 0.25f);

        if (g_save.bosses_defeated[s_active_chamber]) {
            text_draw(SCREEN_W - 65, boss_y + 7, 1, "DEFEATED", 0.25f, 0.95f, 0.35f);
        } else if (boss_unlocked) {
            text_draw(SCREEN_W - 65, boss_y + 7, 1, "READY!", 0.98f, 0.85f, 0.20f);
        } else {
            int left = 5 - story_get_chamber_crystal_count(s_active_chamber);
            char req[32];
            snprintf(req, sizeof(req), "%d CRYSTALS", left);
            text_draw(SCREEN_W - 85, boss_y + 7, 1, req, 0.70f, 0.70f, 0.75f);
        }
    }'''

if old_boss_display not in text:
    old_boss_display = old_boss_display.replace('\n', '\r\n')
    new_boss_display = new_boss_display.replace('\n', '\r\n')
if old_boss_display in text:
    text = text.replace(old_boss_display, new_boss_display)

# Update Warp Room input for Chamber 6 boss pad
old_warp_a = '''                } else {
                    /* Boss portal */
                    if (story_is_boss_unlocked(s_active_chamber))
                        target_lid = story_get_boss_level_id(s_active_chamber);
                }'''
new_warp_a = '''                } else {
                    /* Boss portal or Super Secret Portal */
                    if (s_active_chamber == 5)
                        target_lid = 0x27; /* Hot Coco */
                    else if (story_is_boss_unlocked(s_active_chamber))
                        target_lid = story_get_boss_level_id(s_active_chamber);
                }'''
if old_warp_a not in text:
    old_warp_a = old_warp_a.replace('\n', '\r\n')
    new_warp_a = new_warp_a.replace('\n', '\r\n')
if old_warp_a in text:
    text = text.replace(old_warp_a, new_warp_a)

# Update Chamber footer guide
old_footer = 'L/R : CHANGE CHAMBER (1-5)    D-PAD : SELECT    A : ENTER PORTAL'
new_footer = 'L/R : CHANGE CHAMBER (1-6)    D-PAD : SELECT    A : ENTER PORTAL'
text = text.replace(old_footer, new_footer)

with open('port3ds/source/main.c', 'w', encoding='utf-8') as f:
    f.write(text)
print('Patched main.c!')
