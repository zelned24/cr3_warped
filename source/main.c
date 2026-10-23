/* Crash Bandicoot: Warped (PS1) -> Nintendo 3DS Homebrew Port.
 *
 * Target: Old Nintendo 3DS XL (ARM11 MPCore @ 268 MHz, PICA200 GPU, 64MB FCRAM).
 * Features:
 *   - Citro3D native PICA200 hardware renderer with dynamic chunk culling
 *   - Native resource/zone/scheduler entry; unresolved services report boot faults
 *   - Native SD Save system (sdmc:/3ds/crash3/save.dat) with CRC32
 *   - Dual-Screen UI: 3D world above, live counters and touch controls below
 *   - Native GOOL actors own gameplay; legacy helpers remain as regression references
 */
#include <3ds.h>
#include <citro3d.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "level.h"
#include "level_sd.h"
#include "level_resources.h"
#include "level_behaviors.h"
#include "level_native.h"
#include "char.h"
#define WARP_ROOM_STUBS_DEFINED
#include "props.h"
#include "audio.h"
#include "audio_stream.h"
#include "font_data.h"
#include "shader_shbin.h"
#include "story.h"
#include "world_texture.h"
#include "render_frustum.h"
#include "bottom_ui.h"
#include "native_runtime.h"
#include "gool_native_engine.h"
#include "gool_native_controller.h"
#include "gool_native_address.h"
#include "gool_native_geometry.h"
#include "gool_native_bounds.h"
#include "gool_native_zone_transition.h"
#include "gool_native_activation_query.h"
#include "gool_native_zone_retirement.h"
#include "gool_creation_services.h"
#include "zdat_path_native.h"
#include "audio_native_engine.h"
#include "audio_sequence_resource.h"
#include "audio_resource.h"

#define SCREEN_W 400
#define SCREEN_H 240
#define BOTTOM_W 320
#define BOTTOM_H 240

/* Boot-only timing evidence survives a failed display initialization. */
static void boot_trace(const char *stage)
{
    static u64 started;
    static int initialized;
    u64 now=osGetTime();
    FILE *log=fopen("sdmc:/3ds/crashwarped3ds/boot.log",initialized?"a":"w");
    if(!initialized){started=now;initialized=1;}
    if(log){fprintf(log,"%llu ms %s\n",(unsigned long long)(now-started),stage);fclose(log);}
}

/* Shared by allocation and every writer; counts are multiples of a triangle. */
#define TOP_UI_CAPACITY 49152
#define BOTTOM_UI_CAPACITY 24576
#define PROP_VERTEX_CAPACITY 8190

#define CLEAR_COLOR 0x1A2030FF
#define BOTTOM_CLEAR_COLOR 0x0A0E18FF

/* ------------------------------------------------------------------ */
/* level names (original Crash 3 level codes)                          */
/* ------------------------------------------------------------------ */

static const char *const level_names[64] = {
    "-", "-", "WARP ROOM", "DINGODILE", "N. TROPY", "N. GIN", "TINY",
    "DR. NEO CORTEX", "-", "-", "ORIENT EXPRESS", "TOAD VILLAGE",
    "BONE YARD", "TELL NO TALES", "UNDER PRESSURE", "GEE WIZ",
    "DINO MIGHT", "MIDNIGHT RUN", "TOMB TIME", "BYE BYE BLIMPS",
    "ROAD CRASH", "HOG RIDE", "HANG'EM HIGH", "MAD BOMBERS", "TOMB WADER",
    "MAKIN' WAVES", "HIGH TIME", "FUTURE FRENZY", "DEEP TROUBLE",
    "DOUBLE HEADER", "SPHYNXINATOR", "RINGS OF POWER", "ORANGE ASPHALT",
    "SKI CRAZED", "FLAMING PASSION", "GONE TOMORROW", "BUG LITE",
    "AREA 51", "EGGIPUS REX", "HOT COCO", "OPENING CUTSCENE",
    "ANY% ENDING", "100% ENDING", "-", "-", "-", "-", "-", "-", "-", "-",
    "-", "-", "-", "-", "-", "-", "-", "INTERMISSION VORTEX", "GAME OVER",
    "TITLE SCREEN", "-", "-", "-"
};

static const char *const chamber_names[TOTAL_CHAMBERS] = {
    "MEDIEVAL", "ARABIAN", "ORIENTAL", "EGYPTIAN", "FUTURE", "SECRET (NEO-COR)"
};

/* ------------------------------------------------------------------ */
/* graphics state                                                      */
/* ------------------------------------------------------------------ */

static DVLB_s *s_vshader_dvlb;
static shaderProgram_s s_program;
static int s_uLoc_projection;
static int s_uLoc_modelView;
static C3D_RenderTarget *s_target;
static C3D_RenderTarget *s_target_bottom;

static C3D_Mtx s_projection;
static C3D_Mtx s_ortho;

static level_vertex *s_charbuf;
static level_vertex *s_propbuf;
static level_vertex *s_textbuf;
static int s_textn;

static level_vertex s_bottom_buf[BOTTOM_UI_CAPACITY];
static int s_bottom_n;

static char_state s_player;

/* ------------------------------------------------------------------ */
/* text & 2D rendering primitives (TOP SCREEN - 400x240)               */
/* ------------------------------------------------------------------ */

static void text_reset(void)
{
    s_textn = 0;
}

static void text_glyph_ex(int x, int y, int scale, int glyph, float r, float g, float b, int screen_h)
{
    if (glyph < 0 || glyph >= FONT_GLYPH_COUNT)
        return;
    for (int col = 0; col < FONT_W; col++) {
        uint8_t bits = font_glyphs[glyph][col];
        for (int row = 0; row < FONT_H; row++) {
            if (!(bits & (1u << row)))
                continue;
            if (s_textn + 6 > TOP_UI_CAPACITY)
                return;
            float x0 = (float)(x + col * scale);
            int pixel_y = y + row * scale;
            if (x0 < 0 || x0 + scale > SCREEN_W ||
                pixel_y < 0 || pixel_y + scale > screen_h)
                continue;
            float gy = (float)screen_h - ((float)y + (float)row * (float)scale);
            static const int order[6] = { 0, 1, 2, 0, 2, 3 };
            static const float px[4] = { 0, 1, 1, 0 };
            static const float py[4] = { 0, 0, 1, 1 };
            for (int i = 0; i < 6; i++) {
                level_vertex *v = &s_textbuf[s_textn++];
                v->x = x0 + px[order[i]] * (float)scale;
                v->y = gy - py[order[i]] * (float)scale;
                v->z = 0.0f;
                v->r = r; v->g = g; v->b = b;
            }
        }
    }
}

static int text_draw(int x, int y, int scale, const char *s, float r, float g, float b)
{
    int cx = x;
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        int glyph = 0;
        if (ch >= 32 && ch < 128)
            glyph = font_index[ch - 32];
        if (ch == ' ')
            cx += (FONT_W + 1) * scale;
        else {
            text_glyph_ex(cx, y, scale, glyph, r, g, b, SCREEN_H);
            cx += (FONT_W + 1) * scale;
        }
    }
    return cx;
}

static void draw_rect_2d(float x, float y, float w, float h, float r, float g, float b)
{
    if (s_textn + 6 > TOP_UI_CAPACITY)
        return;
    float y0 = (float)SCREEN_H - y;
    float y1 = (float)SCREEN_H - (y + h);
    float x0 = x;
    float x1 = x + w;

    s_textbuf[s_textn++] = (level_vertex){ x0, y0, 0.0f, r, g, b };
    s_textbuf[s_textn++] = (level_vertex){ x1, y0, 0.0f, r, g, b };
    s_textbuf[s_textn++] = (level_vertex){ x1, y1, 0.0f, r, g, b };
    s_textbuf[s_textn++] = (level_vertex){ x0, y0, 0.0f, r, g, b };
    s_textbuf[s_textn++] = (level_vertex){ x1, y1, 0.0f, r, g, b };
    s_textbuf[s_textn++] = (level_vertex){ x0, y1, 0.0f, r, g, b };
}

static void draw_frame_2d(float x, float y, float w, float h, float th, float r, float g, float b)
{
    draw_rect_2d(x, y, w, th, r, g, b);
    draw_rect_2d(x, y + h - th, w, th, r, g, b);
    draw_rect_2d(x, y + th, th, h - th * 2.0f, r, g, b);
    draw_rect_2d(x + w - th, y + th, th, h - th * 2.0f, r, g, b);
}

static void draw_diamond_2d(float cx, float cy, float radius, float angle, float r, float g, float b)
{
    if (s_textn + 6 > TOP_UI_CAPACITY)
        return;
    float c = cosf(angle) * radius;
    float s = sinf(angle) * radius;

    float p0x = cx - s, p0y = (float)SCREEN_H - (cy + c);
    float p1x = cx + c, p1y = (float)SCREEN_H - (cy - s);
    float p2x = cx + s, p2y = (float)SCREEN_H - (cy - c);
    float p3x = cx - c, p3y = (float)SCREEN_H - (cy + s);

    float r_hi = r * 1.25f > 1.0f ? 1.0f : r * 1.25f;
    float g_hi = g * 1.25f > 1.0f ? 1.0f : g * 1.25f;
    float b_hi = b * 1.25f > 1.0f ? 1.0f : b * 1.25f;

    s_textbuf[s_textn++] = (level_vertex){ p0x, p0y, 0.0f, r, g, b };
    s_textbuf[s_textn++] = (level_vertex){ p1x, p1y, 0.0f, r_hi, g_hi, b_hi };
    s_textbuf[s_textn++] = (level_vertex){ p2x, p2y, 0.0f, r, g, b };

    s_textbuf[s_textn++] = (level_vertex){ p0x, p0y, 0.0f, r, g, b };
    s_textbuf[s_textn++] = (level_vertex){ p2x, p2y, 0.0f, r, g, b };
    s_textbuf[s_textn++] = (level_vertex){ p3x, p3y, 0.0f, r * 0.75f, g * 0.75f, b * 0.75f };
}

/* ------------------------------------------------------------------ */
/* mesh binding and scene setup                                        */
/* ------------------------------------------------------------------ */

static void bind_mesh(const level_vertex *verts, int count)
{
    if (count <= 0)
        return;
    C3D_BufInfo *bufInfo = C3D_GetBufInfo();
    BufInfo_Init(bufInfo);
    BufInfo_Add(bufInfo, (void *)verts, sizeof(level_vertex), 2, 0x10);
}

static void draw_mesh(const level_vertex *verts, int count)
{
    if (count <= 0)
        return;
    bind_mesh(verts, count);
    C3D_DrawArrays(GPU_TRIANGLES, 0, count);
}

static void render_text_top(void)
{
    if (s_textn == 0)
        return;
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, s_uLoc_projection, &s_ortho);
    C3D_Mtx mv;
    Mtx_Identity(&mv);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, s_uLoc_modelView, &mv);
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_ALL);
    draw_mesh(s_textbuf, s_textn);
    C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
}

static int render_text_bottom(void)
{
    if (s_bottom_n < 0 || s_bottom_n > BOTTOM_UI_CAPACITY || s_bottom_n % 3)
        return -1;
    for (int i = 0; i < s_bottom_n; i += 3) {
        const level_vertex *v = &s_bottom_buf[i];
        /* The CPU canvas stores bottom-origin Y; citro2d uses top-origin Y. */
        if (!C2D_DrawTriangle(v[0].x, BOTTOM_H-v[0].y, C2D_Color32f(v[0].r,v[0].g,v[0].b,1.0f),
                              v[1].x, BOTTOM_H-v[1].y, C2D_Color32f(v[1].r,v[1].g,v[1].b,1.0f),
                              v[2].x, BOTTOM_H-v[2].y, C2D_Color32f(v[2].r,v[2].g,v[2].b,1.0f), 0.0f))
            return -1;
    }
    C2D_Flush();
    return 0;
}

static int sceneInit(void)
{
    s_vshader_dvlb = DVLB_ParseFile((u32 *)shader_shbin, shader_shbin_size);
    if (!s_vshader_dvlb)
        return -1;
    shaderProgramInit(&s_program);
    shaderProgramSetVsh(&s_program, &s_vshader_dvlb->DVLE[0]);
    C3D_BindProgram(&s_program);

    s_uLoc_projection = shaderInstanceGetUniformLocation(s_program.vertexShader, "projection");
    s_uLoc_modelView  = shaderInstanceGetUniformLocation(s_program.vertexShader, "modelView");

    C3D_AttrInfo *attrInfo = C3D_GetAttrInfo();
    AttrInfo_Init(attrInfo);
    AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 3);   /* v0 = position */
    AttrInfo_AddLoader(attrInfo, 1, GPU_FLOAT, 3);   /* v1 = colour   */

    Mtx_OrthoTilt(&s_ortho, 0.0f, SCREEN_W, 0.0f, SCREEN_H, 0.0f, 1.0f, true);

    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, 0, 0);
    C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);

    s_charbuf = (level_vertex *)linearAlloc(2048 * sizeof(level_vertex));
    s_propbuf = (level_vertex *)linearAlloc(PROP_VERTEX_CAPACITY * sizeof(level_vertex));
    s_textbuf = (level_vertex *)linearAlloc(TOP_UI_CAPACITY * sizeof(level_vertex));
    return s_charbuf && s_propbuf && s_textbuf ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* game state                                                          */
/* ------------------------------------------------------------------ */

typedef enum {
    STATE_LOGOS = 0,
    STATE_TITLE = 1,
    STATE_INTRO = 2,
    STATE_WARP_ROOM = 3,
    STATE_LEVEL_SELECT = 4,
    STATE_LOADING = 5,
    STATE_ENTRANCE = 6,
    STATE_GAMEPLAY = 7,
    STATE_PAUSE = 8,
    STATE_LEVEL_CLEAR = 9,
    STATE_LOAD_FAILED = 10
} GameState;

static GameState s_state = STATE_LOGOS;
static float s_logo_timer = 0.0f;
static int s_menu_index = 0;
static int s_target_level_idx = 0;
static int s_loading_ticks = 0;
static int s_loading_prepared = 0;
static int s_loading_error = 0;
static uint32_t s_loading_native_address = 0;
static char s_loading_stage[48] = "";
static int s_pause_index = 0;
static u32 s_ui_tick = 0;

static int s_active_chamber = 0;
static int s_warp_cursor = 0;
static int s_rendered_verts = 0;
static char s_save_toast[48] = "";
static float s_save_toast_timer = 0.0f;

static int s_elevator_active = 0;
static float s_elevator_target_y = 0.0f;

static float s_cam_yaw = 0.0f;
static float s_cam_pitch = -0.38f;
static float s_cam_dist = 140.0f;
static int s_loaded_id = -1;
static const StoryPortal *s_pending_portal = NULL;
static const StoryPortal *s_loaded_portal = NULL;
static int s_cur_model = 0;
static int s_bazooka_mode = 0;

static void play_level_theme(int level_id);
static void start_entrance(void);
static int native_main_close(void);
static int native_main_open(void);
static int native_main_step(uint64_t now);

static int loading_result(const char *stage,int error)
{
    if(error<0) {
        char message[128];
        s_loading_error=error;
        snprintf(s_loading_stage,sizeof(s_loading_stage),"%s",stage);
        snprintf(message,sizeof(message),"level %02lX: %s failed (%d)",
            (unsigned long)(s_loaded_id<0?0xffffffffu:(uint32_t)s_loaded_id),stage,error);
        boot_trace(message);
    }
    return error;
}

static u64 s_fps_time = 0;
static int s_fps_frames = 0;
static int s_fps_count = 0;

static float s_entrance_timer = 0.0f;
static float s_spawn_x = 0.0f;
static float s_spawn_y = 0.0f;
static float s_spawn_z = 0.0f;
static float s_spawn_yaw = 3.14159265f;

static void spawn_player(void)
{
    float cx = 0.0f, cy = 0.0f, cz = 0.0f;
    if (level_spawn_point(&cx, &cy, &cz) == 0) {
        /* Preserve the NSD checkpoint height unless support is close enough
         * for this controller's 15-unit step. Never snap to a distant floor. */
        float support = level_ground_near(cx, cz, cy, 15.0f, 1.0f);
        if (support > -999999.0f) cy = support;
    } else {
        float minx, miny, minz, maxx, maxy, maxz;
        level_bounds(&minx, &miny, &minz, &maxx, &maxy, &maxz);
        cx = (minx + maxx) * 0.5f;
        cz = (minz + maxz) * 0.5f;
        int h = level_height(cx, cz);
        cy = (h > -999999) ? (float)h : ((miny + maxy) * 0.5f);
    }

    s_cur_model = 0;
    s_spawn_x = cx;
    s_spawn_y = cy;
    s_spawn_z = cz;

    memset(&s_player, 0, sizeof(s_player));
    s_player.x = cx;
    s_player.y = cy;
    s_player.z = cz;
    s_player.onground = 1;
    s_player.can_double_jump = 1;
    s_player.is_slamming = 0;
    s_player.yaw = s_spawn_yaw;
    s_cam_yaw = 0.0f;
    s_cam_pitch = -0.38f;
    s_cam_dist = 140.0f;
}

static int close_level_runtime(void)
{
    int error=native_main_close();
    if(error)return error;
    error=level_native_close();
    if(error)return error;
    error=level_behaviors_close();
    if(error)return error;
    return level_resources_close();
}

static int load_level_by_index(int idx)
{
    const level_info *info = level_get(idx);
    if (!info)
        return loading_result("level archive index",-1);
    uint32_t lid = info->id;
    /* Current streams must stop before their original source session closes.
     * Future resource owners must also release their page/ticket references. */
    audio_stream_stop_all();
    int resource_error=loading_result("release previous level",close_level_runtime());
    if(!resource_error)resource_error=loading_result("original NSF/NSD",level_resources_open(LEVEL_RESOURCES_DEFAULT_DIRECTORY,lid));
    if(!resource_error) {
        resource_error=loading_result("GOOL catalog",level_behaviors_open(LEVEL_BEHAVIORS_DIRECTORY,lid));
        if(resource_error)close_level_runtime();
    }
    if(resource_error) {
        printf("[resources] original level %02lX unavailable (%d)\n",(unsigned long)lid,resource_error);
        snprintf(s_save_toast,sizeof(s_save_toast),"ORIGINAL RESOURCES UNAVAILABLE (%d)",resource_error);
        s_save_toast_timer=5.0f;
        return resource_error;
    }
    /* Release the previous world's texture payload before allocating a second
     * collision VBO; the frame fence has completed before this load state. */
    world_texture_unload();
    int level_error=loading_result("converted world data",level_load(lid));
    if (level_error != 0) {
        close_level_runtime();
        snprintf(s_save_toast,sizeof(s_save_toast),"LEVEL DATA UNAVAILABLE");
        s_save_toast_timer=5.0f;
        return level_error;
    }
    s_loaded_id = (int)lid;
    s_loaded_portal = s_pending_portal && s_pending_portal->level_id == (int)lid ? s_pending_portal : NULL;
    s_pending_portal = NULL;
    if (world_texture_load(lid) != 0)
        printf("[graphics] no usable textured world for level %02lX\n", (unsigned long)lid);
    spawn_player();
    return 0;
}

