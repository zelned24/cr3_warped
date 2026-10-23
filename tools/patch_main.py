import os

with open('port3ds/source/main.c', 'r', encoding='utf-8') as f:
    text = f.read()

# 1. Add s_bazooka_mode declaration near line 310
old_decl = 'static int s_loaded_id = -1;\nstatic int s_cur_model = 0;'
new_decl = 'static int s_loaded_id = -1;\nstatic int s_cur_model = 0;\nstatic int s_bazooka_mode = 0;'

if old_decl not in text:
    old_decl = old_decl.replace('\n', '\r\n')
    new_decl = new_decl.replace('\n', '\r\n')

assert old_decl in text, 'old_decl not found'
text = text.replace(old_decl, new_decl)

# 2. Add draw_crosshair_2d and play_level_theme helpers before draw_hud
old_hud = '''static void draw_hud(void)
{
    /* Gameplay stays unobstructed; the pause menu appends to this buffer. */
    text_reset();
}'''
if old_hud not in text:
    old_hud = old_hud.replace('\n', '\r\n')

assert old_hud in text, 'old_hud not found'

new_hud_and_clear = '''static void draw_crosshair_2d(float cx, float cy)
{
    draw_rect_2d(cx - 2.0f, cy - 2.0f, 4.0f, 4.0f, 1.0f, 0.20f, 0.20f);
    draw_rect_2d(cx - 18.0f, cy - 1.0f, 10.0f, 2.0f, 0.95f, 0.85f, 0.20f);
    draw_rect_2d(cx + 8.0f,  cy - 1.0f, 10.0f, 2.0f, 0.95f, 0.85f, 0.20f);
    draw_rect_2d(cx - 1.0f,  cy - 18.0f, 2.0f, 10.0f, 0.95f, 0.85f, 0.20f);
    draw_rect_2d(cx - 1.0f,  cy + 8.0f,  2.0f, 10.0f, 0.95f, 0.85f, 0.20f);
    draw_frame_2d(cx - 12.0f, cy - 12.0f, 24.0f, 24.0f, 1.0f, 0.95f, 0.75f, 0.15f);
}

static void play_level_theme(int level_id)
{
    if (props_boss_active() || (level_id == 0x06 || level_id == 0x03 || level_id == 0x04 || level_id == 0x05 || level_id == 0x07)) {
        audio_play_bgm(BGM_BOSS);
        return;
    }
    int chamber = story_get_chamber(level_id);
    switch (chamber) {
    case 0: audio_play_bgm(BGM_MEDIEVAL); break;
    case 1: audio_play_bgm(BGM_ARABIAN); break;
    case 2: audio_play_bgm(BGM_ORIENTAL); break;
    case 3: audio_play_bgm(BGM_EGYPTIAN); break;
    case 4: audio_play_bgm(BGM_FUTURE); break;
    default: audio_play_bgm(BGM_MEDIEVAL); break;
    }
}

static void draw_hud(void)
{
    text_reset();

    if (s_state != STATE_GAMEPLAY && s_state != STATE_ENTRANCE)
        return;

    /* 1. Fruit Bazooka aiming overlay & reticle */
    if (s_bazooka_mode) {
        draw_crosshair_2d(SCREEN_W * 0.5f, SCREEN_H * 0.5f);
        text_draw(162, 138, 1, "AIMING BAZOOKA", 0.98f, 0.85f, 0.20f);
        text_draw(152, 148, 1, "B / Y : FIRE WUMPA", 0.80f, 0.90f, 1.0f);
    }

    /* 2. Top-Right: Wumpa Fruit & Crate counters */
    char w_str[32];
    snprintf(w_str, sizeof(w_str), "WUMPA %02d", g_stats.wumpa_count);
    text_draw(310, 8, 1, w_str, 0.98f, 0.55f, 0.15f);

    char c_str[32];
    snprintf(c_str, sizeof(c_str), "CRATES %d/%d", g_stats.crates_broken, g_stats.crates_total);
    text_draw(300, 20, 1, c_str, 0.85f, 0.75f, 0.35f);

    /* 3. Top-Left: Crash lives & Aku Aku mask tier */
    char l_str[32];
    snprintf(l_str, sizeof(l_str), "CRASH x%d", g_stats.lives);
    text_draw(12, 8, 1, l_str, 0.95f, 0.85f, 0.35f);

    if (g_stats.aku_aku_tier > 0) {
        char a_str[32];
        snprintf(a_str, sizeof(a_str), "AKU AKU x%d", g_stats.aku_aku_tier);
        text_draw(12, 20, 1, a_str, 0.25f, 0.95f, 0.35f);
    }

    /* 4. Top-Center: Time Trial clock if active */
    if (props_is_time_trial()) {
        uint32_t ms = props_get_time_trial_ms();
        uint32_t mins = ms / 60000;
        uint32_t secs = (ms / 1000) % 60;
        uint32_t cs = (ms / 10) % 100;
        char t_str[32];
        snprintf(t_str, sizeof(t_str), "%02u:%02u:%02u", mins, secs, cs);
        draw_rect_2d(160, 6, 80, 16, 0.08f, 0.12f, 0.20f);
        draw_frame_2d(160, 6, 80, 16, 1.0f, 0.95f, 0.75f, 0.20f);
        text_draw(172, 9, 2, t_str, 0.98f, 0.85f, 0.20f);
    }

    /* 5. Top-Center: Boss Health Bar */
    if (props_boss_active()) {
        int hp = props_boss_hp();
        int max_hp = (s_loaded_id == 0x05 || s_loaded_id == 0x07) ? 5 : 3;
        draw_rect_2d(120, 26, 160, 10, 0.15f, 0.08f, 0.08f);
        draw_frame_2d(120, 26, 160, 10, 1.0f, 0.95f, 0.35f, 0.25f);
        float pct = (float)hp / (float)max_hp;
        if (pct > 0.0f)
            draw_rect_2d(122, 28, 156.0f * pct, 6, 0.95f, 0.25f, 0.20f);
        text_draw(165, 14, 1, "BOSS HEALTH", 0.95f, 0.35f, 0.25f);
    }
}

static void draw_level_clear_screen(void)
{
    text_reset();

    /* Header banner */
    draw_rect_2d(0, 0, SCREEN_W, 36, 0.08f, 0.12f, 0.20f);
    draw_rect_2d(0, 36, SCREEN_W, 2, 0.95f, 0.55f, 0.15f);

    const char *lvl_name = (s_loaded_id >= 0 && s_loaded_id < 64) ? level_names[s_loaded_id & 0x3F] : "UNKNOWN";
    char title[64];
    snprintf(title, sizeof(title), "LEVEL CLEARED: %s", lvl_name ? lvl_name : "UNKNOWN");
    text_draw(35, 10, 2, title, 0.98f, 0.85f, 0.22f);

    /* Main summary panel */
    draw_rect_2d(25, 46, SCREEN_W - 50, 158, 0.06f, 0.09f, 0.16f);
    draw_frame_2d(25, 46, SCREEN_W - 50, 158, 2.0f, 0.35f, 0.55f, 0.85f);

    /* 1. Crystal status */
    int has_crystal = (s_loaded_id >= 0 && s_loaded_id < 64) ? g_save.levels[s_loaded_id].crystal : 0;
    draw_diamond_2d(45, 64, 8.0f, s_ui_tick * 0.05f, 0.85f, 0.25f, 0.98f);
    if (has_crystal) {
        text_draw(60, 58, 2, "POWER CRYSTAL RETRIEVED!", 0.92f, 0.40f, 0.98f);
    } else {
        text_draw(60, 58, 2, "POWER CRYSTAL MISSED", 0.60f, 0.60f, 0.70f);
    }

    /* 2. Crate tally & Clear Gem status */
    char crate_txt[64];
    snprintf(crate_txt, sizeof(crate_txt), "CRATES: %d / %d", g_stats.crates_broken, g_stats.crates_total);
    text_draw(60, 84, 2, crate_txt, 0.95f, 0.80f, 0.30f);

    int has_gem = (s_loaded_id >= 0 && s_loaded_id < 64) ? g_save.levels[s_loaded_id].clear_gem : 0;
    draw_diamond_2d(45, 114, 8.0f, -s_ui_tick * 0.05f, 0.30f, 0.85f, 0.98f);
    if (has_gem) {
        text_draw(60, 108, 2, "CLEAR GEM AWARDED - ALL BOXES SMASHED!", 0.30f, 0.95f, 0.98f);
    } else {
        int missed = g_stats.crates_total - g_stats.crates_broken;
        if (missed < 0) missed = 0;
        char miss_txt[64];
        snprintf(miss_txt, sizeof(miss_txt), "MISSED %d CRATES FOR CLEAR GEM", missed);
        text_draw(60, 108, 1, miss_txt, 0.75f, 0.75f, 0.80f);
    }

    /* 3. Time trial & Relic */
    uint32_t best_ms = (s_loaded_id >= 0 && s_loaded_id < 64) ? g_save.levels[s_loaded_id].best_time_ms : 0;
    if (best_ms > 0) {
        uint32_t mins = best_ms / 60000;
        uint32_t secs = (best_ms / 1000) % 60;
        uint32_t cs = (best_ms / 10) % 100;
        char tt_txt[64];
        snprintf(tt_txt, sizeof(tt_txt), "BEST TIME: %02u:%02u:%02u", mins, secs, cs);
        text_draw(60, 134, 1, tt_txt, 0.98f, 0.85f, 0.20f);

        RelicTier tier = (RelicTier)g_save.levels[s_loaded_id].relic_tier;
        if (tier == RELIC_PLATINUM)
            text_draw(60, 148, 2, "PLATINUM RELIC AWARDED!", 0.85f, 0.95f, 0.98f);
        else if (tier == RELIC_GOLD)
            text_draw(60, 148, 2, "GOLD RELIC AWARDED!", 0.98f, 0.85f, 0.20f);
        else if (tier == RELIC_SAPPHIRE)
            text_draw(60, 148, 2, "SAPPHIRE RELIC AWARDED!", 0.30f, 0.70f, 0.98f);
    } else {
        text_draw(60, 134, 1, "TIME TRIAL: BEAT TARGET CANONICAL TIME FOR RELIC", 0.60f, 0.65f, 0.75f);
    }

    /* 4. Super Power notification if boss level */
    int boss_idx = story_get_chamber(s_loaded_id);
    if (props_boss_hp() <= 0 && (s_loaded_id == 0x06 || s_loaded_id == 0x03 || s_loaded_id == 0x04 || s_loaded_id == 0x05 || s_loaded_id == 0x07)) {
        static const char *const power_names[5] = {
            "SUPER BELLY FLOP (PRESS L IN AIR)",
            "DOUBLE JUMP (PRESS A IN AIR)",
            "DEATH TORNADO SPIN (RAPID B/Y)",
            "FRUIT BAZOOKA (HOLD L/X TO AIM, B/Y TO FIRE)",
            "CRASH DASH (HOLD R TO RUN FAST)"
        };
        if (boss_idx >= 0 && boss_idx < 5) {
            char p_txt[80];
            snprintf(p_txt, sizeof(p_txt), "NEW POWER: %s", power_names[boss_idx]);
            text_draw(45, 172, 1, p_txt, 0.20f, 0.98f, 0.35f);
        }
    }

    /* Footer guide */
    draw_rect_2d(0, SCREEN_H - 24, SCREEN_W, 24, 0.06f, 0.09f, 0.15f);
    float pulse = 0.5f + 0.5f * sinf(s_ui_tick * 0.12f);
    text_draw(50, SCREEN_H - 17, 2, "PRESS A OR START TO RETURN TO WARP ROOM",
              0.80f + 0.20f * pulse, 0.85f + 0.15f * pulse, 0.25f + 0.35f * pulse);

    render_text_top();
}'''
if '\r\n' in text:
    new_hud_and_clear = new_hud_and_clear.replace('\n', '\r\n')