static int load_level_by_id(int level_id)
{
    int idx = level_find((uint32_t)level_id);
    if (idx < 0) return -1;
    s_menu_index = idx;
    s_target_level_idx = idx;
    s_state = STATE_LOADING;
    s_loading_ticks = 0;
    s_loading_prepared = 0;
    audio_stop_bgm();
    return 0;
}

/* Entry identity must survive archive selection. Secret arrivals require an
 * original route/spawn selector; never silently substitute the normal spawn. */
static int load_story_portal(int chamber, int slot)
{
    const StoryPortal *portal = NULL;
    if (story_resolve_portal(chamber, slot, &portal) != STORY_PORTAL_READY)
        return -1;
    if (portal->entry_kind != STORY_ENTRY_NORMAL && portal->entry_kind != STORY_ENTRY_BOSS) {
        snprintf(s_save_toast, sizeof(s_save_toast), "SECRET ARRIVAL DATA PENDING");
        s_save_toast_timer = 2.5f;
        return -1;
    }
    if (load_level_by_id(portal->level_id) != 0) return -1;
    s_pending_portal = portal;
    return 0;
}

static const StoryPortal *hub_portal_for_archive(int level_id)
{
    int chamber = s_active_chamber == 5 ? 5 : story_get_chamber(level_id);
    for (int slot = 0; slot < story_get_chamber_portal_count(chamber); ++slot) {
        const StoryPortal *portal = story_get_portal(chamber, slot);
        if (portal && portal->level_id == level_id) return portal;
    }
    return NULL;
}

static void trigger_level_clear(void)
{
    s_state = STATE_LEVEL_CLEAR;
    s_player.vx = 0.0f;
    s_player.vy = 0.0f;
    s_player.vz = 0.0f;
    s_player.spin_timer = 0.0f;
    s_player.is_sliding = 0;

    int all_crates = (props_get_broken_crates() >= props_get_total_crates() && props_get_total_crates() > 0);
    uint32_t tt_ms = props_is_time_trial() ? props_get_time_trial_ms() : 0;
    story_on_level_cleared(s_loaded_id, all_crates, tt_ms);
    if (props_is_secret_exit()) {
        story_award_bonus_gem(s_loaded_id);
    }
    snprintf(s_save_toast, sizeof(s_save_toast), "AUTO-SAVED (%lu%%)", (unsigned long)g_save.completion_pct);
    s_save_toast_timer = 2.5f;
    audio_stop_bgm();
    audio_play_sfx(SFX_LEVEL_CLEAR);
}

static int __attribute__((unused)) next_available(int from, int dir)
{
    int n = level_count();
    if (n <= 0)
        return -1;
    for (int i = 1; i <= n; i++) {
        int j = ((from + dir * i) % n + n) % n;
        if (level_get(j))
            return j;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* update player with super powers                                     */
/* ------------------------------------------------------------------ */

static void update_warp_room(float dt);

static void update_player(float dt)
{
    float previous_y = s_player.y;
    int was_grounded = s_player.onground;
    if (s_player.landing_timer > 0.0f)
        s_player.landing_timer = fmaxf(0.0f, s_player.landing_timer - dt);
    circlePosition cp;
    hidCircleRead(&cp);

    u32 held = hidKeysHeld();
    u32 kDown = hidKeysDown();

    /* Fruit Bazooka Aiming Mode: Hold L or X while on the ground */
    if (story_has_power(POWER_BAZOOKA) && s_player.onground && ((held & KEY_L) || (held & KEY_X))) {
        s_bazooka_mode = 1;
        s_player.vx = 0.0f;
        s_player.vz = 0.0f;
        if (cp.dx > 20 || cp.dx < -20)
            s_cam_yaw -= ((float)cp.dx / 150.0f) * 2.0f * dt;
        if (cp.dy > 20 || cp.dy < -20)
            s_cam_pitch += ((float)cp.dy / 150.0f) * 1.5f * dt;
        if (held & KEY_DLEFT)  s_cam_yaw += 1.8f * dt;
        if (held & KEY_DRIGHT) s_cam_yaw -= 1.8f * dt;
        if (held & KEY_DUP)    s_cam_pitch += 1.4f * dt;
        if (held & KEY_DDOWN)  s_cam_pitch -= 1.4f * dt;
        if (s_cam_pitch < -1.1f) s_cam_pitch = -1.1f;
        if (s_cam_pitch > 0.8f)  s_cam_pitch = 0.8f;

        if ((kDown & KEY_B) || (kDown & KEY_Y) || (kDown & KEY_R)) {
            /* build_view looks along negative local Z. */
            float fx = -sinf(s_cam_yaw) * cosf(s_cam_pitch);
            float fy = sinf(s_cam_pitch);
            float fz = -cosf(s_cam_yaw) * cosf(s_cam_pitch);
            props_spawn_projectile(s_player.x, s_player.y + 18.0f, s_player.z, fx, fy, fz);
            audio_play_sfx(SFX_BAZOOKA);
            if (g_stats.wumpa_count > 0)
                g_stats.wumpa_count--;
        }
        props_update(dt, &s_player);
        if (props_check_exit(s_player.x, s_player.y, s_player.z)) {
            trigger_level_clear();
            return;
        }
        return;
    } else {
        s_bazooka_mode = 0;
    }

    float move = 0.0f, strafe = 0.0f;
    if (cp.dx > 24 || cp.dx < -24)
        strafe = (float)cp.dx / 150.0f;
    if (cp.dy > 24 || cp.dy < -24)
        move = -(float)cp.dy / 150.0f;

    if (held & KEY_DLEFT)  strafe = -1.0f;
    if (held & KEY_DRIGHT) strafe = 1.0f;
    if (held & KEY_DUP)    move = -1.0f;
    if (held & KEY_DDOWN)  move = 1.0f;
    if (move > 1.0f) move = 1.0f;
    if (move < -1.0f) move = -1.0f;
    if (strafe > 1.0f) strafe = 1.0f;
    if (strafe < -1.0f) strafe = -1.0f;

    /* Base speed vs Crash Dash super power */
    float speed = 260.0f;
    if (story_has_power(POWER_CRASH_DASH) && (hidKeysHeld() & KEY_R)) {
        speed = 410.0f;
    }

    float fx = sinf(s_cam_yaw), fz = cosf(s_cam_yaw);
    float rx = cosf(s_cam_yaw), rz = -sinf(s_cam_yaw);
    float tx = fx * move + rx * strafe;
    float tz = fz * move + rz * strafe;
    float tl = sqrtf(tx * tx + tz * tz);
    if (tl > 1.0f) { tx /= tl; tz /= tl; tl = 1.0f; }

    /* Acceleration has distance/time^2 units, independent of target speed. */
    float accel = s_player.onground ? (tl > 0.0f ? 1400.0f : 1800.0f) : 500.0f;
    float dvx = tx * speed - s_player.vx;
    float dvz = tz * speed - s_player.vz;
    float dv = sqrtf(dvx * dvx + dvz * dvz);
    float step = accel * dt;
    if (dv > step && dv > 0.0f) {
        dvx *= step / dv;
        dvz *= step / dv;
    }
    s_player.vx += dvx;
    s_player.vz += dvz;
    float sp = sqrtf(s_player.vx * s_player.vx + s_player.vz * s_player.vz);
    if (sp > speed) {
        s_player.vx = s_player.vx / sp * speed;
        s_player.vz = s_player.vz / sp * speed;
        sp = speed;
    }

    level_move_horizontal(&s_player.x, &s_player.z, s_player.y,
                          s_player.vx * dt, s_player.vz * dt,
                          12.0f, 45.0f, 15.0f);

    if (sp > 12.0f) {
        float want = atan2f(s_player.vx, s_player.vz);
        float d = want - s_player.yaw;
        while (d > 3.14159265f) d -= 6.28318531f;
        while (d < -3.14159265f) d += 6.28318531f;
        s_player.yaw += d * (1.0f - expf(-12.0f * dt));
    }

    /* Jump / Double Jump / Belly Flop */


    /* Slide attack and Slide-Jump */
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
        s_player.is_slamming = 0;
        if (kDown & KEY_A) {
            s_player.air_time = 0.0f;
            s_player.landing_timer = 0.0f;
            s_player.vy = 520.0f;
            audio_play_sfx(SFX_JUMP);
        }
    } else {
        /* In mid-air: Super Belly Flop */
        if (story_has_power(POWER_BELLY_FLOP) && (kDown & KEY_L)) {
            s_player.is_slamming = 1;
            s_player.vy = -850.0f;
            s_player.vx = s_player.vz = 0.0f;
            audio_play_sfx(SFX_SPIN);
        }
        /* In mid-air: Double Jump */
        else if (story_has_power(POWER_DOUBLE_JUMP) && (kDown & KEY_A) && s_player.can_double_jump) {
            s_player.vy = 480.0f;
            s_player.can_double_jump = 0;
            audio_play_sfx(SFX_JUMP);
            props_spawn_landing_dust(s_player.x, s_player.y, s_player.z);
        }
    }

    /* Gravity & Death Tornado Spin glide */
    if (!s_player.is_slamming) {
        if (story_has_power(POWER_TORNADO_SPIN) && s_player.spin_timer > 0.0f && !s_player.onground) {
            s_player.vy -= 400.0f * dt;
            if (s_player.vy < -90.0f)
                s_player.vy = -90.0f; /* Hover glide */
        } else {
            s_player.vy -= 1500.0f * dt;
        }
    }

    s_player.y += s_player.vy * dt;
    if (!was_grounded || s_player.vy > 0.0f)
        s_player.air_time += dt;

    /* Void-fall respawn protection: if Crash falls off into a bottomless pit */
    if (s_player.y < s_spawn_y - 1200.0f) {
        audio_play_sfx(SFX_EXPLODE);
        if (g_stats.lives > 0)
            g_stats.lives--;
        props_checkpoint_point(&s_spawn_x, &s_spawn_y, &s_spawn_z);
        start_entrance();
        return;
    }

    /* Ground collision */
    int ground = level_height_below(s_player.x, s_player.z,
                                   previous_y + (s_player.onground ? 15.0f : 0.0f));
    if (ground > -999999 && s_player.vy <= 0.0f && s_player.y <= (float)ground) {
        if (!was_grounded) {
            s_player.landing_timer = CHAR_LAND_DURATION;
            s_player.air_time = 0.0f;
        }
        if (s_player.is_slamming) {
            props_spawn_landing_dust(s_player.x, s_player.y, s_player.z);
            audio_play_sfx(SFX_EXPLODE);
        }
        s_player.y = (float)ground;
        s_player.vy = 0.0f;
        s_player.onground = 1;
        s_player.can_double_jump = 1;
        s_player.is_slamming = 0;
    } else {
        s_player.onground = 0;
    }

    /* One complete leg cycle per stride, instead of ~10 cycles/second. */
    float stride = sp > 180.0f ? 173.3333f : 104.0f;
    if (s_player.onground)
        s_player.walk += sp * dt / stride * 6.28318531f;
    if (s_player.walk > 6.28318531f * 64.0f)
        s_player.walk -= 6.28318531f * 64.0f;

    s_player.anim_time += dt;

    /* Spin attack / Death Tornado */
    if ((kDown & KEY_B) || (kDown & KEY_Y)) {
        s_player.spin_duration = story_has_power(POWER_TORNADO_SPIN) ? 0.65f : 0.35f;
        s_player.spin_timer = s_player.spin_duration;
        audio_play_sfx(SFX_SPIN);
    }
    if (s_player.spin_timer > 0.0f) {
        s_player.spin_timer -= dt;
        if (s_player.spin_timer < 0.0f)
            s_player.spin_timer = 0.0f;
    }

    props_update(dt, &s_player);

    if (s_loaded_id == 0x02) {
        update_warp_room(dt);
    }


    if (props_check_exit(s_player.x, s_player.y, s_player.z)) {
        trigger_level_clear();
        return;
    }
}

static void start_entrance(void)
{
    s_state = STATE_ENTRANCE;
    s_entrance_timer = 0.0f;
    s_player.x = s_spawn_x;
    s_player.z = s_spawn_z;
    s_player.y = s_spawn_y + 70.0f;
    s_player.yaw = s_spawn_yaw;
    s_player.vy = -55.0f;
    s_player.vx = 0.0f;
    s_player.vz = 0.0f;
    s_player.onground = 0;
    s_player.anim_time = 0.0f;
    s_player.air_time = 0.0f;
    s_player.landing_timer = 0.0f;
    s_player.spin_duration = 0.30f;
    s_player.spin_timer = s_player.spin_duration;
    audio_play_sfx(SFX_SPIN);
    props_set_vortex(1, s_player.x, s_spawn_y + 70.0f, s_player.z, 1.0f);
}

static void update_entrance(float dt)
{
    s_entrance_timer += dt;
    s_player.anim_time += dt;

    if (!s_player.onground) {
        s_player.y += s_player.vy * dt;
        s_player.vy -= 260.0f * dt;
        s_player.air_time += dt;
        s_player.spin_timer = s_player.spin_duration - fmodf(s_entrance_timer, s_player.spin_duration);
        props_set_vortex(1, s_player.x, s_spawn_y + 70.0f, s_player.z, 1.0f);

        if (s_player.y <= s_spawn_y) {
            s_player.y = s_spawn_y;
            s_player.vy = 0.0f;
            s_player.onground = 1;
            s_player.spin_timer = 0.0f;
            s_player.landing_timer = CHAR_LAND_DURATION;
            audio_play_sfx(SFX_JUMP);
            props_spawn_landing_dust(s_player.x, s_player.y, s_player.z);
        }
    } else {
        s_player.landing_timer = fmaxf(0.0f, s_player.landing_timer - dt);
        float fade = 1.0f - (s_entrance_timer - 0.5f) / 0.5f;
        if (fade < 0.0f) fade = 0.0f;
        props_set_vortex(fade > 0.05f ? 1 : 0, s_player.x, s_spawn_y + 70.0f, s_player.z, fade);
    }

    u32 kDown = hidKeysDown();
    if (s_entrance_timer >= 1.4f || ((kDown & KEY_A) || (kDown & KEY_START))) {
        props_set_vortex(0, 0, 0, 0, 0.0f);
        s_player.y = s_spawn_y;
        s_player.vy = 0.0f;
        s_player.onground = 1;
        s_player.spin_timer = 0.0f;
        s_player.walk = 0.0f;
        s_state = STATE_GAMEPLAY;
        play_level_theme(s_loaded_id);
    }

    props_update(dt, &s_player);

    if (props_check_exit(s_player.x, s_player.y, s_player.z)) {
        trigger_level_clear();
        return;
    }
}

static void update_warp_room(float dt)
{
    /* 1. Elevator Vertical Transit */
    if (s_elevator_active) {
        s_player.vx = 0.0f;
        s_player.vz = 0.0f;
        float dy = s_elevator_target_y - s_player.y;
        float step = 1000.0f * dt;
        if (fabsf(dy) <= step) {
            s_player.y = s_elevator_target_y;
            s_elevator_active = 0;
            s_spawn_y = s_elevator_target_y;
            audio_play_sfx(SFX_JUMP);
            props_spawn_landing_dust(s_player.x, s_player.y, s_player.z);
        } else {
            s_player.y += (dy > 0.0f ? 1.0f : -1.0f) * step;
        }
        return;
    }

    /* 2. Elevator Step-on Detection */
    if (s_player.onground) {
        float r2 = s_player.x * s_player.x + s_player.z * s_player.z;
        if (r2 < 45.0f * 45.0f) {
            if (s_player.y < 1250.0f) {
                /* Main Floor -> Descend to Chamber 6 if 5+ Relics */
                if (story_is_chamber_unlocked(5)) {
                    s_elevator_active = 1;
                    s_elevator_target_y = 2500.0f;
                    s_active_chamber = 5;
                    audio_play_sfx(SFX_BAZOOKA);
                }
            } else {
                /* Chamber 6 -> Ascend to Main Floor */
                s_elevator_active = 1;
                s_elevator_target_y = 0.0f;
                s_active_chamber = 0;
                audio_play_sfx(SFX_BAZOOKA);
            }
        }
    }

    /* 3. 3D Warp Portal Collision */
    if (!s_elevator_active) {
        int warp_target = props_get_warp_target(s_player.x, s_player.y, s_player.z);
        if (warp_target > 0) {
            const StoryPortal *portal = hub_portal_for_archive(warp_target);
            if (portal && load_story_portal(portal->chamber_idx, portal->slot_idx) == 0) {
                audio_play_sfx(SFX_MENU_SELECT);
                return;
            } else {
                /* Locked portal bounce */
                float fx = sinf(s_player.yaw), fz = cosf(s_player.yaw);
                s_player.x -= fx * 16.0f;
                s_player.z -= fz * 16.0f;
                s_player.vx = -fx * 140.0f;
                s_player.vz = -fz * 140.0f;
                audio_play_sfx(SFX_SPIN);
            }
        }
    }
}

static void update_camera(void)
{
    touchPosition touch;
    static int dragging = 0;
    static int button_gesture = 0;
    static touchPosition last;

    if (hidKeysHeld() & KEY_TOUCH) {
        hidTouchRead(&touch);
        if (hidKeysDown() & KEY_TOUCH)
            button_gesture = touch.py >= 168;
        /* A gesture that began on a button stays reserved until release. */
        if (!button_gesture && touch.py < 168) {
            if (!dragging) {
                dragging = 1;
                last = touch;
            } else {
                float dx = (float)(touch.px - last.px);
                float dy = (float)(touch.py - last.py);
                s_cam_yaw += dx * 0.012f;
                s_cam_pitch += dy * 0.008f;
                if (s_cam_pitch < -1.2f) s_cam_pitch = -1.2f;
                if (s_cam_pitch > 0.9f) s_cam_pitch = 0.9f;
                last = touch;
            }
        } else {
            dragging = 0;
        }
    } else {
        dragging = 0;
        button_gesture = 0;
    }

    /* Keep the chosen view orientation while moving. Turning it toward
     * velocity creates feedback with camera-relative input and bends a held
     * direction into an orbit. Original ZDAT camera paths are not decoded
     * here; this translated view must not be described as a PS1 rail. */

    if (s_cam_pitch < -1.2f) s_cam_pitch = -1.2f;
    if (s_cam_pitch > 0.9f) s_cam_pitch = 0.9f;

    if (!s_bazooka_mode && (hidKeysDown() & KEY_X)) {
        if (s_cam_dist < 165.0f)
            s_cam_dist = 190.0f;
        else if (s_cam_dist < 220.0f)
            s_cam_dist = 250.0f;
        else
            s_cam_dist = 140.0f;
    }
}

/* ------------------------------------------------------------------ */
/* 3D rendering with dynamic chunk culling                             */
/* ------------------------------------------------------------------ */

static void build_view(C3D_Mtx *mv)
{
    float tx = s_player.x;
    float ty = (s_state == STATE_ENTRANCE ? s_spawn_y : s_player.y) + 24.0f;
    float tz = s_player.z;

    Mtx_Identity(mv);
    Mtx_Translate(mv, 0.0f, 0.0f, -s_cam_dist, true);
    Mtx_RotateX(mv, -s_cam_pitch, true);
    Mtx_RotateY(mv, -s_cam_yaw, true);
    Mtx_Translate(mv, -tx, -ty, -tz, true);
}

static void restore_color_pipeline(void)
{
    C3D_BindProgram(&s_program);
    C3D_AttrInfo *attrs = C3D_GetAttrInfo();
    AttrInfo_Init(attrs);
    AttrInfo_AddLoader(attrs, 0, GPU_FLOAT, 3);
    AttrInfo_AddLoader(attrs, 1, GPU_FLOAT, 3);
    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, 0, 0);
    C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
    /* citro2d owns TEV stages and depth/blend state on the bottom pass. */
    for (int stage = 1; stage < 6; stage++) C3D_TexEnvInit(C3D_GetTexEnv(stage));
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
    C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
    C3D_CullFace(GPU_CULL_NONE);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, s_uLoc_projection, &s_projection);
}