text = text.replace(old_hud, new_hud_and_clear)

# 3. Add Bazooka mode in update_player
old_player_start = '''    circlePosition cp;
    hidCircleRead(&cp);

    float move = 0.0f, strafe = 0.0f;'''
if old_player_start not in text:
    old_player_start = old_player_start.replace('\n', '\r\n')

assert old_player_start in text, 'old_player_start not found'

new_player_start = '''    circlePosition cp;
    hidCircleRead(&cp);

    u32 held = hidKeysHeld();
    u32 kDown = hidKeysDown();

    /* Fruit Bazooka Aiming Mode: Hold L or X while on the ground */
    if (story_has_power(POWER_BAZOOKA) && s_player.onground && ((held & KEY_L) || (held & KEY_X))) {
        s_bazooka_mode = 1;
        s_player.vx = 0.0f;
        s_player.vz = 0.0f;
        if (cp.dx > 20 || cp.dx < -20)
            s_cam_yaw += ((float)cp.dx / 150.0f) * 2.0f * dt;
        if (cp.dy > 20 || cp.dy < -20)
            s_cam_pitch -= ((float)cp.dy / 150.0f) * 1.5f * dt;
        if (held & KEY_DLEFT)  s_cam_yaw -= 1.8f * dt;
        if (held & KEY_DRIGHT) s_cam_yaw += 1.8f * dt;
        if (held & KEY_DUP)    s_cam_pitch += 1.4f * dt;
        if (held & KEY_DDOWN)  s_cam_pitch -= 1.4f * dt;
        if (s_cam_pitch < -1.1f) s_cam_pitch = -1.1f;
        if (s_cam_pitch > 0.8f)  s_cam_pitch = 0.8f;

        if ((kDown & KEY_B) || (kDown & KEY_Y) || (kDown & KEY_R)) {
            float fx = sinf(s_cam_yaw) * cosf(s_cam_pitch);
            float fy = sinf(s_cam_pitch);
            float fz = cosf(s_cam_yaw) * cosf(s_cam_pitch);
            props_spawn_projectile(s_player.x, s_player.y + 18.0f, s_player.z, fx, fy, fz);
            audio_play_sfx(SFX_BAZOOKA);
            if (g_stats.wumpa_count > 0)
                g_stats.wumpa_count--;
        }
        props_update(dt, &s_player);
        if (props_check_exit(s_player.x, s_player.y, s_player.z)) {
            s_state = STATE_LEVEL_CLEAR;
            int all_crates = (g_stats.crates_broken >= g_stats.crates_total && g_stats.crates_total > 0);
            uint32_t tt_ms = props_is_time_trial() ? props_get_time_trial_ms() : 0;
            story_on_level_cleared(s_loaded_id, all_crates, tt_ms);
            story_save();
            audio_play_sfx(SFX_LEVEL_CLEAR);
        }
        return;
    } else {
        s_bazooka_mode = 0;
    }

    float move = 0.0f, strafe = 0.0f;'''

if '\r\n' in text:
    new_player_start = new_player_start.replace('\n', '\r\n')
text = text.replace(old_player_start, new_player_start)

# 4. Remove duplicate u32 held and u32 kDown further down in update_player
old_held_line = '    u32 held = hidKeysHeld();'
if '\r\n' in text:
    old_held_line = old_held_line.replace('\n', '\r\n')
idx1 = text.find(old_held_line)
if idx1 != -1:
    idx2 = text.find(old_held_line, idx1 + len(old_held_line))
    if idx2 != -1:
        text = text[:idx2] + text[idx2 + len(old_held_line):]

old_kdown_line = '    u32 kDown = hidKeysDown();'
if '\r\n' in text:
    old_kdown_line = old_kdown_line.replace('\n', '\r\n')
idx1 = text.find(old_kdown_line)
if idx1 != -1:
    idx2 = text.find(old_kdown_line, idx1 + len(old_kdown_line))
    if idx2 != -1:
        text = text[:idx2] + text[idx2 + len(old_kdown_line):]

# 5. Add exit check at end of update_player
old_props_update = '    props_update(dt, &s_player);\n}'
if old_props_update not in text:
    old_props_update = old_props_update.replace('\n', '\r\n')

assert old_props_update in text, 'old_props_update not found'