static void render_world(C3D_Mtx *mv)
{
    int textured = world_texture_draw(&s_projection, mv, s_player.x, s_player.y, s_player.z, 16000.0f);
    if (textured >= 0) {
        s_rendered_verts = textured;
        restore_color_pipeline();
        return;
    }
    bind_mesh(level_vertices(), level_vertex_count());
    /* The original sky is camera-relative and never writes the depth buffer. */
    C3D_Mtx sky = *mv;
    sky.r[0].w = sky.r[1].w = sky.r[2].w = 0.0f;
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, s_uLoc_modelView, &sky);
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
    for (int i = 0; i < level_model_count(); i++) {
        const level_model_info *model = level_model(i);
        if (model && model->is_skybox && model->count)
            C3D_DrawArrays(GPU_TRIANGLES, model->first, model->count);
    }
    C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, s_uLoc_modelView, mv);
    float projection_rows[4][4],view_rows[4][4];double clip[4][4];
    for(int r=0;r<4;r++) {
        projection_rows[r][0]=s_projection.r[r].x;projection_rows[r][1]=s_projection.r[r].y;
        projection_rows[r][2]=s_projection.r[r].z;projection_rows[r][3]=s_projection.r[r].w;
        view_rows[r][0]=mv->r[r].x;view_rows[r][1]=mv->r[r].y;
        view_rows[r][2]=mv->r[r].z;view_rows[r][3]=mv->r[r].w;
    }
    render_frustum_matrix(projection_rows,view_rows,clip);
    s_rendered_verts = level_render_visible_frustum(s_player.x,s_player.y,s_player.z,16000.0f,clip);
}

static void render_char(C3D_Mtx *mv)
{
    int n = char_build(s_charbuf, 2048, &s_player);
    if(world_texture_draw_actor(&s_projection,mv,s_charbuf,n)>=0){
        restore_color_pipeline();
        return;
    }
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, s_uLoc_modelView, mv);
    draw_mesh(s_charbuf, n);
}

static void render_props(C3D_Mtx *mv)
{
    int textured = world_texture_props_ready();
    uint32_t textured_types=0;
    if(textured){
        for(int i=0;i<props_count();i++){
            const prop_item *item=props_get(i);
            if(!item || !item->active)continue;
            float y=item->y;
            if(item->type==PROP_WUMPA)y+=sinf(item->timer*4.0f)*2.0f;
            int drawn=world_texture_draw_prop(item->type,&s_projection,mv,item->x,y,item->z,
                item->type==PROP_WUMPA?s_cam_yaw:0.0f,item->sx,item->sy,item->sz,item->timer);
            if(drawn>=0 && item->type<32)textured_types|=1u<<item->type;
        }
        prop_item mask;
        if(props_aku_visual(&s_player,s_cam_yaw,&mask))
            world_texture_draw_prop(mask.type,&s_projection,mv,mask.x,mask.y,mask.z,
                mask.rot_y,mask.sx,mask.sy,mask.sz,mask.timer);
        restore_color_pipeline();
    }
    int n = textured ? props_build_missing_mesh(s_propbuf, PROP_VERTEX_CAPACITY,textured_types)
                     : props_build_mesh(s_propbuf, PROP_VERTEX_CAPACITY);
    if (n > 0) {
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, s_uLoc_modelView, mv);
        draw_mesh(s_propbuf, n);
    }
}

/* ------------------------------------------------------------------ */
/* 2D Screens (Title, Intro, Warp Room, Loading, Pause, HUD)          */
/* ------------------------------------------------------------------ */


static void draw_logos_screen(void)
{
    text_reset();
    draw_rect_2d(0, 0, SCREEN_W, SCREEN_H, 0.02f, 0.03f, 0.05f);

    if (s_logo_timer < 2.0f) {
        /* Universal Interactive Studios */
        draw_rect_2d(50, 45, SCREEN_W - 100, 150, 0.05f, 0.08f, 0.16f);
        draw_frame_2d(50, 45, SCREEN_W - 100, 150, 2.0f, 0.25f, 0.55f, 0.95f);
        text_draw(120, 75, 2, "UNIVERSAL", 0.95f, 0.85f, 0.25f);
        text_draw(80, 105, 2, "INTERACTIVE STUDIOS", 0.85f, 0.88f, 0.98f);
        draw_diamond_2d(200, 152, 14.0f, s_logo_timer * 3.0f, 0.35f, 0.70f, 1.0f);
    } else if (s_logo_timer < 4.0f) {
        /* Naughty Dog */
        draw_rect_2d(50, 45, SCREEN_W - 100, 150, 0.12f, 0.04f, 0.04f);
        draw_frame_2d(50, 45, SCREEN_W - 100, 150, 2.0f, 0.95f, 0.35f, 0.15f);
        text_draw(110, 80, 3, "NAUGHTY DOG", 0.98f, 0.35f, 0.12f);
        text_draw(105, 130, 2, "CREATORS OF CRASH", 0.95f, 0.85f, 0.30f);
    } else {
        /* Sony Computer Entertainment */
        draw_rect_2d(40, 45, SCREEN_W - 80, 150, 0.04f, 0.06f, 0.10f);
        draw_frame_2d(40, 45, SCREEN_W - 80, 150, 2.0f, 0.85f, 0.75f, 0.35f);
        text_draw(95, 75, 2, "SONY COMPUTER", 0.98f, 0.85f, 0.30f);
        text_draw(55, 100, 2, "ENTERTAINMENT AMERICA", 0.98f, 0.85f, 0.30f);
        text_draw(75, 145, 1, "ORIGINAL PS1 105% CANONICAL ENGINE", 0.70f, 0.85f, 0.98f);
    }

    float pulse = 0.5f + 0.5f * sinf(s_ui_tick * 0.15f);
    text_draw(115, 215, 1, "PRESS START OR A TO SKIP", 0.60f + 0.30f * pulse, 0.70f, 0.85f);
    render_text_top();
}



static void draw_title_screen(void)
{
    text_reset();

    draw_rect_2d(0, 0, SCREEN_W, 6, 0.95f, 0.50f, 0.10f);
    draw_rect_2d(0, 6, SCREEN_W, 2, 0.98f, 0.78f, 0.20f);
    draw_rect_2d(0, SCREEN_H - 8, SCREEN_W, 2, 0.98f, 0.78f, 0.20f);
    draw_rect_2d(0, SCREEN_H - 6, SCREEN_W, 6, 0.95f, 0.50f, 0.10f);

    draw_rect_2d(20, 20, SCREEN_W - 40, 96, 0.08f, 0.12f, 0.20f);
    draw_frame_2d(20, 20, SCREEN_W - 40, 96, 2.0f, 0.95f, 0.65f, 0.15f);

    text_draw(80, 30, 3, "CRASH BANDICOOT", 0.98f, 0.75f, 0.18f);
    text_draw(155, 58, 4, "WARPED", 1.0f, 0.32f, 0.12f);
    text_draw(95, 94, 2, "NATIVE OLD 3DS XL PORT", 0.75f, 0.88f, 0.95f);

    float pulse = 0.5f + 0.5f * sinf(s_ui_tick * 0.10f);
    if ((s_ui_tick / 24) % 2 == 0) {
        text_draw(90, 138, 2, "PRESS START OR A TO PLAY",
                  0.70f + 0.30f * pulse, 0.85f + 0.15f * pulse, 0.20f + 0.40f * pulse);
    }

    float spin = (float)s_ui_tick * 0.05f;
    draw_diamond_2d(70, 145, 9.0f, spin, 0.2f, 0.75f, 1.0f);
    draw_diamond_2d(330, 145, 9.0f, -spin, 0.2f, 0.75f, 1.0f);

    draw_rect_2d(36, 172, SCREEN_W - 72, 48, 0.05f, 0.08f, 0.14f);
    draw_frame_2d(36, 172, SCREEN_W - 72, 48, 1.0f, 0.35f, 0.45f, 0.60f);

    text_draw(48, 178, 1, "A / START : TIME TWISTER WARP ROOM", 0.85f, 0.88f, 0.95f);
    text_draw(48, 190, 1, "Y : LEVEL SELECT (DEBUG CATALOG)", 0.75f, 0.80f, 0.88f);
    text_draw(48, 202, 1, "SELECT : EXIT HOMEBREW", 0.65f, 0.70f, 0.78f);

    render_text_top();
}

static void draw_intro_screen(void)
{
    text_reset();

    draw_rect_2d(0, 0, SCREEN_W, SCREEN_H, 0.06f, 0.08f, 0.14f);
    draw_rect_2d(0, 0, SCREEN_W, 30, 0.12f, 0.18f, 0.28f);
    draw_rect_2d(0, 30, SCREEN_W, 2, 0.95f, 0.55f, 0.15f);

    text_draw(95, 8, 2, "PROLOGUE: TIME TWISTER", 0.98f, 0.80f, 0.20f);

    draw_rect_2d(25, 48, SCREEN_W - 50, 140, 0.08f, 0.11f, 0.18f);
    draw_frame_2d(25, 48, SCREEN_W - 50, 140, 2.0f, 0.35f, 0.55f, 0.85f);

    text_draw(40, 60, 2, "AKU AKU:", 0.95f, 0.75f, 0.20f);
    text_draw(40, 80, 1, "\"Crash, Coco! Uka Uka has been freed from his temple prison!", 0.85f, 0.88f, 0.95f);
    text_draw(40, 94, 1, "Dr. Neo Cortex and Dr. N. Tropy have built the Time Twister", 0.85f, 0.88f, 0.95f);
    text_draw(40, 108, 1, "machine to gather the 25 Crystals from their original epochs!", 0.85f, 0.88f, 0.95f);
    text_draw(40, 126, 1, "You must journey across time through the chambers, collect", 0.95f, 0.85f, 0.35f);
    text_draw(40, 140, 1, "the Crystals before they do, and defeat their minions!\"", 0.95f, 0.85f, 0.35f);

    float pulse = 0.5f + 0.5f * sinf(s_ui_tick * 0.12f);
    text_draw(110, 205, 2, "PRESS A OR START TO ENTER",
              0.80f + 0.20f * pulse, 0.85f + 0.15f * pulse, 0.30f);

    render_text_top();
}

static void draw_warp_room_screen(void)
{
    text_reset();

    /* Header banner */
    draw_rect_2d(0, 0, SCREEN_W, 36, 0.08f, 0.12f, 0.20f);
    draw_rect_2d(0, 36, SCREEN_W, 2, 0.95f, 0.55f, 0.15f);

    char title[64];
    snprintf(title, sizeof(title), "TIME TWISTER: CHAMBER %d - %s",
             s_active_chamber + 1, chamber_names[s_active_chamber]);
    text_draw(40, 10, 2, title, 0.98f, 0.85f, 0.22f);

    /* 5 Chamber level buttons + Boss */
    for (int i = 0; i < LEVELS_PER_CHAMBER; i++) {
        const StoryPortal *portal = story_get_portal(s_active_chamber, i);
        if (!portal) continue;
        int lid = portal->level_id;
        int sel = (s_warp_cursor == i);
        int ypos = 46 + i * 26;
        int unlocked = story_is_chamber_level_unlocked(s_active_chamber, i);

        float bg_r = sel ? (unlocked ? 0.20f : 0.30f) : 0.08f;
        float bg_g = sel ? (unlocked ? 0.32f : 0.15f) : 0.12f;
        float bg_b = sel ? (unlocked ? 0.52f : 0.15f) : 0.20f;
        draw_rect_2d(25, ypos, SCREEN_W - 50, 22, bg_r, bg_g, bg_b);
        draw_frame_2d(25, ypos, SCREEN_W - 50, 22, 1.0f, sel ? 0.98f : 0.30f, sel ? 0.80f : 0.40f, sel ? 0.25f : 0.55f);

        char ltxt[64];
        const char *nm = level_names[lid & 0x3F];
        snprintf(ltxt, sizeof(ltxt), "%d. %s", i + 1 + s_active_chamber * 5, nm ? nm : "???");
        text_draw(40, ypos + 4, 2, ltxt, unlocked ? (sel ? 0.98f : 0.75f) : 0.45f,
                                         unlocked ? (sel ? 0.98f : 0.80f) : 0.45f,
                                         unlocked ? (sel ? 0.40f : 0.85f) : 0.50f);

        if (!unlocked) {
            if (s_active_chamber == 5) {
                char req_txt[32];
                snprintf(req_txt, sizeof(req_txt), "%d RELICS", portal->required_relics);
                text_draw(SCREEN_W - 75, ypos + 6, 1, req_txt, 0.95f, 0.40f, 0.25f);
            } else {
                text_draw(SCREEN_W - 65, ypos + 6, 1, "LOCKED", 0.70f, 0.40f, 0.35f);
            }
        } else if (g_save.levels[lid].crystal) {
            draw_diamond_2d(SCREEN_W - 55, ypos + 11, 6.0f, 0.0f, 0.85f, 0.25f, 0.98f);
            text_draw(SCREEN_W - 45, ypos + 6, 1, "CRYSTAL", 0.85f, 0.35f, 0.98f);
        } else {
            text_draw(SCREEN_W - 45, ypos + 6, 1, "NEED", 0.50f, 0.55f, 0.65f);
        }
    }

    /* Only the five main chambers have a sixth boss pad. */
    if (story_get_chamber_portal_count(s_active_chamber) > LEVELS_PER_CHAMBER) {
        int boss_sel = (s_warp_cursor == 5);
        int boss_unlocked = story_is_boss_unlocked(s_active_chamber);
        int boss_lid = story_get_boss_level_id(s_active_chamber);
        int boss_y = 46 + 5 * 26;

        draw_rect_2d(25, boss_y, SCREEN_W - 50, 24, boss_sel ? 0.45f : 0.20f, 0.10f, 0.12f);
        draw_frame_2d(25, boss_y, SCREEN_W - 50, 24, 1.0f, 0.95f, 0.35f, 0.25f);

        char btxt[64];
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
    }

    /* Footer guide */
    draw_rect_2d(0, SCREEN_H - 24, SCREEN_W, 24, 0.06f, 0.09f, 0.15f);
    text_draw(15, SCREEN_H - 16, 1,
              "L/R : CHANGE CHAMBER (1-6)    D-PAD : SELECT    A : ENTER PORTAL",
              0.80f, 0.85f, 0.92f);

    render_text_top();
}

static void draw_loading_screen(int target_idx, int ticks)
{
    text_reset();

    draw_rect_2d(0, 0, SCREEN_W, SCREEN_H, 0.06f, 0.08f, 0.13f);

    draw_rect_2d(0, 0, SCREEN_W, 28, 0.10f, 0.14f, 0.22f);
    draw_rect_2d(0, 28, SCREEN_W, 2, 0.95f, 0.55f, 0.15f);
    text_draw(85, 8, 2, "CRASH BANDICOOT: WARPED", 0.98f, 0.75f, 0.18f);

    draw_rect_2d(30, 52, SCREEN_W - 60, 142, 0.08f, 0.11f, 0.18f);
    draw_frame_2d(30, 52, SCREEN_W - 60, 142, 2.0f, 0.95f, 0.60f, 0.15f);

    char load_txt[32];
    int dot_count = (ticks / 8) % 4;
    snprintf(load_txt, sizeof(load_txt), "WARPING%.*s", dot_count, "...");
    text_draw(145, 70, 3, load_txt, 1.0f, 0.88f, 0.25f);

    const level_info *info = level_get(target_idx);
    uint32_t lid = info ? info->id : 0;
    const char *nm = level_names[lid & 0x3F];
    char lvl_line[64];
    snprintf(lvl_line, sizeof(lvl_line), "LEVEL 0x%02X: %s", (unsigned)lid, nm ? nm : "???");
    text_draw(65, 105, 2, lvl_line, 0.90f, 0.92f, 0.98f);

    float spin = (float)ticks * 0.15f;
    draw_diamond_2d(200, 142, 14.0f, spin, 0.25f, 0.85f, 0.98f);
    draw_diamond_2d(200, 142, 7.0f, -spin * 1.5f, 0.98f, 0.85f, 0.20f);

    draw_frame_2d(55, 168, SCREEN_W - 110, 12, 1.0f, 0.40f, 0.50f, 0.65f);
    float pbar_w = ((float)(ticks % 60) / 60.0f) * (SCREEN_W - 114.0f);
    if (pbar_w > 0.0f)
        draw_rect_2d(57, 170, pbar_w, 8, 0.95f, 0.55f, 0.15f);

    text_draw(75, 212, 1, "STREAMING NSF CHUNKS TO PICA200 MEMORY...", 0.60f, 0.65f, 0.75f);

    render_text_top();
}



static void draw_crosshair_2d(float cx, float cy)
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
    if (props_boss_active()) {
        audio_play_bgm(BGM_BOSS);
        return;
    }
    BgmTheme theme = level_get_bgm_theme(level_id);
    if (theme != BGM_NONE)
        audio_play_bgm(theme);
    else
        audio_stop_bgm();
}

static void draw_hud(void)
{
    /* Counters, portal details and help belong to the touch screen. Keep
     * only the aiming reticle attached to the action on the top screen. */
    text_reset();
    if (s_bazooka_mode && s_state == STATE_GAMEPLAY)
        draw_crosshair_2d(SCREEN_W * 0.5f, SCREEN_H * 0.5f);
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
    int chamber_cur = story_get_chamber(s_loaded_id);
    int is_main_chamber = (chamber_cur >= 0 && chamber_cur < 5);
    int has_crystal = (s_loaded_id >= 0 && s_loaded_id < 64) ? g_save.levels[s_loaded_id].crystal : 0;
    if (is_main_chamber) {
        draw_diamond_2d(45, 64, 8.0f, s_ui_tick * 0.05f, 0.85f, 0.25f, 0.98f);
        if (has_crystal) {
            text_draw(60, 58, 2, "POWER CRYSTAL RETRIEVED!", 0.92f, 0.40f, 0.98f);
        } else {
            text_draw(60, 58, 2, "POWER CRYSTAL MISSED", 0.60f, 0.60f, 0.70f);
        }
    } else {
        text_draw(60, 58, 2, "NO POWER CRYSTAL IN THIS ZONE", 0.50f, 0.60f, 0.70f);
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
        snprintf(tt_txt, sizeof(tt_txt), "BEST TIME: %02u:%02u:%02u", (unsigned)mins, (unsigned)secs, (unsigned)cs);
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
    } else if (s_loaded_id >= 0 && s_loaded_id < 64 && g_save.levels[s_loaded_id].bonus_gem) {
        GemColor col = story_get_level_colored_gem(s_loaded_id);
        const char *gname = story_get_colored_gem_name(col);
        char gtxt[64];
        snprintf(gtxt, sizeof(gtxt), "%s AWARDED!", gname);
        float gr = 0.95f, gg = 0.95f, gb = 0.95f;
        if (col == GEM_COLOR_BLUE) { gr = 0.20f; gg = 0.50f; gb = 1.0f; }
        else if (col == GEM_COLOR_RED) { gr = 1.0f; gg = 0.20f; gb = 0.20f; }
        else if (col == GEM_COLOR_GREEN) { gr = 0.20f; gg = 1.0f; gb = 0.20f; }
        else if (col == GEM_COLOR_YELLOW) { gr = 1.0f; gg = 0.95f; gb = 0.20f; }
        else if (col == GEM_COLOR_PURPLE) { gr = 0.85f; gg = 0.25f; gb = 0.98f; }
        draw_diamond_2d(45, 172, 8.0f, s_ui_tick * 0.05f, gr, gg, gb);
        text_draw(60, 168, 1, gtxt, gr, gg, gb);
    }

    /* Footer guide */
    draw_rect_2d(0, SCREEN_H - 24, SCREEN_W, 24, 0.06f, 0.09f, 0.15f);
    float pulse = 0.5f + 0.5f * sinf(s_ui_tick * 0.12f);
    text_draw(50, SCREEN_H - 17, 2, "PRESS A OR START TO RETURN TO WARP ROOM",
              0.80f + 0.20f * pulse, 0.85f + 0.15f * pulse, 0.25f + 0.35f * pulse);

    render_text_top();
}

static void draw_menu(void)
{
    text_reset();

    draw_rect_2d(0, 0, SCREEN_W, 36, 0.08f, 0.12f, 0.20f);
    draw_rect_2d(0, 36, SCREEN_W, 2, 0.95f, 0.55f, 0.15f);
    text_draw(80, 6, 2, "CRASH BANDICOOT 3: WARPED", 0.98f, 0.75f, 0.18f);

    int n = level_count();
    char hdr[48];
    snprintf(hdr, sizeof(hdr), "SELECT LEVEL (%d / %d)", s_menu_index + 1, n);
    text_draw(120, 22, 1, hdr, 0.75f, 0.88f, 0.98f);

    int first = s_menu_index - 4;
    for (int i = 0; i < 9; i++) {
        int idx = first + i;
        if (idx < 0 || idx >= n)
            continue;
        const level_info *info = level_get(idx);
        char line[80];
        char name[40];
        const char *nm = level_names[info->id & 0x3F];
        int k = 0;
        for (const char *p = nm; *p && k < 36; p++)
            name[k++] = *p;
        name[k] = 0;
        snprintf(line, sizeof(line), "%02d [%c] 0x%02X %s",
                 idx + 1, level_code(info->id), (unsigned)info->id, name);

        int sel = (idx == s_menu_index);
        int ypos = 46 + i * 18;
        if (sel) {
            draw_rect_2d(25, ypos - 2, SCREEN_W - 50, 16, 0.18f, 0.28f, 0.45f);
            draw_frame_2d(25, ypos - 2, SCREEN_W - 50, 16, 1.0f, 0.95f, 0.75f, 0.20f);
            text_draw(32, ypos, 2, ">", 0.25f, 0.98f, 0.35f);
            text_draw(50, ypos, 2, line, 0.25f, 0.98f, 0.35f);
        } else {
            text_draw(50, ypos, 2, line, 0.65f, 0.70f, 0.78f);
        }
    }

    draw_rect_2d(0, SCREEN_H - 22, SCREEN_W, 22, 0.08f, 0.11f, 0.18f);
    draw_rect_2d(0, SCREEN_H - 22, SCREEN_W, 1, 0.35f, 0.45f, 0.60f);
    text_draw(15, SCREEN_H - 16, 1,
              "D-PAD: SCROLL (+/-5)   A/START: LOAD LEVEL   B: TITLE SCREEN",
              0.80f, 0.85f, 0.92f);

    render_text_top();
}

/* ------------------------------------------------------------------ */
/* Interactive Dual-Screen Bottom HUD (Citro3D / 320x240)             */
/* ------------------------------------------------------------------ */

static bottom_ui_page s_bottom_page = BOTTOM_PAGE_STATUS;

static int draw_bottom_screen(void)
{
    int trial_lid = s_loaded_id;
    int trial_supported_route = 1;
    char context[64], detail[64];
    const char *context_text = NULL, *detail_text = NULL;
    const char *name = s_loaded_id >= 0 ? level_names[s_loaded_id & 0x3F] : "WARPED";
    if (s_state == STATE_WARP_ROOM) {
        const StoryPortal *selected = story_get_portal(s_active_chamber, s_warp_cursor);
        int lid = selected ? selected->level_id : -1;
        trial_lid = lid;
        trial_supported_route = selected && selected->entry_kind<=STORY_ENTRY_BOSS;
        name = "TIME TWISTER";
        snprintf(context, sizeof(context), "CHAMBER %d / PORTAL %d", s_active_chamber+1, s_warp_cursor+1);
        snprintf(detail, sizeof(detail), "%s%s", lid>=0 && lid<64 && level_names[lid] ? level_names[lid] : "NO PORTAL",
                 selected && selected->entry_kind>STORY_ENTRY_BOSS ? " / SECRET ROUTE PENDING" : "");
        context_text=context; detail_text=detail;
    } else if (s_state == STATE_TITLE || s_state == STATE_LOGOS || s_state == STATE_INTRO) {
        trial_lid=-1;
        name="CRASH WARPED";
        context_text="TIME TWISTER / ADVENTURE";
        detail_text="A OR START TO CONTINUE";
    } else if (s_loaded_id == 0x02) {
        float dist=999.0f;
        const prop_item *portal=props_get_nearest_portal(s_player.x,s_player.y,s_player.z,&dist);
        if(portal && dist<85.0f && portal->value>0 && portal->value<64) {
            int lid=portal->value;
            snprintf(context,sizeof(context),"%s",level_names[lid]?level_names[lid]:"PORTAL");
            const StoryPortal *entry=hub_portal_for_archive(lid);
            trial_lid=lid;
            trial_supported_route=entry && entry->entry_kind<=STORY_ENTRY_BOSS;
            const StoryPortal *ready=NULL;
            int open=entry && story_resolve_portal_id(entry->portal_id,&ready)==STORY_PORTAL_READY;
            const char *status=!open?"LOCKED":(entry->entry_kind>STORY_ENTRY_BOSS?"ROUTE PENDING":"OPEN");
            snprintf(detail,sizeof(detail),"%s  CRYSTAL %s  GEM %s",status,
                g_save.levels[lid].crystal?"YES":"NO",g_save.levels[lid].clear_gem?"YES":"NO");
        } else {
            snprintf(context,sizeof(context),"CHAMBER %d: %s",s_active_chamber+1,chamber_names[s_active_chamber]);
            snprintf(detail,sizeof(detail),"APPROACH A PORTAL TO VIEW ITS STATUS");
        }
        context_text=context; detail_text=detail;
    }
    int trial_available = trial_supported_route && story_has_relic_targets(trial_lid);
    uint32_t trial_targets[3]={0,0,0};
    if(trial_available)
        story_get_canonical_relic_times(trial_lid,&trial_targets[0],&trial_targets[1],&trial_targets[2]);
    if(s_bottom_page==BOTTOM_PAGE_TRIAL && trial_lid>=0 && trial_lid<64 && level_names[trial_lid])
        name=level_names[trial_lid];
    bottom_ui_state state = {
        .level_name = name,
        .page = s_bottom_page,
        .trial_targets_available = trial_available,
        .trial_targets_ms = {trial_targets[0],trial_targets[1],trial_targets[2]},
        .saved_relic_tier = trial_available ? g_save.levels[trial_lid].relic_tier : RELIC_NONE,
        .best_time_ms = trial_available ? g_save.levels[trial_lid].best_time_ms : 0,
        .context = context_text,
        .context_detail = detail_text,
        .paused = s_state == STATE_PAUSE,
        .pause_index = s_pause_index,
        .aiming = s_bazooka_mode,
        .aku_aku = g_stats.aku_aku_tier,
        .time_trial = props_is_time_trial(),
        .time_trial_ms = props_get_time_trial_ms(),
        .boss_active = props_boss_active(),
        .boss_hp = props_boss_hp(),
        .boss_max_hp = (s_loaded_id == 0x05 || s_loaded_id == 0x07) ? 5 : 3,
        .message = s_save_toast_timer > 0.0f ? s_save_toast : NULL,
        .wumpa = g_stats.wumpa_count,
        .boxes = g_stats.crates_broken,
        .boxes_total = g_stats.crates_total,
        .lives = g_stats.lives,
        .crystals = g_save.crystals_count,
        .gems = g_save.gems_count,
        .relics = g_save.relics_count,
        .powers = g_save.unlocked_powers,
        .completion = g_save.completion_pct
    };
    s_bottom_n = bottom_ui_build(s_bottom_buf, BOTTOM_UI_CAPACITY, &state);
    return render_text_bottom();
}

/* ------------------------------------------------------------------ */
/* level source                                                        */
/* ------------------------------------------------------------------ */

/* Kept only for the existing prototype reference regressions. */
static void update_player(float dt) __attribute__((unused));
static void update_camera(void) __attribute__((unused));
static void update_entrance(float dt) __attribute__((unused));
static void render_char(C3D_Mtx *mv) __attribute__((unused));
static void render_props(C3D_Mtx *mv) __attribute__((unused));

/* The application owns native globals and continuations. A missing original
 * callee is a visible, sticky boot fault, never permission to run the old
 * approximate controller or acknowledge an unfinished native operation. */
enum { NATIVE_MAIN_UNIMPLEMENTED=-240, NATIVE_MAIN_ROUTES=4, NATIVE_MAIN_PAGES=32 };
static struct {
    gool_domain *domain;
    gool_native_registry *registry;
    gool_native_factory factory;
    gool_native_factory_slot slots[GOOL_NATIVE_POOL_COUNT+1];
    gool_native_route_frame world_routes[NATIVE_MAIN_ROUTES];
    gool_native_route_frame retire_routes[NATIVE_MAIN_ROUTES];
    gool_native_route_frame pad_routes[NATIVE_MAIN_ROUTES];
    gool_native_route_frame creation_routes[NATIVE_MAIN_ROUTES];
    native_runtime world;
    gool_native_controller controller;
    gool_native_zone_retirement retirement;
    gool_native_zone_transition transition;
    gool_native_activation_query activation_query;
    gool_native_address address;
    gool_creation_services creation;
    gool_create create;
    gool_native_state creation_state;
    gool_native_router creation_router;
    uint32_t activation_zone,activation_marker,activation_index,activation_phase,activation_row;
    uint32_t activation_word,activation_key,activation_result;
    int activation_busy;
    gool_native_geometry_services geometry;
    audio_native_services sound;
    AudioNativeEngine audio;
    AudioResource adio[24];
    AudioSequenceResource sequence;
    zdat_resources miscellaneous;
    uint8_t miscellaneous_storage[ZDAT_RESOURCES_SLOTS*ZDAT_RESOURCE_BYTES];
    struct {zdat_resources *bank;zdat_resource_lease lease;uint32_t address,reference;} resources[16];
    nsf_resource_ticket resource_ticket;
    nsf_resource_view resource_view;
    zdat_resources *resource_bank;
    uint32_t resource_reference,resource_eid,resource_phase;
    uint32_t *resource_result;
    int resource_owned,resource_barrier;
    struct {uint32_t node,instruction,command,address,result;int active,pending;} script_resource;
    /* Original22DA0 pins pages, independently of relocated actor/zone copies.
     * 32 resident pages are a port budget, not an inferred PS1 pool size. */
    struct {uint32_t cid;uint8_t soft,total;int valid;} pages[NATIVE_MAIN_PAGES];
    uint8_t page_storage[NATIVE_MAIN_PAGES][NSF_PAGE_BYTES];
    uint32_t camera_state[0x50/4];
    uint32_t projection_h;
    struct {
        uint32_t database,position,direction,list,index,count,pass,eid,cid,slot;
        nsf_resource_ticket ticket;nsf_resource_view view;
        int busy,owned,selected;
    } prefetch;
    gool_domain_lease mappings[17];
    zdat_resource_lease zone_lease;
    uint32_t mapped,zone,zone_eid,database,phase,unimplemented;
    uint32_t completed_updates;
    uint32_t globals[0x330/4],gp_before[0x218/4],gp_after[0x5b0/4];
    uint32_t zone_globals[0x300/4],activation[256*3],usage[0xc00/4];
    uint32_t timers[16],timer_pointers[6],scratch[0xa00/4],voices[0x800/4];
    uint32_t visible_lists[2][65536/4];
    uint32_t save_state[0x200/4],replay_usage[0x200/4];
    uint64_t tick,clock_origin,last_frame;
    int opened,status,zone_retained;
} s_native;

/* File-backed engine data from audited SCUS_942.44; native BSS stays zero. */
static void native_initial_data(void)
{
    static const uint32_t before[134]={
        [2]=0x00001000u,
        [3]=0x00200000u,
        [5]=0x64616564u,
        [6]=0x0000000au,
        [7]=0x5d58255bu,
        [9]=0x00642570u,
        [11]=0x00000001u,
        [12]=0x65637268u,
        [13]=0x0000000au,
        [14]=0x0a656468u,
        [17]=0x00800100u,
        [18]=0x6c206564u,
        [19]=0x00000a73u,
        [20]=0x6c206564u,
        [21]=0x0000000au,
        [22]=0x6d656d21u,
        [23]=0x0000000au,
        [24]=0x30337243u,
        [25]=0x00000054u,
        [29]=0xffff0100u,
        [30]=0x0000ffffu,
        [32]=0x20637766u,
        [33]=0x000a6425u,
        [37]=0x00000001u,
        [40]=0x00808080u,
        [41]=0x00008000u,
        [43]=0x8005c488u,
        [45]=0x00000001u,
        [46]=0x31746157u,
        [47]=0x00000052u,
        [48]=0x32746157u,
        [49]=0x00000052u,
        [50]=0x36746157u,
        [51]=0x00000052u,
        [52]=0x37746157u,
        [53]=0x00000052u,
        [54]=0x38746157u,
        [55]=0x00000052u,
        [56]=0x33746157u,
        [57]=0x00000052u,
        [58]=0x34746157u,
        [59]=0x00000052u,
        [60]=0x35746157u,
        [61]=0x00000052u,
        [62]=0x39746157u,
        [63]=0x00000052u,
        [64]=0x41746157u,
        [65]=0x00000052u,
        [66]=0x42746157u,
        [67]=0x00000052u,
        [68]=0x43746157u,
        [69]=0x00000052u,
        [70]=0x44746157u,
        [71]=0x00000052u,
        [72]=0x45746157u,
        [73]=0x00000052u,
        [74]=0x31726554u,
        [75]=0x00000052u,
        [76]=0x32726554u,
        [77]=0x00000052u,
        [78]=0x33726554u,
        [79]=0x00000052u,
        [80]=0x34726554u,
        [81]=0x00000052u,
        [82]=0x0000f424u,
        [83]=0x3f6d6376u,
        [84]=0x00000058u,
        [85]=0x3f3f6270u,
        [86]=0x00000042u,
        [87]=0x00000001u,
        [88]=0x00000001u,
        [89]=0x30304443u,
        [90]=0x00000031u,
        [91]=0x0a736672u,
        [93]=0x0000002eu,
        [94]=0x00002e2eu,
        [95]=0x50524157u,
        [98]=0x0000001eu,
        [103]=0xffffffffu,
        [104]=0xffffffffu,
        [107]=0x1f800040u,
        [109]=0x00003039u,
        [110]=0x000baec2u,
    };
    memcpy(s_native.gp_before,before,sizeof(before));
    static const uint32_t after[347]={
    };
    memcpy(s_native.gp_after,after,sizeof(after));
}