new_props_update = '''    props_update(dt, &s_player);

    if (props_check_exit(s_player.x, s_player.y, s_player.z)) {
        s_state = STATE_LEVEL_CLEAR;
        int all_crates = (g_stats.crates_broken >= g_stats.crates_total && g_stats.crates_total > 0);
        uint32_t tt_ms = props_is_time_trial() ? props_get_time_trial_ms() : 0;
        story_on_level_cleared(s_loaded_id, all_crates, tt_ms);
        story_save();
        audio_play_sfx(SFX_LEVEL_CLEAR);
        return;
    }
}'''
if '\r\n' in text:
    new_props_update = new_props_update.replace('\n', '\r\n')
text = text.replace(old_props_update, new_props_update)

# 6. In update_camera: protect KEY_X zoom from triggering in bazooka mode
old_cam_x = 'if (hidKeysDown() & KEY_X) {'
new_cam_x = 'if (!s_bazooka_mode && (hidKeysDown() & KEY_X)) {'
assert old_cam_x in text, 'old_cam_x not found'
text = text.replace(old_cam_x, new_cam_x)

# 7. In load_level_by_index: call play_level_theme
old_load_end = '    props_init_level_facing((int)lid, s_player.x, s_player.y, s_player.z,s_spawn_yaw);\n    return 0;'
if old_load_end not in text:
    old_load_end = old_load_end.replace('\n', '\r\n')