static int native_missing(uint32_t address)
{
    if(!s_native.unimplemented) {
        s_native.unimplemented=address;
        printf("[native] unresolved service %08lX, phase %lu\n",
            (unsigned long)address,(unsigned long)s_native.phase);
        char message[96];
        snprintf(message,sizeof(message),"native service %08lX unresolved, boot phase %lu",
            (unsigned long)address,(unsigned long)s_native.phase);
        boot_trace(message);
    }
    return NATIVE_MAIN_UNIMPLEMENTED;
}
static int native_map(uint32_t address,void *bytes,uint32_t size)
{
    if(s_native.mapped>=sizeof(s_native.mappings)/sizeof(s_native.mappings[0]))
        return GOOL_DOMAIN_FULL;
    int error=gool_domain_map_mutable(s_native.domain,address,bytes,size,
        &s_native.mappings[s_native.mapped]);
    if(!error)s_native.mapped++;
    return error;
}
static int native_read(uint32_t address,uint32_t *value)
{return gool_native_registry_read(s_native.registry,address,value);}
static int native_write(uint32_t address,uint32_t value)
{return gool_native_registry_write(s_native.registry,address,value);}
static int native_operand(void *context,uint32_t node,uint32_t operand,uint32_t *address)
{(void)context;return gool_native_address_resolve(&s_native.address,node,operand,address);}
static int native_sound_play(void *context,uint32_t node,uint32_t source,int16_t volume,int32_t *result)
{
    (void)context;
    int error=audio_native_engine_play(&s_native.audio,node,source,volume,result);
    return error==AUDIO_VOICE_SERVICE_REQUIRED?native_missing(0x80033864u):error;
}
static int native_sound_control(void *context,uint32_t target,uint32_t options,uint32_t source,uint32_t node,int32_t *result)
{
    (void)context;
    int error=audio_native_engine_control(&s_native.audio,target,options,source,node,result);
    return error==AUDIO_VOICE_SERVICE_REQUIRED?native_missing(0x80033c54u):error;
}
static int native_stop_audio(void *context,uint32_t node)
{(void)context;return audio_native_engine_stop(&s_native.audio,node);}
static int native_cancel_audio(void *context,uint32_t node)
{
    /* Native audio commands are synchronous. A failed SPU/NDSP call cannot
     * leave a borrowed callback buffer; stopping still scans real ownership. */
    return native_stop_audio(context,node);
}
static int native_notify(void *context,uint32_t node,uint32_t signal)
{
    (void)context;
    int error=s_native.retirement.services.signal(&s_native.retirement,node,signal);
    return error==1?GOOL_DESTROY_PROVIDER_PENDING:error;
}
static int native_pad(void *context,uint32_t function,const uint32_t args[3],uint32_t argc,uint32_t *result)
{
    (void)context;(void)argc;
    if(function==0x8004a748u||function==0x80048e68u) {*result=0;return 0;}
    if(function==0x80048f00u) {
        if(args[0]){*result=0;return 0;}
        /* Old3DS has one local pad. Publish a connected PS1 digital packet
         * using actual HID state; absent rumble/analog negotiation stays off. */
        uint32_t held=hidKeysHeld(),buttons=0;
        const uint32_t keys[12]={KEY_SELECT,KEY_START,KEY_DUP,KEY_DRIGHT,KEY_DDOWN,KEY_DLEFT,
            KEY_L,KEY_R,KEY_X,KEY_A,KEY_B,KEY_Y};
        const uint16_t bits[12]={0x100,0x800,0x1000,0x2000,0x4000,0x8000,
            4,8,0x10,0x20,0x40,0x80};
        for(unsigned i=0;i<12;i++)if(held&keys[i])buttons|=bits[i];
        circlePosition circle;hidCircleRead(&circle);
        if(circle.dx>32)buttons|=0x2000;
        if(circle.dx<-32)buttons|=0x8000;
        if(circle.dy>32)buttons|=0x1000;
        if(circle.dy<-32)buttons|=0x4000;
        uint16_t raw=(uint16_t)~((buttons>>8)|(buttons<<8));
        int error=gool_domain_write32(s_native.domain,0x80065bccu,
            0x4100u|((uint32_t)raw<<16));
        if(error)return error;
        *result=6;return 0;
    }
    if(function==0x80048fc0u) {*result=4;return 0;}
    if(function==0x8004926cu||function==0x80049234u||function==0x800492b4u) {
        /* This HID backend advertises digital input and no rumble device. */
        *result=0;return 0;
    }
    return native_missing(function);
}
static int native_protected_update(void *context,uint32_t node)
{
    (void)context;(void)node;
    int error;
    if(!s_native.controller.active) {
        error=gool_native_controller_begin(&s_native.controller,0,0);
        if(error!=1)return error?error:GOOL_NATIVE_CONTROLLER_INVALID;
    }
    return gool_native_controller_step(&s_native.controller,16);
}
static int native_transform(void *context,uint32_t node,const uint32_t previous[6],
    int has_previous,uint32_t representation[2],uint32_t *written)
{
    (void)context;(void)node;(void)previous;(void)has_previous;(void)representation;(void)written;
    return native_missing(0x8001e78cu);
}
static int native_present_object(void *context,uint32_t node,const uint32_t representation[2],uint32_t written)
{
    (void)context;(void)node;(void)representation;(void)written;
    return native_missing(0x8001b7a0u);
}
static int native_cancel_object(void *context,uint32_t node,uint32_t service)
{
    (void)context;(void)node;
    if(service==GOOL_NATIVE_OBJECT_PROTECTED)return gool_native_controller_close(&s_native.controller);
    /* Unimplemented transform/present reject synchronously, borrowing nothing. */
    return 0;
}
static uint32_t native_camera_shift8(uint32_t value)
{return (value>>8)|((value&0x80000000u)?0xff000000u:0);}
static int native_camera_position(zdat_native *source,uint32_t database,uint32_t position,
                                  uint32_t output,uint32_t auxiliary,uint32_t *result)
{
    uint32_t zone,offsets,index=native_camera_shift8(position),camera,current;
    zdat_native_value point,angles,count;
    int error=native_read(database+4u,&zone);if(error)return error;
    error=native_read(zone+20u,&offsets);if(error)return error;
    error=zdat_native_select(source,database,0x4b,0,index,&point);if(error)return error;
    if(!point.address) {
        for(unsigned i=0;i<6;i++){error=native_write(output+i*4u,0);if(error)return error;}
        if(auxiliary)for(unsigned i=0;i<3;i++){error=native_write(auxiliary+i*4u,0);if(error)return error;}
        *result=0;return 0;
    }
    error=native_read(0x80065d64u,&current);if(error)return error;
    if(database==current){error=native_read(0x80065d68u,&camera);if(error)return error;}
    else {
        uint32_t selector,header,info;
        error=zdat_native_scalar(source,database,0x173u,&selector);if(error)return error;
        error=native_read(zone+16u,&header);if(error)return error;
        error=native_read(header+0x184u,&info);if(error)return error;
        error=native_read(zone+16u+(info+selector*3u+1u)*4u,&camera);if(error)return error;
    }
    error=zdat_native_select(source,camera,0x4b,0,index*2u,&angles);if(error)return error;
    if(!angles.address)return GOOL_DOMAIN_UNBOUND;
    uint32_t value=0;
    for(unsigned i=0;i<3;i++) {
        uint16_t half;uint32_t offset;
        error=gool_domain_read16(source->domain,point.address+i*2u,&half);if(error)return error;
        error=native_read(offsets+i*4u,&offset);if(error)return error;
        value=(offset+((half&0x8000u)?0xffff0000u|half:half))<<8;
        error=native_write(output+i*4u,value);if(error)return error;
    }
    for(unsigned i=0;i<3;i++) {
        uint16_t half;
        error=gool_domain_read16(source->domain,angles.address+i*2u,&half);if(error)return error;
        value=(half&0x8000u)?0xffff0000u|half:half;
        error=native_write(output+12u+i*4u,value);if(error)return error;
    }
    if(auxiliary)for(unsigned i=0;i<3;i++) {
        uint16_t half;
        error=gool_domain_read16(source->domain,angles.address+6u+i*2u,&half);if(error)return error;
        value=(half&0x8000u)?0xffff0000u|half:half;
        error=native_write(auxiliary+i*4u,value);if(error)return error;
    }
    uint32_t fraction=position&255u;
    if(!fraction){*result=value;return 0;}
    error=zdat_native_select(source,database,0x27f,0,0,&count);if(error)return error;
    if(((index+1u)^0x80000000u)>=(count.length^0x80000000u)){*result=count.length;return 0;}
    for(unsigned i=0;i<3;i++) {
        uint16_t half;uint32_t offset,previous;
        error=gool_domain_read16(source->domain,point.address+6u+i*2u,&half);if(error)return error;
        error=native_read(offsets+i*4u,&offset);if(error)return error;
        value=(offset+((half&0x8000u)?0xffff0000u|half:half))<<8;
        error=native_read(output+i*4u,&previous);if(error)return error;
        value=previous+native_camera_shift8((value-previous)*fraction);
        error=native_write(output+i*4u,value);if(error)return error;
    }
    for(unsigned i=0;i<3;i++) {
        uint16_t half;uint32_t previous,delta;
        error=gool_domain_read16(source->domain,angles.address+12u+i*2u,&half);if(error)return error;
        error=native_read(output+12u+i*4u,&previous);if(error)return error;
        delta=((uint32_t)half-previous)&0xfffu;
        if(delta&0x800u)delta|=0xfffff000u;
        value=(previous+native_camera_shift8(delta*fraction))&0xfffu;
        error=native_write(output+12u+i*4u,value);if(error)return error;
    }
    *result=value;return 0;
}
static int native_page_identity(uint32_t reference,uint32_t *eid,uint32_t *cid)
{
    nsd_resource_index nsd;int error=level_resources_nsd(&nsd);if(error)return error;
    if(!(reference&1u)) {
        if(reference>=0xb4000000u&&reference<0xb4000000u+sizeof(s_native.page_storage)&&
           !((reference-0xb4000000u)%NSF_PAGE_BYTES)) {
            unsigned slot=(reference-0xb4000000u)/NSF_PAGE_BYTES;
            if(!s_native.pages[slot].valid)return NSF_RESOURCE_MISSING;
            *cid=s_native.pages[slot].cid;
            const uint8_t *p=s_native.page_storage[slot];
            *eid=(uint32_t)p[4]|(uint32_t)p[5]<<8|(uint32_t)p[6]<<16|(uint32_t)p[7]<<24;
            return 0;
        }
        unsigned i;
        for(i=0;i<16;i++)if(s_native.resources[i].bank&&s_native.resources[i].address==reference) {
            zdat_resources *bank=s_native.resources[i].bank;
            reference=bank->slots[s_native.resources[i].lease.slot].eid;break;
        }
        if(i==16) {
            uint32_t offset=reference-0x88000520u;
            if(reference<0x88000520u||(offset&7u)||offset/8u>=nsd.count)return NSF_RESOURCE_MISSING;
            const uint8_t *p=nsd.data+0x524u+offset;
            reference=(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
        }
    }
    error=nsd_resource_find(&nsd,reference,cid);if(!error)*eid=reference;return error;
}
static int native_page_find(uint32_t cid)
{
    for(unsigned i=0;i<NATIVE_MAIN_PAGES;i++)
        if(s_native.pages[i].valid&&s_native.pages[i].cid==cid)return (int)i;
    return -1;
}
static int native_prefetch_release_io(void)
{
    int error;
    if(s_native.prefetch.view.leased) {
        error=nsf_resource_release_view(&s_native.prefetch.view);if(error)return error;
    }
    if(s_native.prefetch.owned) {
        error=nsf_resource_release_ticket(s_native.prefetch.ticket);if(error)return error;
        s_native.prefetch.owned=0;
    }
    return 0;
}
static int native_prefetch_step(uint32_t budget)
{
    while(budget--) {
        int error;
        if(!s_native.prefetch.busy)return GOOL_NATIVE_ZONE_TRANSITION_INVALID;
        if(s_native.prefetch.pass==2u)return 0;
        if(!s_native.prefetch.selected) {
            int reverse=s_native.prefetch.direction==2u;
            uint32_t key=reverse?0x209u:0x208u;
            uint32_t position=s_native.prefetch.position+(reverse?0xffffff00u:0x100u);
            if(s_native.prefetch.pass){key=reverse?0x208u:0x209u;position=s_native.prefetch.position;}
            zdat_native_value value;
            error=zdat_native_select_exact(level_native_database(),s_native.prefetch.database,
                (int32_t)key,position,&value);if(error)return error;
            s_native.prefetch.list=value.address;
            s_native.prefetch.count=value.address?value.length:0;
            s_native.prefetch.index=0;s_native.prefetch.selected=1;
        }
        if(s_native.prefetch.index>=s_native.prefetch.count) {
            s_native.prefetch.pass++;s_native.prefetch.selected=0;continue;
        }
        uint32_t reference,eid,cid;
        error=native_read(s_native.prefetch.list+s_native.prefetch.index*4u,&reference);if(error)return error;
        error=native_page_identity(reference,&eid,&cid);if(error)return error;
        int page=native_page_find(cid);
        if(!s_native.prefetch.pass) {
            /* 22DA0(flags14,a3=1) ->1439C(-1):13ECC drops the
             * conditional pin, then13E64 drops a positive signed total. */
            if(page>=0&&s_native.pages[page].soft) {
                s_native.pages[page].soft--;
                if(s_native.pages[page].total&&s_native.pages[page].total<128u)s_native.pages[page].total--;
            }
            s_native.prefetch.index++;continue;
        }
        if(page<0) {
            if(!s_native.prefetch.owned) {
                unsigned slot;
                for(slot=0;slot<NATIVE_MAIN_PAGES;slot++)
                    if(!s_native.pages[slot].valid||(!s_native.pages[slot].total&&!s_native.pages[slot].soft))break;
                if(slot==NATIVE_MAIN_PAGES)return ZDAT_RESOURCE_FULL;
                error=nsf_resource_request(eid,&s_native.prefetch.ticket);
                if(error==NSF_STREAM_FULL)return 1;
                if(error)return error;
                s_native.prefetch.slot=slot;s_native.prefetch.eid=eid;s_native.prefetch.cid=cid;
                s_native.prefetch.owned=1;
            }
            if(s_native.prefetch.eid!=eid||s_native.prefetch.cid!=cid)return GOOL_DOMAIN_INVALID;
            nsf_stream_state state;int failure;
            error=nsf_resource_poll(s_native.prefetch.ticket,&state,&failure);if(error)return error;
            if(state==NSF_STREAM_QUEUED||state==NSF_STREAM_READING)return 1;
            if(state!=NSF_STREAM_READY)return failure?failure:NSF_RESOURCE_FORMAT;
            error=nsf_resource_acquire(s_native.prefetch.ticket,&s_native.prefetch.view);if(error)return error;
            page=(int)s_native.prefetch.slot;
            memcpy(s_native.page_storage[page],s_native.prefetch.view.page,NSF_PAGE_BYTES);
            error=native_prefetch_release_io();if(error)return error;
            s_native.pages[page].cid=cid;s_native.pages[page].valid=1;
            s_native.pages[page].soft=s_native.pages[page].total=0;
        }
        /* 22DA0(flags0C) ->142B4(flags0E,a2=1):13FA8 increments
         * both original byte counters. The copied page remains retained. */
        s_native.pages[page].soft++;s_native.pages[page].total++;
        s_native.prefetch.index++;
    }
    return 1;
}
static int native_resource_retain(zdat_resources *bank,uint32_t reference,uint32_t *result)
{
    unsigned index;
    for(index=0;index<16;index++)if(!s_native.resources[index].bank)break;
    if(index==16)return ZDAT_RESOURCE_FULL;
    zdat_resource_lease lease;uint32_t address=reference;
    int error=(reference&1u)?zdat_resources_acquire(bank,reference,&address,&lease):
        zdat_resources_acquire_address(bank,reference,&lease);
    if(error)return error;
    s_native.resources[index].bank=bank;s_native.resources[index].lease=lease;
    s_native.resources[index].address=address;s_native.resources[index].reference=s_native.resource_reference;
    error=native_write(s_native.resource_reference,address);
    if(error)return error;
    *result=address;return 0;
}
/* Texture chunks have no entry/item directory. Retain their original page
 * bytes separately from relocated ZDAT/SVTX/TGEO entries. GPU upload belongs
 * to the model texture owner; a CPU residency result is not a draw result. */
static int native_resource_texture(const nsf_resource_view *view,uint32_t *result)
{
    uint32_t eid,cid;int error=native_page_identity(view->eid,&eid,&cid);if(error)return error;
    int page=native_page_find(cid);
    if(page<0) {
        for(unsigned i=0;i<NATIVE_MAIN_PAGES;i++)
            if(!s_native.pages[i].valid||(!s_native.pages[i].total&&!s_native.pages[i].soft)) {page=(int)i;break;}
        if(page<0)return ZDAT_RESOURCE_FULL;
        memcpy(s_native.page_storage[page],view->page,NSF_PAGE_BYTES);
        s_native.pages[page].cid=cid;s_native.pages[page].valid=1;
        s_native.pages[page].total=s_native.pages[page].soft=0;
    }
    if(s_native.pages[page].total==255u)return ZDAT_RESOURCE_FULL;
    s_native.pages[page].total++;
    *result=0xb4000000u+(uint32_t)page*NSF_PAGE_BYTES;
    return native_write(s_native.resource_reference,*result);
}
static int native_resource_cached(uint32_t eid,uint32_t *result)
{
    uint32_t cid,actual;int error=native_page_identity(eid,&actual,&cid);if(error)return error;
    int page=native_page_find(cid);if(page<0)return NSF_RESOURCE_MISSING;
    nsf_resource_view view={0};
    error=nsf_resource_parse_page(s_native.page_storage[page],NSF_PAGE_BYTES,cid,actual,&view);
    if(error)return error;
    if(view.page_type==1u)return native_resource_texture(&view,result);
    zdat_resources *bank;
    if(view.entry_type==7)bank=level_native_zones();
    else if(view.entry_type==1||view.entry_type==2)bank=level_native_models();
    else if(view.entry_type==4)bank=&s_native.miscellaneous;
    else return native_missing(0x800142b4u);
    error=zdat_resources_import(bank,&view);
    if(error==ZDAT_RESOURCE_FULL) {
        for(unsigned i=0;i<ZDAT_RESOURCES_SLOTS;i++)
            if(bank->slots[i].state==ZDAT_RESOURCE_READY&&!bank->slots[i].references) {
                int eviction=zdat_resources_evict(bank,bank->slots[i].eid);
                if(eviction==ZDAT_RESOURCE_BUSY)continue;
                if(eviction)return eviction;
                error=zdat_resources_import(bank,&view);break;
            }
    }
    if(error)return error;
    return native_resource_retain(bank,actual,result);
}
static int native_resource_step(void)
{
    if(!s_native.resource_owned)return GOOL_NATIVE_ZONE_TRANSITION_INVALID;
    int error;
    if(s_native.resource_phase==0) {
        nsf_stream_state state;int failure;
        error=nsf_resource_poll(s_native.resource_ticket,&state,&failure);if(error)return error;
        if(state==NSF_STREAM_QUEUED||state==NSF_STREAM_READING)return 1;
        if(state!=NSF_STREAM_READY)return failure?failure:NSF_RESOURCE_FORMAT;
        error=nsf_resource_acquire(s_native.resource_ticket,&s_native.resource_view);if(error)return error;
        if(s_native.resource_view.page_type==1u)
            return native_resource_texture(&s_native.resource_view,s_native.resource_result);
        unsigned type=s_native.resource_view.entry_type;
        if(type==7)s_native.resource_bank=level_native_zones();
        else if(type==1||type==2)s_native.resource_bank=level_native_models();
        else if(type==4)s_native.resource_bank=&s_native.miscellaneous;
        else return native_missing(0x800142b4u);
        error=zdat_resources_request(s_native.resource_bank,s_native.resource_eid);if(error)return error;
        s_native.resource_phase=1;
    }
    error=zdat_resources_poll(s_native.resource_bank);if(error<0)return error;
    error=native_resource_retain(s_native.resource_bank,s_native.resource_eid,s_native.resource_result);
    return error==ZDAT_RESOURCE_BUSY?1:error;
}
static int native_zone_cancel(void *context);
static int native_creation_enter(void *context,uint32_t node,uint32_t state,uint32_t argc,
    uint32_t arguments,uint32_t *result)
{
    (void)context;
    gool_native_dispatch *pending=gool_native_state_pending(&s_native.creation_state);
    int error;
    if(pending) {
        if(!s_native.creation_router.active) {
            error=gool_native_router_begin(&s_native.creation_router,pending);
            if(error!=1)return error?error:GOOL_NATIVE_DISPATCH_INVALID;
        }
        error=gool_native_router_step(&s_native.creation_router,32);
        if(error)return error;
        error=gool_native_router_close(&s_native.creation_router);if(error)return error;
        error=gool_native_router_bind(&s_native.creation_router,&s_native.factory,s_native.creation_routes,
            NATIVE_MAIN_ROUTES,0x94040000u);if(error)return error;
    }
    return gool_native_state_enter(&s_native.creation_state,node,state,argc,arguments,result);
}
static int native_creation_retire(void *context,uint32_t node,uint32_t notify)
{
    (void)context;(void)node;(void)notify;
    /* Creation's synchronous reclamation boundary cannot discard a pending
     * signal/audio job. Zone retirement uses the resumable owner below. */
    return native_missing(0x8001aa78u);
}
static int native_player_event(void *context,uint32_t node,uint32_t data,uint32_t count,uint32_t argument)
{(void)context;(void)node;(void)data;(void)count;(void)argument;return native_missing(0x8002ff3cu);}
static int native_activation_find(uint32_t key,uint32_t *node)
{
    int error=gool_search_serial(0x80068eb8u,key,GOOL_NATIVE_POOL_COUNT+8,
        gool_native_registry_read,s_native.registry,node);
    if(error||*node)return error;
    return gool_search_serial(0x80068ec0u,key,GOOL_NATIVE_POOL_COUNT+8,
        gool_native_registry_read,s_native.registry,node);
}
static int native_activation_step(void)
{
    int error;uint32_t value,node;
    for(unsigned work=0;work<32;work++) {
        uint32_t row=s_native.activation_row;
        switch(s_native.activation_phase) {
        case 0:
            if((int32_t)s_native.activation_index<0) {s_native.activation_busy=0;return 0;}
            if(s_native.activation_index>=256)return GOOL_NATIVE_ACTIVATION_CAPACITY;
            row=0x80067a38u+s_native.activation_index*12u;s_native.activation_row=row;
            error=native_read(row+8,&value);if(error)return error;
            if(s_native.activation_marker!=UINT32_MAX&&value!=s_native.activation_marker)
                {s_native.activation_phase=9;break;}
            if(value!=0x13bu&&value!=0x13cu){s_native.activation_phase=9;break;}
            error=native_read(row,&s_native.activation_word);if(error)return error;
            s_native.activation_key=s_native.activation_word&0x00ffff00u;
            value=s_native.activation_key>>8;
            if(value>=sizeof(s_native.usage)/4)return GOOL_CREATE_RANGE;
            s_native.activation_phase=1;break;
        case 1: {
            uint32_t marker;error=native_read(row+8,&marker);if(error)return error;
            uint32_t used=s_native.usage[s_native.activation_key>>8]&1u;
            if(marker==0x13cu) {
                if(!used&&(s_native.activation_word&255u)<16u) {
                    uint32_t header,reference;
                    error=native_read(s_native.activation_zone+16,&header);if(error)return error;
                    reference=header+0x194u+(s_native.activation_word&255u)*4u;
                    uint32_t eid;error=native_read(reference,&eid);if(error)return error;
                    s_native.resource_reference=reference;s_native.resource_result=&s_native.activation_result;
                    if(eid&1u) {
                        error=nsf_resource_request(eid,&s_native.resource_ticket);if(error)return error;
                        s_native.resource_owned=1;s_native.resource_phase=0;s_native.resource_eid=eid;
                        s_native.activation_phase=2;break;
                    }
                    error=native_resource_retain(level_native_zones(),eid,&s_native.activation_result);
                    if(error)return error;
                    s_native.activation_phase=3;break;
                }
                error=native_read(row+4,&node);if(error)return error;
                if(!node) {
                    error=native_activation_find(s_native.activation_key,&node);if(error)return error;
                    error=native_write(row+4,node);if(error)return error;
                }
                error=native_write(row+8,0);if(error)return error;
                s_native.activation_phase=9;break;
            }
            if(!used){s_native.activation_phase=8;break;}
            error=native_read(row+4,&node);if(error)return error;
            if(!node) {
                error=native_activation_find(s_native.activation_key,&node);if(error)return error;
                error=native_write(row+4,node);if(error)return error;
            }
            if(!node){s_native.activation_phase=8;break;}
            error=native_read(node+0xacu,&value);if(error)return error;
            if(value&0x01000000u){s_native.activation_phase=8;break;}
            error=native_read(node+0x100u,&value);if(error)return error;
            s_native.activation_phase=(value&0x10000u)?8u:6u;break;
        }
        case 2:
            error=native_resource_step();if(error)return error;
            /* The acquired zone reference survives request cancellation. */
            error=native_zone_cancel(NULL);if(error)return error;
            s_native.activation_phase=3;break;
        case 3:
            error=gool_create_begin(&s_native.create,s_native.activation_result,s_native.activation_word>>24,
                &s_native.creation.services);
            if(error!=1)return error?error:GOOL_CREATE_INVALID;
            s_native.activation_phase=4;break;
        case 4:
            error=gool_create_step(&s_native.create,16);if(error)return error;
            error=native_write(row+4,(uint32_t)s_native.create.native_result);if(error)return error;
            if(s_native.create.native_result>=-254){s_native.activation_phase=8;break;}
            error=native_write(row+8,0);if(error)return error;
            s_native.activation_phase=9;break;
        case 6: {
            int32_t result;error=native_read(row+4,&node);if(error)return error;
            error=s_native.retirement.services.retire(&s_native.retirement,node,1,&result);
            if(error)return error;
            s_native.activation_phase=8;break;
        }
        case 8:
            error=native_read(0x80060a94u,&value);if(error)return error;
            if(value>256||value<=s_native.activation_index)return GOOL_NATIVE_ACTIVATION_CAPACITY;
            for(uint32_t i=s_native.activation_index+1u;i<value;i++)for(unsigned word=0;word<3;word++) {
                uint32_t contents;error=native_read(0x80067a38u+i*12u+word*4u,&contents);if(error)return error;
                error=native_write(0x80067a38u+(i-1u)*12u+word*4u,contents);if(error)return error;
            }
            error=native_write(0x80060a94u,value-1u);if(error)return error;
            s_native.activation_phase=9;break;
        case 9:s_native.activation_index--;s_native.activation_phase=0;break;
        default:return GOOL_NATIVE_ACTIVATION_INVALID;
        }
    }
    return 1;
}
static int native_zone_call(void *context,const gool_native_zone_transition_call *call,uint32_t *result)
{
    (void)context;
    zdat_native *source=level_native_database();
    zdat_native_value value;
    const uint32_t *a=call->arguments;
    int error;
    switch(call->address) {
    case 0x8002aa14u: {
        uint16_t flags,count;
        error=gool_domain_read16(s_native.domain,a[0]+2u,&flags);if(error)return error;
        if(flags&1u)return native_missing(a[3]?0x80039e3cu:0x8003a234u);
        error=gool_domain_read16(s_native.domain,a[0],&count);if(error)return error;
        if((uint32_t)count*2u>65532u||
           (a[2]!=0xbf100000u&&a[2]!=0xbf120000u))return GOOL_DOMAIN_INVALID;
        for(uint32_t i=0;i<count;i++) {
            uint16_t word;error=gool_domain_read16(s_native.domain,a[0]+4u+i*2u,&word);if(error)return error;
            error=gool_domain_write16(s_native.domain,a[2]+4u+i*2u,word);if(error)return error;
        }
        error=gool_domain_write16(s_native.domain,a[2],count);if(error)return error;
        *result=a[2];return 0;
    }
    case 0x80022c9cu:
        if(s_native.prefetch.busy)return GOOL_DOMAIN_BUSY;
        s_native.prefetch.busy=1;s_native.prefetch.database=a[0];
        s_native.prefetch.position=a[1];s_native.prefetch.direction=a[2];
        return native_prefetch_step(64);
    case 0x80021e60u: {
        uint32_t word;
        error=native_read(a[0]+0x1cu,&word);if(error)return error;
        error=native_write(0x80068f70u,word);if(error)return error;
        *result=word;
        if(!(a[1]&4u)) {
            /* Original3505C only schedules the zone audio state change. */
            error=native_read(call->gp+0x2a0u,&word);if(error)return error;
            if(!word){error=native_write(call->gp+0x2a0u,1);if(error)return error;}
            *result=1;
        }
        return 0;
    }
    case 0x80012eacu: {
        /* CD/cache drain uses the actual port workers. Completed cached pages
         * hold no worker tickets; outstanding typed copies must reach READY. */
        zdat_resources *banks[3]={level_native_zones(),level_native_models(),&s_native.miscellaneous};
        int pending=0;
        for(unsigned i=0;i<3;i++) {
            error=zdat_resources_poll(banks[i]);if(error<0)return error;
            pending+=error;
        }
        s_native.resource_barrier=pending!=0;*result=UINT32_MAX;
        return pending?1:0;
    }
    case 0x80024330u: {
        uint8_t mode;
        error=native_write(0x80065d88u,0);if(error)return error;
        error=gool_domain_read8(s_native.domain,0x8005c63fu,&mode);if(error)return error;
        if(mode) {
            *result=0x80060000u;return native_write(0x8005c66cu,0);
        }
        if(a[0]==0x10u){*result=0x10u;return 0;}
        /* Original2513C seeds the transition from current camera angles.
         * The file-backed word at8003AE8C is zero in SCUS_942.44. */
        error=native_write(0x8005c66cu,1);if(error)return error;
        error=native_write(0x8005c674u,0x154u);if(error)return error;
        error=native_write(0x8005c670u,0);if(error)return error;
        for(unsigned i=0;i<3;i++) {
            uint32_t angle;
            error=native_read(0x80065da8u+i*4u,&angle);if(error)return error;
            error=native_write(0x8005c678u+i*4u,angle);if(error)return error;
        }
        *result=0x80065c90u;return 0;
    }
    case 0x8002437cu: {
        uint32_t database,zone,header,info,camera,first,second;
        error=native_read(0x80065d64u,&database);if(error)return error;
        error=native_write(0x80065d8cu,a[0]);if(error)return error;
        if(!database){*result=0;return 0;}
        error=native_read(0x80065d5cu,&zone);if(error)return error;
        if(!zone){*result=2;return 0;}
        error=zdat_native_scalar(source,database,0x173u,&camera);if(error)return error;
        error=native_read(zone+16u,&header);if(error)return error;
        error=native_read(header+0x184u,&info);if(error)return error;
        uint32_t base=info+camera*3u;
        error=native_read(zone+16u+(base+1u)*4u,&first);if(error)return error;
        error=native_write(0x80065d68u,first);if(error)return error;
        if(a[0]==1u)second=first;
        else {error=native_read(zone+16u+(base+a[0])*4u,&second);if(error)return error;}
        error=native_write(0x80065d6cu,second);if(error)return error;
        *result=a[0]==1u?1u:second;return 0;
    }
    case 0x80014468u: {
        /* Port page owners replace the PS1 cache rows. Only unborrowed
         * residents can be reclaimed; live zone/model/SLST readers stay pinned. */
        zdat_resources *banks[3]={level_native_zones(),level_native_models(),&s_native.miscellaneous};
        for(unsigned b=0;b<3;b++)for(unsigned i=0;i<ZDAT_RESOURCES_SLOTS;i++)
            if(banks[b]->slots[i].state==ZDAT_RESOURCE_READY&&!banks[b]->slots[i].references) {
                error=zdat_resources_evict(banks[b],banks[b]->slots[i].eid);if(error)return error;
            }
        return 0;
    }
    case 0x8001ae64u:
        if(s_native.activation_busy)return GOOL_DOMAIN_BUSY;
        s_native.activation_busy=1;s_native.activation_zone=a[0];s_native.activation_marker=a[1];
        s_native.activation_phase=0;
        error=native_write(0x80069210u,0);if(error)return error;
        error=native_write(0x80069218u,0);if(error)return error;
        error=native_write(0x80069214u,0x19000u);if(error)return error;
        error=native_read(0x80060a94u,&s_native.activation_index);if(error)return error;
        s_native.activation_index--;return 1;
    case 0x80032710u:
        if(call->stack_count<2)return GOOL_NATIVE_ZONE_TRANSITION_INVALID;
        if(call->stack_arguments[1]==1u)
            error=zdat_native_select_exact(source,a[0],(int32_t)a[1],a[2],&value);
        else if(call->stack_arguments[1]==2u)
            error=zdat_native_select(source,a[0],(int32_t)a[1],a[2],a[3],&value);
        else return native_missing(call->address);
        if(!error)*result=value.address;
        return error;
    case 0x80032edcu:
        error=zdat_native_select(source,a[0],(int32_t)a[1],a[2],0,&value);
        if(!error)*result=value.length;
        return error;
    case 0x80028740u:
        return native_camera_position(source,a[0],a[1],a[2],a[3],result);
    case 0x80032df8u:
        if(call->stack_count>=3u&&call->stack_arguments[1]==3u&&!(a[2]&255u)) {
            /* At an exact integral timestamp,32710 mode3 returns the
             * original sample without interpolation. Other time intervals
             * still require the native timed interpolator. */
            error=zdat_native_select_exact(source,a[0],(int32_t)a[1],a[2],&value);
            if(error)return error;
            if(!value.address) {
                if(value.descriptor)return native_missing(call->address);
                *result=call->stack_arguments[2];return 0;
            }
            uint32_t format=value.format&31u;
            if(format==1u||format==17u) {
                uint8_t byte;error=gool_domain_read8(s_native.domain,value.address,&byte);
                if(!error)*result=format==17u&&(byte&128u)?0xffffff00u|byte:byte;
            } else if(format==2u||format==18u) {
                uint16_t half;error=gool_domain_read16(s_native.domain,value.address,&half);
                if(!error)*result=format==18u&&(half&32768u)?0xffff0000u|half:half;
            } else error=native_read(value.address,result);
            return error;
        }
        /* Mode2 scalar helper currently supports direct sample0. */
        if(a[2]||call->stack_count<2u||call->stack_arguments[1]!=2u)
            return native_missing(call->address);
        return zdat_native_scalar(source,a[0],a[1],result);
    case 0x80025c0cu: {
        uint32_t database,position;
        error=native_read(0x80065d64u,&database);if(error)return error;
        error=native_read(0x80065d70u,&position);if(error)return error;
        gool_native_zone_transition_call scalar={.address=0x80032df8u,.gp=call->gp,
            .arguments={database,0x130u,position,0},.argument_count=4,
            .stack_arguments={0,3,0x120u},.stack_count=3};
        return native_zone_call(context,&scalar,result);
    }
    case 0x8004f724u: {
        /* SetGeomScreen writes unsigned GTE H. Preserve its focal length
         * in pixels in the active top-screen PICA projection. */
        uint32_t h=a[0]&0xffffu;
        if(!h)return native_missing(call->address);
        s_native.projection_h=h;
        Mtx_PerspStereoTilt(&s_projection,2.0f*atanf((float)SCREEN_H/(2.0f*(float)h)),
            C3D_AspectRatioTop,8.0f,60000.0f,0.0f,2.0f,false);
        *result=0;return 0;
    }
    case 0x800142b4u:case 0x8001440cu: {
        uint32_t reference;error=native_read(a[0],&reference);if(error)return error;
        if(call->address==0x8001440cu) {
            *result=0;
            for(unsigned i=0;i<16;i++)if(s_native.resources[i].bank&&
                (s_native.resources[i].reference==a[0]||s_native.resources[i].address==reference)) {
                *result=s_native.resources[i].address;
                if(a[1]) {
                    error=zdat_resources_release(s_native.resources[i].bank,s_native.resources[i].lease);
                    if(error)return error;
                    memset(&s_native.resources[i],0,sizeof(s_native.resources[i]));
                }
                return 0;
            }
            /* A resident owner can answer the original non-loading probe. */
            zdat_resources *banks[3]={level_native_zones(),level_native_models(),&s_native.miscellaneous};
            for(unsigned b=0;b<3;b++)for(unsigned i=0;i<ZDAT_RESOURCES_SLOTS;i++)
                if(banks[b]->slots[i].state==ZDAT_RESOURCE_READY&&
                   (banks[b]->slots[i].eid==reference||banks[b]->slots[i].address==reference))
                    {*result=banks[b]->slots[i].address;return 0;}
            return 0;
        }
        s_native.resource_reference=a[0];
        if(!(reference&1u)) {
            zdat_resources *banks[3]={level_native_zones(),level_native_models(),&s_native.miscellaneous};
            for(unsigned i=0;i<3;i++) {
                error=native_resource_retain(banks[i],reference,result);
                if(error!=NSF_RESOURCE_MISSING)return error;
            }
            uint32_t cid;
            error=native_page_identity(reference,&reference,&cid);if(error)return error;
        }
        if(s_native.resource_owned)return GOOL_DOMAIN_BUSY;
        error=native_resource_cached(reference,result);
        if(error!=NSF_RESOURCE_MISSING)return error;
        error=nsf_resource_request(reference,&s_native.resource_ticket);if(error)return error;
        s_native.resource_owned=1;s_native.resource_phase=0;s_native.resource_eid=reference;
        s_native.resource_result=result;return 1;
    }
    default:return native_missing(call->address);
    }
}
static int native_zone_step(void *context,uint32_t budget)
{
    (void)context;
    if(s_native.prefetch.busy)return native_prefetch_step(budget);
    if(s_native.resource_barrier) {
        int pending=0;
        zdat_resources *banks[3]={level_native_zones(),level_native_models(),&s_native.miscellaneous};
        for(unsigned i=0;i<3;i++) {
            int error=zdat_resources_poll(banks[i]);if(error<0)return error;pending+=error;
        }
        s_native.resource_barrier=pending!=0;return pending?1:0;
    }
    return s_native.activation_busy?native_activation_step():native_resource_step();
}
static int native_zone_cancel(void *context)
{
    (void)context;int error;
    error=native_prefetch_release_io();if(error)return error;
    memset(&s_native.prefetch,0,sizeof(s_native.prefetch));
    s_native.resource_barrier=0;
    if(s_native.resource_view.leased) {
        error=nsf_resource_release_view(&s_native.resource_view);if(error)return error;
    }
    if(s_native.resource_owned) {
        error=nsf_resource_release_ticket(s_native.resource_ticket);if(error)return error;
        s_native.resource_owned=0;
    }
    s_native.resource_result=NULL;s_native.resource_bank=NULL;return 0;
}
/*35538 read-address semantics: direct members and STACK pop are distinct
 * from35410's linked operand addresses. The stopped actor owns raw SP here. */
static int native_resource_operand(uint32_t node,uint32_t operand,uint32_t *address)
{
    if((operand&0xe00u)==0xe00u) {
        if(operand!=0xe1fu){*address=node+0x40u+(operand&0x1ffu)*4u;return 0;}
        uint32_t sp;int error=native_read(node+0xbcu,&sp);if(error)return error;
        if(sp<4u)return GOOL_VM_STACK;
        *address=sp-4u;return native_write(node+0xbcu,*address);
    }
    return native_operand(NULL,node,operand,address);
}
static int native_script_resource(void *context,uint32_t node,uint32_t instruction,int cancel)
{
    (void)context;
    if(cancel) {
        if(!s_native.script_resource.active)return 0;
        if(s_native.script_resource.node!=node)return GOOL_DOMAIN_BUSY;
        int error=native_zone_cancel(NULL);if(error)return error;
        memset(&s_native.script_resource,0,sizeof(s_native.script_resource));return 0;
    }
    int error;
    if(!s_native.script_resource.active) {
        uint32_t address,command;
        error=native_resource_operand(node,(instruction>>12)&0xfffu,&address);if(error)return error;
        error=native_read(address,&command);if(error)return error;
        error=native_resource_operand(node,instruction&0xfffu,&address);if(error)return error;
        s_native.script_resource.node=node;s_native.script_resource.instruction=instruction;
        s_native.script_resource.command=command;s_native.script_resource.address=address;
        s_native.script_resource.active=1;
        if(command==1u||command==6u||command==2u||command==3u) {
            gool_native_zone_transition_call call={
                .address=command==1u||command==6u?0x800142b4u:0x8001440cu,
                .arguments={address,command==6u||command==2u,1},.argument_count=3};
            error=native_zone_call(NULL,&call,&s_native.script_resource.result);
            if(error<0)return error;
            s_native.script_resource.pending=error!=0;
            if(error)return 1;
        } else if(command>=1u&&command<=6u)return native_missing(command==5u?0x800148f8u:0x800145a0u);
    } else {
        if(s_native.script_resource.node!=node||s_native.script_resource.instruction!=instruction)
            return GOOL_DOMAIN_BUSY;
        if(s_native.script_resource.pending) {
            error=native_resource_step();if(error)return error;
            error=native_zone_cancel(NULL);if(error)return error;
            s_native.script_resource.pending=0;
        }
    }
    uint32_t command=s_native.script_resource.command;
    if(command==1u||command==2u||command==6u)
        error=native_write(node+0xd4u,s_native.script_resource.result);
    else if(command==3u) {
        uint32_t sp;error=native_read(node+0xbcu,&sp);if(error)return error;
        error=native_write(sp,s_native.script_resource.result);if(error)return error;
        error=native_write(node+0xbcu,sp+4u);
    } else error=0;
    if(error)return error;
    memset(&s_native.script_resource,0,sizeof(s_native.script_resource));return 0;
}
static int native_main_open(void)
{
    if(s_native.opened)return GOOL_DOMAIN_BUSY;
    s_native.domain=level_native_domain();s_native.registry=level_native_registry();
    if(!s_native.domain||!s_native.registry)return GOOL_DOMAIN_INVALID;
    s_native.opened=1;s_native.status=1;s_native.phase=0;
    s_native.clock_origin=osGetTime();s_native.last_frame=s_native.clock_origin;
    native_initial_data();
    int error;
#define NATIVE_TRY(expression) do{error=(expression);if(error){loading_result(#expression,error);return s_native.status=error;}}while(0)
    NATIVE_TRY(native_map(0x80060878u,s_native.gp_before,sizeof(s_native.gp_before)));
    NATIVE_TRY(native_map(0x80060a94u,s_native.gp_after,sizeof(s_native.gp_after)));
    NATIVE_TRY(native_map(0x80068ef8u,s_native.globals,sizeof(s_native.globals)));
    NATIVE_TRY(native_map(0x80065bc8u,s_native.zone_globals,sizeof(s_native.zone_globals)));
    NATIVE_TRY(native_map(0x80067a38u,s_native.activation,sizeof(s_native.activation)));
    NATIVE_TRY(native_map(0x8006b688u,s_native.usage,sizeof(s_native.usage)));
    NATIVE_TRY(native_map(0x80061a70u,s_native.timer_pointers,sizeof(s_native.timer_pointers)));
    NATIVE_TRY(native_map(0xbf000000u,s_native.timers,sizeof(s_native.timers)));
    NATIVE_TRY(native_map(0x1f800040u,s_native.scratch,sizeof(s_native.scratch)));
    NATIVE_TRY(native_map(0x8006c7d8u,s_native.voices,sizeof(s_native.voices)));
    NATIVE_TRY(native_map(0x8006c2d8u,s_native.save_state,sizeof(s_native.save_state)));
    NATIVE_TRY(native_map(0x8006aee8u,s_native.replay_usage,sizeof(s_native.replay_usage)));
    NATIVE_TRY(native_map(0xb4000000u,s_native.page_storage,sizeof(s_native.page_storage)));
    /* Original initialized bytes at8005C634; other camera words start zero. */
    s_native.camera_state[0]=0x10u;s_native.camera_state[8]=0x1001u;
    NATIVE_TRY(native_map(0x8005c634u,s_native.camera_state,sizeof(s_native.camera_state)));
    NATIVE_TRY(native_map(0xbf100000u,s_native.visible_lists[0],sizeof(s_native.visible_lists[0])));
    NATIVE_TRY(native_map(0xbf120000u,s_native.visible_lists[1],sizeof(s_native.visible_lists[1])));
    NATIVE_TRY(gool_native_engine_map_dispatch(s_native.domain,&s_native.mappings[s_native.mapped]));
    s_native.mapped++;
    NATIVE_TRY(native_write(0x80061a7cu,0xbf000000u));
    NATIVE_TRY(native_write(0x80061a80u,0xbf000020u));
    NATIVE_TRY(native_write(0x80060aa8u,0xbf100000u));
    NATIVE_TRY(native_write(0x80060ab8u,0xbf120000u));
    NATIVE_TRY(gool_native_engine_defaults(s_native.domain));
    NATIVE_TRY(gool_native_engine_random_init(s_native.domain));
    NATIVE_TRY(gool_native_engine_level_begin(s_native.domain));
    NATIVE_TRY(gool_native_engine_camera_reset(s_native.domain));
    /* Keep the selected level request live through loading, as1166C does;
     * checkpoint creation tests this word before issuing2FF3C. */
    NATIVE_TRY(native_write(0x80060ac0u,(uint32_t)s_loaded_id));
    NATIVE_TRY(native_write(0x80060abcu,(uint32_t)s_loaded_id));
    s_native.projection_h=0x120u;
    Mtx_PerspStereoTilt(&s_projection,2.0f*atanf((float)SCREEN_H/(2.0f*288.0f)),
        C3D_AspectRatioTop,8.0f,60000.0f,0.0f,2.0f,false);
    NATIVE_TRY(gool_native_engine_prepare(s_native.domain));
    NATIVE_TRY(zdat_resources_open_models(&s_native.miscellaneous,s_native.domain,
        s_native.miscellaneous_storage,0xb2000000u));
    /* SLST needs native item pointers, not TGEO-specific header constraints. */
    s_native.miscellaneous.allowed_types=1u<<4;
    s_native.address=(gool_native_address){s_native.registry,0x80060a20u};
    s_native.geometry=(gool_native_geometry_services){.address=native_operand,
        .hierarchy_scan_limit=GOOL_NATIVE_POOL_COUNT+8,.zone_neighbor_limit=16};
    s_native.audio=(AudioNativeEngine){.registry=s_native.registry,.gp=0x80060878u};
    s_native.sound=(audio_native_services){.address=native_operand,.play=native_sound_play,.control=native_sound_control};
    gool_native_engine_data engine={.globals=(int32_t*)s_native.globals,
        .global_count=sizeof(s_native.globals)/4,.random_state=&s_native.gp_before[(0x80060a2cu-0x80060878u)/4],
        .phase_source=(int32_t*)&s_native.globals[(0x80069034u-0x80068ef8u)/4],
        .frame_factor=(int32_t*)&s_native.scratch[5],.tick=&s_native.tick,.send_enabled=1,
        .resource=native_script_resource,
        .geometry=&s_native.geometry,.sound=&s_native.sound};
    NATIVE_TRY(gool_native_factory_open(&s_native.factory,s_native.registry,level_native_pool(),
        s_native.slots,GOOL_NATIVE_POOL_COUNT+1,&engine,0x94000000u));
    gool_native_controller_services pad={.pad=native_pad};
    NATIVE_TRY(gool_native_controller_initialize(s_native.domain,1,1,&pad));
    NATIVE_TRY(gool_native_controller_bind(&s_native.controller,&s_native.factory,0x80060878u,1,
        &pad,s_native.pad_routes,NATIVE_MAIN_ROUTES,0x94020000u,128));
    gool_destroy_services destruction={.protected_object=(uint32_t*)level_native_pool()->protected_object,
        .detached_object=(uint32_t*)level_native_pool()->pointers,.usage=s_native.usage,
        .usage_count=sizeof(s_native.usage)/4,.free_root=0x80068ee0u,.scan_limit=GOOL_NATIVE_POOL_COUNT+8,
        .notify=native_notify,.stop_audio=native_stop_audio};
    gool_native_retirement_audio audio={.stop_audio=native_stop_audio,.cancel_audio=native_cancel_audio};
    NATIVE_TRY(gool_native_zone_retirement_bind(&s_native.retirement,&s_native.factory,s_native.retire_routes,
        NATIVE_MAIN_ROUTES,0x94010000u,128,GOOL_NATIVE_POOL_COUNT+8,&destruction,&audio));
    gool_native_state_engine state_engine={.context=&s_native.factory,.actor=gool_native_factory_actor};
    NATIVE_TRY(gool_native_state_bind(&s_native.creation_state,s_native.registry,level_native_pool(),&state_engine,128));
    NATIVE_TRY(gool_native_router_bind(&s_native.creation_router,&s_native.factory,s_native.creation_routes,
        NATIVE_MAIN_ROUTES,0x94040000u));
    gool_creation_engine creation={.enter_state=native_creation_enter,.retire=native_creation_retire,
        .player_event=native_player_event};
    NATIVE_TRY(gool_creation_services_bind(&s_native.creation,s_native.registry,level_native_database(),&creation,
        0x8006b688u,sizeof(s_native.usage)/4,GOOL_NATIVE_POOL_COUNT+8,GOOL_NATIVE_POOL_COUNT));
    s_native.factory.engine.creation=&s_native.creation.services;
    gool_native_object_services objects={.protected_update=native_protected_update,
        .transform=native_transform,.present=native_present_object,.cancel=native_cancel_object};
    NATIVE_TRY(native_runtime_bind(&s_native.world,&s_native.factory,s_native.world_routes,
        NATIVE_MAIN_ROUTES,0x94030000u,&objects,&destruction));
    s_native.activation_query=(gool_native_activation_query){.source=*level_native_database(),.bank=level_native_zones()};
    uint32_t metadata;NATIVE_TRY(native_read(0x80061044u,&metadata));
    uint32_t checkpoints;NATIVE_TRY(native_read(metadata,&checkpoints));
    if(!checkpoints)return s_native.status=GOOL_DOMAIN_INVALID;
    NATIVE_TRY(native_read(metadata+0x2dcu,&s_native.zone_eid));
    NATIVE_TRY(zdat_resources_request(level_native_zones(),s_native.zone_eid));
    boot_trace("native factory/controller/zone request bound");
#undef NATIVE_TRY
    return 1;
}
static int native_main_step(uint64_t now)
{
    if(!s_native.opened)return GOOL_DOMAIN_INVALID;
    if(s_native.status<0)return s_native.status;
    int error;
    for(unsigned i=0;i<24;i++)if(s_native.adio[i].initialized) {
        error=audio_resource_poll(&s_native.adio[i]);if(error<0)return s_native.status=error;
    }
    if(s_native.sequence.state!=AUDIO_SEQUENCE_RESOURCE_IDLE) {
        error=audio_sequence_resource_poll(&s_native.sequence,(now-s_native.clock_origin)*1000u,64);
        if(error<0)return s_native.status=error;
    }
    if(s_native.phase==0) {
        error=zdat_resources_poll(level_native_zones());if(error<0)return s_native.status=error;
        error=zdat_resources_acquire(level_native_zones(),s_native.zone_eid,&s_native.zone,&s_native.zone_lease);
        if(error==ZDAT_RESOURCE_BUSY)return 1;
        if(error)return s_native.status=error;
        s_native.zone_retained=1;
        uint32_t header,info,index,metadata;
        if((error=native_read(s_native.zone+16,&header))||
           (error=native_read(header+0x184u,&info))||
           (error=native_read(0x80061044u,&metadata))||
           (error=native_read(metadata+0x2e0u,&index))||
           (error=native_read(s_native.zone+16u+(info+index*3u)*4u,&s_native.database)))
            return s_native.status=error;
        gool_native_zone_transition_services services={.context=&s_native,.registry=s_native.registry,
            .retirement=s_native.retirement.services,.activation_query={&s_native.activation_query,
                gool_native_activation_query_begin,gool_native_activation_query_step,gool_native_activation_query_cancel},
            .neighbor_limit=16,.path_limit=65536,.activation_capacity=256,
            .call_begin=native_zone_call,.call_step=native_zone_step,.call_cancel=native_zone_cancel};
        error=gool_native_zone_transition_begin(&s_native.transition,s_native.zone,s_native.database,0,0,
            0x80060878u,&services);
        if(error!=1)return s_native.status=error?error:GOOL_DOMAIN_INVALID;
        s_native.phase=1;return 1;
    }
    if(s_native.phase==1) {
        error=gool_native_zone_transition_step(&s_native.transition,256);
        if(error)return error<0?s_native.status=error:1;
        error=gool_native_zone_transition_close(&s_native.transition);if(error)return s_native.status=error;
        error=native_write(0x80060ac0u,UINT32_MAX);if(error)return s_native.status=error;
        s_native.phase=2;boot_trace("native zone activation complete");
    }
    /* Keep the measured counter frozen while one world invocation, nested
     * script, resource request or presentation owns the current native tick. */
    if(!s_native.world.active) {
        uint32_t elapsed=(uint32_t)(now-s_native.clock_origin);
        s_native.tick=gool_world_tick(elapsed);
        s_native.timers[6]=(uint32_t)(now-s_native.last_frame);s_native.last_frame=now;
        s_native.timers[12]=elapsed;
        error=native_write(0x80068eecu,(uint32_t)s_native.tick);if(error)return s_native.status=error;
        error=gool_native_bounds_reset(s_native.domain);if(error)return s_native.status=error;
        uint32_t player,flags;
        error=native_read(0x80060a90u,&player);if(error)return s_native.status=error;
        /* Original1D16C dereferences the player before its late null guard.
         * An unfinished activation cannot masquerade as an empty world tick. */
        if(!player)return s_native.status=GOOL_NATIVE_OBJECT_INVALID;
        error=native_read(player+0x100u,&flags);if(error)return s_native.status=error;
        uint32_t kind=flags>>28;
        const uint32_t limits[3]={kind==8u?0x9c4000u:kind==13u?0x1f4000u:0x96000u,
            kind==8u?0x9c4000u:kind==13u?0x1f4000u:0xaf000u,
            kind==8u?0x9c4000u:kind==13u?0x1f4000u:0x96000u};
        for(unsigned i=0;i<3;i++) {
            error=native_write(0x8006921cu+i*4u,limits[i]);if(error)return s_native.status=error;
        }
        error=gool_native_engine_prepare(s_native.domain);if(error)return s_native.status=error;
        error=native_write(0x80060a98u,0);if(error)return s_native.status=error;
        error=native_runtime_begin(&s_native.world,1);if(error!=1)return s_native.status=error?error:GOOL_DOMAIN_INVALID;
    }
    error=native_runtime_step(&s_native.world,512);
    if(error<0) {
        char message[192];
        gool_native_actor *actor=s_native.world.object.actor;
        if(actor&&actor->vm) {
            snprintf(message,sizeof(message),"native fault %d node %08lX PC %lu op %08lX detail %d stage %lu",
                error,(unsigned long)actor->node,(unsigned long)actor->vm->fault_pc,
                (unsigned long)actor->vm->instruction,actor->vm->detail,
                (unsigned long)s_native.world.object.phase);
            boot_trace(message);
        }
    }
    if(!error) {
        uint32_t player,flags,world_flags;
        if((error=native_read(0x80060a90u,&player)))return s_native.status=error;
        if(player) {
            if((error=native_read(player+0x100u,&flags)))return s_native.status=error;
            uint32_t kind=flags>>28;
            if(kind==10u)return s_native.status=native_missing(0x8002b838u);
            if(kind==14u) {
                if((error=native_read(0x80068f1cu,&world_flags)))return s_native.status=error;
                if(world_flags&0x40000000u)return s_native.status=native_missing(0x8002b838u);
            }
        }
        error=native_write(0x1f80005cu,0x8005d120u);
        if(!error)error=gool_native_engine_frame_flags(s_native.domain);
    }
    if(!error) {
        s_native.completed_updates++;
        if(s_native.completed_updates==1u||s_native.completed_updates==120u) {
            uint32_t player,position[3],actors=0;
            error=native_read(0x80060a90u,&player);if(error)return s_native.status=error;
            for(unsigned i=0;i<3;i++) {
                error=native_read(player+0x60u+i*4u,&position[i]);if(error)return s_native.status=error;
            }
            for(unsigned i=0;i<s_native.factory.count;i++)actors+=s_native.factory.slots[i].actor.attached!=0;
            char message[160];
            snprintf(message,sizeof(message),"native updates %lu, tick %lu, actors %lu, player %08lX at %08lX/%08lX/%08lX",
                (unsigned long)s_native.completed_updates,(unsigned long)s_native.tick,(unsigned long)actors,
                (unsigned long)player,(unsigned long)position[0],(unsigned long)position[1],(unsigned long)position[2]);
            boot_trace(message);
        }
    }
    return error<0?s_native.status=error:error;
}
static int native_main_close(void)
{
    if(!s_native.opened)return 0;
    int error=gool_native_zone_transition_close(&s_native.transition);if(error)return error;
    error=gool_native_router_close(&s_native.creation_router);if(error)return error;
    error=gool_native_state_close(&s_native.creation_state);if(error)return error;
    error=native_runtime_close(&s_native.world);if(error)return error;
    error=gool_native_controller_close(&s_native.controller);if(error)return error;
    error=gool_native_zone_retirement_close(&s_native.retirement);if(error)return error;
    error=audio_sequence_resource_stop(&s_native.sequence);if(error)return error;
    for(unsigned i=0;i<24;i++)if(s_native.adio[i].initialized) {
        error=audio_resource_stop(&s_native.adio[i]);if(error)return error;
    }
    /* This is level-owner shutdown after every nested job was cancelled,
     * distinct from acknowledging a gameplay retirement notification. */
    for(unsigned i=0;i<s_native.factory.count;i++) {
        gool_native_factory_slot *slot=&s_native.factory.slots[i];
        while(slot->request) {
            error=gool_native_factory_cancel_request(&s_native.factory,slot->node);if(error)return error;
        }
        if(slot->actor.attached) {
            error=native_stop_audio(NULL,slot->node);if(error)return error;
            error=gool_native_actor_detach(&slot->actor);if(error)return error;
        }
    }
    error=gool_native_factory_close(&s_native.factory);if(error)return error;
    for(unsigned i=0;i<16;i++)if(s_native.resources[i].bank) {
        error=zdat_resources_release(s_native.resources[i].bank,s_native.resources[i].lease);if(error)return error;
        memset(&s_native.resources[i],0,sizeof(s_native.resources[i]));
    }
    error=zdat_resources_close(&s_native.miscellaneous);if(error)return error;
    if(s_native.zone_retained) {
        error=zdat_resources_release(level_native_zones(),s_native.zone_lease);if(error)return error;
        s_native.zone_retained=0;
    }
    while(s_native.mapped) {
        error=gool_domain_release(s_native.domain,s_native.mappings[s_native.mapped-1]);if(error)return error;
        s_native.mapped--;
    }
    memset(&s_native,0,sizeof(s_native));return 0;
}

static void init_level_source(void)
{
#ifndef LEVELS_FROM_SD
    /* Keep geometry and its entry metadata from the same build, and avoid
     * allocating a second 16 MB copy just because an old SD blob exists. */
    printf("[crash3ds] levels: using this build's embedded assets\n");
#else
    if (level_sd_open(LEVEL_SD_DEFAULT_PATH) == 0) {
        printf("[crash3ds] levels: SD card blob (%u bytes, %d levels)\n",
               (unsigned int)level_sd_blob_size(), level_sd_count());
    } else {
        printf("[crash3ds] levels: no valid SD blob at %s - nothing to load!\n",
               LEVEL_SD_DEFAULT_PATH);
    }
#endif
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    int exit_code = 1;
    int c3d_ready = 0;
    int c2d_ready = 0;

    gfxInitDefault();
    link3dsStdio();
    boot_trace("audio initialization begin");
    if(audio_init_native()!=0)boot_trace("native NDSP unavailable");
    boot_trace("audio initialization complete");
    story_init();
    init_level_source();
    boot_trace("level source ready");
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE))
        goto shutdown;
    c3d_ready = 1;

    /* Setup Top Screen Render Target */
    s_target = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    if (!s_target)
        goto shutdown;
    C3D_RenderTargetSetOutput(s_target, GFX_TOP, GFX_LEFT,
        (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) |
         GX_TRANSFER_RAW_COPY(0) | GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
         GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
         GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO)));

    /* Setup Bottom Screen Render Target (Dual Screen) */
    s_target_bottom = C3D_RenderTargetCreate(240, 320, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    if (!s_target_bottom)
        goto shutdown;
    C3D_RenderTargetSetOutput(s_target_bottom, GFX_BOTTOM, GFX_LEFT,
        (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) |
         GX_TRANSFER_RAW_COPY(0) | GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
         GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
         GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO)));

    if (sceneInit() != 0)
        goto shutdown;
    /* Three vertices per triangle, bounded by the existing CPU canvas.
     * citro2d reserves four vertices per object, once at initialization. */
    if (!C2D_Init((BOTTOM_UI_CAPACITY + 3u) / 4u))
        goto shutdown;
    c2d_ready = 1;
    boot_trace("display targets and color shader ready");
    if (world_texture_init() != 0)
        printf("[graphics] texture shader unavailable\n");
    else if(world_texture_load_actor()!=0)
        printf("[graphics] original character textures unavailable\n");
    if(world_texture_load_props()!=0)
        printf("[graphics] original prop texture set unavailable\n");
    boot_trace("texture resources ready");

    C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
    C3D_CullFace(GPU_CULL_NONE);

    Mtx_PerspStereoTilt(&s_projection, C3D_AngleFromDegrees(60.0f),
                        C3D_AspectRatioTop, 8.0f, 60000.0f, 0.0f, 2.0f, false);

    int first = -1;
    for (int i = 0; i < level_count(); i++) {
        const level_info *info = level_get(i);
        /* Original main11628 selects title archive3C at11650. */
        if (info && info->id == 0x3C) { first = i; break; }
    }
    if (first < 0) first = 0;
    s_menu_index = first;
    s_target_level_idx=first;
    s_state=STATE_LOADING;
    s_loading_ticks=0;s_loading_prepared=0;
    boot_trace("original native title archive3C selected");

    s_fps_time = osGetTime();
    u64 previous_update_time = s_fps_time;
    unsigned submitted_frames=0;

    while (aptMainLoop()) {
        audio_stream_poll_all();
        /* Acquire the GPU queue before input can replace level resources or
         * rendering can rewrite the shared character/UI vertex buffers.
         * FrameSync alone only waits for VBlank, not GPU completion. */
        if (!C3D_FrameBegin(0)) {
            boot_trace("frame begin rejected");
            continue;
        }
        u64 update_time = osGetTime();
        float frame_dt = (float)(update_time - previous_update_time) * 0.001f;
        previous_update_time = update_time;
        if (frame_dt < 1.0f / 240.0f) frame_dt = 1.0f / 240.0f;
        if (frame_dt > 1.0f / 20.0f) frame_dt = 1.0f / 20.0f;
        hidScanInput();
        u32 kDown = hidKeysDown();
        s_ui_tick++;

        if (s_save_toast_timer > 0.0f)
            s_save_toast_timer -= frame_dt;

        /* Touch interaction on bottom screen buttons */
        if (kDown & KEY_TOUCH) {
            touchPosition touch;
            hidTouchRead(&touch);
            bottom_ui_action action=bottom_ui_hit_test(touch.px,touch.py,s_state==STATE_PAUSE);
            if(action==BOTTOM_UI_STATUS_PAGE || action==BOTTOM_UI_TRIAL_PAGE) {
                s_bottom_page=action==BOTTOM_UI_STATUS_PAGE?BOTTOM_PAGE_STATUS:BOTTOM_PAGE_TRIAL;
                audio_play_sfx(SFX_MENU_SELECT);
            } else if (s_state == STATE_PAUSE && action != BOTTOM_UI_NONE) {
                if(action==BOTTOM_UI_RESUME) s_pause_index=0;
                else if(action==BOTTOM_UI_RESTART) s_pause_index=1;
                else if(action==BOTTOM_UI_HUB) s_pause_index=2;
                else if(action==BOTTOM_UI_TITLE) s_pause_index=3;
                kDown |= KEY_A;
            } else if (action != BOTTOM_UI_NONE &&
                       (s_state==STATE_GAMEPLAY || s_state==STATE_WARP_ROOM || s_state==STATE_TITLE)) {
                if (action == BOTTOM_UI_SAVE) {
                    /* SAVE SD */
                    if (story_save() == 0) {
                        snprintf(s_save_toast, sizeof(s_save_toast), "SAVED TO SD (%lu%%)!", (unsigned long)g_save.completion_pct);
                        audio_play_sfx(SFX_MENU_SELECT);
                    } else {
                        snprintf(s_save_toast, sizeof(s_save_toast), "SAVE FAILED!");
                    }
                    s_save_toast_timer = 2.0f;
                } else if (action == BOTTOM_UI_LOAD) {
                    /* LOAD SD */
                    if (story_load() == 0) {
                        snprintf(s_save_toast, sizeof(s_save_toast), "LOADED SAVE (%lu%%)!", (unsigned long)g_save.completion_pct);
                        audio_play_sfx(SFX_MENU_SELECT);
                    } else {
                        snprintf(s_save_toast, sizeof(s_save_toast), "NO SAVE ON SD!");
                    }
                    s_save_toast_timer = 2.0f;
                } else if (action == BOTTOM_UI_HUB) {
                    /* WARP ROOM */
                    s_state = STATE_WARP_ROOM; audio_play_bgm(BGM_WARP_ROOM);
                    audio_play_sfx(SFX_MENU_SELECT);
                } else if (action == BOTTOM_UI_CAMERA && s_state==STATE_GAMEPLAY) {
                    /* CAMERA ZOOM */
                    if (s_cam_dist < 165.0f)
                        s_cam_dist = 190.0f;
                    else if (s_cam_dist < 220.0f)
                        s_cam_dist = 250.0f;
                    else
                        s_cam_dist = 140.0f;
                    audio_play_sfx(SFX_MENU_SELECT);
                }
            }
        }

        /* -------------------------------------------------------------- */
        /* State machine logic                                            */
        /* -------------------------------------------------------------- */
        if (s_state == STATE_LOGOS) {
            s_logo_timer += frame_dt;
            if ((kDown & KEY_START) || (kDown & KEY_A) || s_logo_timer >= 6.0f) {
                audio_play_sfx(SFX_MENU_SELECT);
                s_state = STATE_TITLE;
                audio_play_bgm(BGM_TITLE);
            }
        } else if (s_state == STATE_TITLE) {
            if ((kDown & KEY_START) || (kDown & KEY_A)) {
                audio_play_sfx(SFX_MENU_SELECT);
                if (g_save.completion_pct == 0) {
                    s_state = STATE_INTRO;
                } else {
                    s_state = STATE_WARP_ROOM;
                    audio_play_bgm(BGM_WARP_ROOM);
                }
            } else if (kDown & KEY_Y) {
                audio_play_sfx(SFX_MENU_SELECT);
                s_state = STATE_LEVEL_SELECT;
            } else if (kDown & KEY_SELECT) {
                C3D_FrameEnd(0);
                break;
            }
        } else if (s_state == STATE_INTRO) {
            if ((kDown & KEY_A) || (kDown & KEY_START)) {
                audio_play_sfx(SFX_MENU_SELECT);
                s_state = STATE_WARP_ROOM; audio_play_bgm(BGM_WARP_ROOM);
            }
        } else if (s_state == STATE_WARP_ROOM) {
            if (kDown & KEY_L) {
                s_active_chamber = (s_active_chamber - 1 + TOTAL_CHAMBERS) % TOTAL_CHAMBERS;
                audio_play_sfx(SFX_MENU_SELECT);
            }
            if (kDown & KEY_R) {
                s_active_chamber = (s_active_chamber + 1) % TOTAL_CHAMBERS;
                audio_play_sfx(SFX_MENU_SELECT);
            }
            int portal_count = story_get_chamber_portal_count(s_active_chamber);
            if (s_warp_cursor >= portal_count) s_warp_cursor = portal_count - 1;
            if (kDown & KEY_DUP) {
                s_warp_cursor = (s_warp_cursor - 1 + portal_count) % portal_count;
                audio_play_sfx(SFX_MENU_SELECT);
            }
            if (kDown & KEY_DDOWN) {
                s_warp_cursor = (s_warp_cursor + 1) % portal_count;
                audio_play_sfx(SFX_MENU_SELECT);
            }
            if ((kDown & KEY_A) || (kDown & KEY_START)) {
                if (load_story_portal(s_active_chamber, s_warp_cursor) == 0)
                    audio_play_sfx(SFX_MENU_SELECT);
                else
                    audio_play_sfx(SFX_SPIN);
            } else if (kDown & KEY_B) {
                audio_play_sfx(SFX_MENU_SELECT);
                s_state = STATE_TITLE;
                audio_play_bgm(BGM_TITLE);
            }
        } else if (s_state == STATE_LEVEL_SELECT) {
            int n = level_count();
            if (n > 0) {
                if (kDown & KEY_DUP)    { s_menu_index = (s_menu_index - 1 + n) % n; audio_play_sfx(SFX_MENU_SELECT); }
                if (kDown & KEY_DDOWN)  { s_menu_index = (s_menu_index + 1) % n; audio_play_sfx(SFX_MENU_SELECT); }
                if (kDown & KEY_DLEFT)  { s_menu_index = (s_menu_index - 5 + n) % n; audio_play_sfx(SFX_MENU_SELECT); }
                if (kDown & KEY_DRIGHT) { s_menu_index = (s_menu_index + 5) % n; audio_play_sfx(SFX_MENU_SELECT); }
            }
            if ((kDown & KEY_A) || (kDown & KEY_START)) {
                audio_play_sfx(SFX_MENU_SELECT);
                s_target_level_idx = s_menu_index;
                s_pending_portal = NULL; /* developer archive browser */
                s_state = STATE_LOADING;
                s_loading_ticks = 0;
                s_loading_prepared = 0;
                audio_stop_bgm();
            } else if (kDown & KEY_B) {
                audio_play_sfx(SFX_MENU_SELECT);
                s_state = STATE_TITLE;
                audio_play_bgm(BGM_TITLE);
            }
        } else if (s_state == STATE_LOADING) {
            if(s_loading_ticks<0x7fffffff)s_loading_ticks++;
            if (s_loading_ticks >= 2) {
                int result=0;
                if(!s_loading_prepared) {
                    s_loading_error=0;s_loading_native_address=0;s_loading_stage[0]=0;
                    result=load_level_by_index(s_target_level_idx);
                    if(!result)s_loading_prepared=1;
                }
                if(!result&&s_loading_prepared==1) {
                    result=loading_result("GOOL residency",level_behaviors_poll());
                    if(result==LEVEL_BEHAVIORS_READY) {
                        result=loading_result("native memory/pool",level_native_open((uint32_t)s_loaded_id));
                        if(!result)s_loading_prepared=2;
                    }
                }
                if(!result&&s_loading_prepared==2) {
                    result=loading_result("native services",native_main_open());
                    if(result>=0) {s_loading_prepared=3;result=0;}
                }
                if(!result&&s_loading_prepared==3) {
                    result=loading_result("native zone activation",native_main_step(update_time));
                    if(!result) {
                        s_state=STATE_GAMEPLAY;s_loading_ticks=0;s_loading_prepared=0;
                        boot_trace("first native world update complete");
                    }
                }
                if(result<0) {
                    s_loading_native_address=s_native.unimplemented;
                    int cleanup=close_level_runtime();
                    if(cleanup)loading_result("failed level cleanup",cleanup);
                    snprintf(s_save_toast,sizeof(s_save_toast),"NATIVE BOOT FAILED (%d)",result);
                    s_save_toast_timer=5.0f;
                    s_pending_portal = NULL;
                    s_state = STATE_LOAD_FAILED;
                    s_loading_ticks=0;s_loading_prepared=0;
                }
            }
        } else if (s_state == STATE_LOAD_FAILED) {
            if(kDown&(KEY_A|KEY_START)) {
                s_state=STATE_LOADING;s_loading_ticks=0;s_loading_prepared=0;
            } else if(kDown&KEY_B) s_state=STATE_LEVEL_SELECT;
        } else if (s_state == STATE_ENTRANCE) {
            s_state=STATE_GAMEPLAY;
        } else if (s_state == STATE_GAMEPLAY) {
            if (kDown & KEY_START) {
                s_state = STATE_PAUSE;
                s_pause_index = 0;
            } else {
                int native_status=native_main_step(update_time);
                if(native_status<0) {
                    loading_result("native activation/frame",native_status);
                    s_loading_native_address=s_native.unimplemented;
                    s_state=STATE_LOAD_FAILED;
                    snprintf(s_save_toast,sizeof(s_save_toast),"NATIVE %08lX ERROR %d",
                        (unsigned long)s_native.unimplemented,native_status);
                    s_save_toast_timer=5.0f;
                }
            }
        } else if (s_state == STATE_LEVEL_CLEAR) {
            if ((kDown & KEY_A) || (kDown & KEY_START) || (kDown & KEY_B)) {
                audio_play_sfx(SFX_MENU_SELECT);
                if (s_loaded_portal) {
                    s_active_chamber = s_loaded_portal->chamber_idx;
                    s_warp_cursor = s_loaded_portal->slot_idx;
                } else {
                    int cleared_ch = story_get_chamber(s_loaded_id);
                    if (cleared_ch >= 0 && cleared_ch < TOTAL_CHAMBERS) {
                        s_active_chamber = cleared_ch;
                        for (int slot=0;slot<story_get_chamber_portal_count(cleared_ch);++slot) {
                            const StoryPortal *portal=story_get_portal(cleared_ch,slot);
                            if (portal && portal->level_id==s_loaded_id) {s_warp_cursor=slot;break;}
                        }
                    }
                }
                s_state = STATE_WARP_ROOM; audio_play_bgm(BGM_WARP_ROOM);
            }
        } else if (s_state == STATE_PAUSE) {
            if (kDown & KEY_DUP)
                s_pause_index = (s_pause_index - 1 + 4) % 4;
            if (kDown & KEY_DDOWN)
                s_pause_index = (s_pause_index + 1) % 4;
            if ((kDown & KEY_START) || (kDown & KEY_B)) {
                s_state = STATE_GAMEPLAY;
                play_level_theme(s_loaded_id);
            } else if (kDown & KEY_A) {
                switch (s_pause_index) {
                case 0: /* CONTINUE */
                    s_state = STATE_GAMEPLAY;
                    play_level_theme(s_loaded_id);
                    break;
                case 1: /* RESTART LEVEL */
                    s_target_level_idx=level_find((uint32_t)s_loaded_id);
                    s_state=STATE_LOADING;s_loading_ticks=0;s_loading_prepared=0;
                    break;
                case 2: /* WARP ROOM */
                    s_state = STATE_WARP_ROOM; audio_play_bgm(BGM_WARP_ROOM);
                    break;
                case 3: /* TITLE SCREEN */
                    s_state = STATE_TITLE;
                    audio_play_bgm(BGM_TITLE);
                    break;
                }
            }
        }

        /* -------------------------------------------------------------- */
        /* Render Dual-Screen Frame (Top 400x240, Bottom 320x240)         */
        /* -------------------------------------------------------------- */
            /* TOP SCREEN RENDERING */
            C3D_RenderTargetClear(s_target, C3D_CLEAR_ALL, CLEAR_COLOR, 0);
            C3D_FrameDrawOn(s_target);
            restore_color_pipeline();

            if (s_state == STATE_GAMEPLAY || s_state == STATE_ENTRANCE || s_state == STATE_PAUSE) {
                C3D_Mtx mv;
                build_view(&mv);
                C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, s_uLoc_projection, &s_projection);
                render_world(&mv);
                /* Native actor admission/presentation belongs to the bound
                 * scheduler. Prototype entity/model draws cannot substitute
                 * for an unresolved native service. */
                draw_hud();
                render_text_top();
            } else if (s_state == STATE_LOGOS) {
                C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, s_uLoc_projection, &s_ortho);
                draw_logos_screen();
            } else if (s_state == STATE_TITLE) {
                C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, s_uLoc_projection, &s_ortho);
                draw_title_screen();
            } else if (s_state == STATE_INTRO) {
                C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, s_uLoc_projection, &s_ortho);
                draw_intro_screen();
            } else if (s_state == STATE_WARP_ROOM) {
                C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, s_uLoc_projection, &s_ortho);
                draw_warp_room_screen();
            } else if (s_state == STATE_LEVEL_SELECT) {
                C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, s_uLoc_projection, &s_ortho);
                draw_menu();
            } else if (s_state == STATE_LOAD_FAILED) {
                C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,s_uLoc_projection,&s_ortho);
                text_reset();
                char failure[64];
                text_draw(52,52,2,"LEVEL LOAD FAILED",1.0f,0.35f,0.25f);
                text_draw(24,105,1,s_loading_stage,0.9f,0.9f,0.9f);
                snprintf(failure,sizeof(failure),"ERROR %d / NATIVE %08lX",s_loading_error,
                    (unsigned long)s_loading_native_address);
                text_draw(24,125,1,failure,1.0f,0.7f,0.3f);
                text_draw(24,165,1,"DETAILS: /3ds/crashwarped3ds/boot.log",0.7f,0.8f,0.9f);
                text_draw(24,195,1,"A: RETRY   B: LEVEL ARCHIVES",0.8f,0.8f,0.8f);
                render_text_top();
            } else if (s_state == STATE_LOADING) {
                C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, s_uLoc_projection, &s_ortho);
                draw_loading_screen(s_target_level_idx, s_loading_ticks);
            } else if (s_state == STATE_LEVEL_CLEAR) {
                C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, s_uLoc_projection, &s_ortho);
                draw_level_clear_screen();
            }

            /* BOTTOM SCREEN RENDERING (Interactive HUD / Touch controls) */
            C3D_RenderTargetClear(s_target_bottom, C3D_CLEAR_ALL, BOTTOM_CLEAR_COLOR, 0);
            C2D_Prepare();
            C2D_SceneBegin(s_target_bottom);
            C2D_ViewReset();
            if (draw_bottom_screen() != 0) {
                C2D_Flush();
                C3D_FrameEnd(0);
                goto shutdown;
            }

        C3D_FrameEnd(0);

        if(submitted_frames++==0)boot_trace("first frame submitted");
        if(submitted_frames==60)boot_trace("sixty frames submitted");

        /* Real-time FPS calculation */
        s_fps_count++;
        u64 now = osGetTime();
        if (now - s_fps_time >= 1000ULL) {
            s_fps_frames = s_fps_count;
            s_fps_count = 0;
            s_fps_time = now;
        }
    }

    exit_code = 0;
shutdown:
    if (exit_code)
        printf("[crash3ds] graphics initialization or rendering failed\n");
    /* Fini drains submitted commands before any referenced buffers, texture
     * storage or shader programs are released. It also owns render targets. */
    if (c3d_ready) C3D_Fini();
    /* The C3D shutdown above drains GPU work before releasing C2D buffers.
     * C2D_Fini then detaches its frame hook and frees its shader/storage. */
    if (c2d_ready) C2D_Fini();
    world_texture_exit();
    level_unload();
    level_sd_close();
    if (s_charbuf) linearFree(s_charbuf);
    if (s_propbuf) linearFree(s_propbuf);
    if (s_textbuf) linearFree(s_textbuf);
    audio_stream_stop_all();
    int runtime_close_error=close_level_runtime();
    if(runtime_close_error) {
        printf("[resources] runtime close failed (%d)\n",runtime_close_error);
        exit_code=1;
    }
    audio_exit();
    audio_stream_release_all_after_ndsp_exit();
    if (s_vshader_dvlb) {
        shaderProgramFree(&s_program);
        DVLB_Free(s_vshader_dvlb);
    }
    gfxExit();
    return exit_code;
}