assert old_load_end in text, 'old_load_end not found'
new_load_end = '''    props_init_level_facing((int)lid, s_player.x, s_player.y, s_player.z,s_spawn_yaw);
    play_level_theme((int)lid);
    return 0;'''
if '\r\n' in text:
    new_load_end = new_load_end.replace('\n', '\r\n')
text = text.replace(old_load_end, new_load_end)

# 8. Main loop: STATE_LEVEL_CLEAR input handling
old_pause_state = '} else if (s_state == STATE_PAUSE) {'
new_clear_state = '''} else if (s_state == STATE_LEVEL_CLEAR) {
            if ((kDown & KEY_A) || (kDown & KEY_START) || (kDown & KEY_B)) {
                audio_play_sfx(SFX_MENU_SELECT);
                s_state = STATE_WARP_ROOM;
                audio_play_bgm(BGM_WARP_ROOM);
            }
        } else if (s_state == STATE_PAUSE) {'''
if '\r\n' in text:
    new_clear_state = new_clear_state.replace('\n', '\r\n')
assert old_pause_state in text, 'old_pause_state not found'
text = text.replace(old_pause_state, new_clear_state)

# 9. In STATE_WARP_ROOM transitions, play BGM_WARP_ROOM
old_to_warp = 's_state = STATE_WARP_ROOM;'
new_to_warp = 's_state = STATE_WARP_ROOM; audio_play_bgm(BGM_WARP_ROOM);'
text = text.replace(old_to_warp, new_to_warp)

# In Title screen startup
old_start_title = 'audio_init();\n    story_init();'
if old_start_title not in text:
    old_start_title = old_start_title.replace('\n', '\r\n')
new_start_title = 'audio_init();\n    story_init();\n    audio_play_bgm(BGM_TITLE);'
if '\r\n' in text:
    new_start_title = new_start_title.replace('\n', '\r\n')
assert old_start_title in text, 'old_start_title not found'
text = text.replace(old_start_title, new_start_title)

# 10. Top render loop for STATE_LEVEL_CLEAR
old_loading_render = '''} else if (s_state == STATE_LOADING) {
                C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, s_uLoc_projection, &s_ortho);
                draw_loading_screen(s_target_level_idx, s_loading_ticks);
            }'''
if old_loading_render not in text:
    old_loading_render = old_loading_render.replace('\n', '\r\n')
assert old_loading_render in text, 'old_loading_render not found'

new_loading_render = '''} else if (s_state == STATE_LOADING) {
                C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, s_uLoc_projection, &s_ortho);
                draw_loading_screen(s_target_level_idx, s_loading_ticks);
            } else if (s_state == STATE_LEVEL_CLEAR) {
                C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, s_uLoc_projection, &s_ortho);
                draw_level_clear_screen();
            }'''
if '\r\n' in text:
    new_loading_render = new_loading_render.replace('\n', '\r\n')
text = text.replace(old_loading_render, new_loading_render)

with open('port3ds/source/main.c', 'w', encoding='utf-8') as f:
    f.write(text)

print('Successfully patched main.c!')
