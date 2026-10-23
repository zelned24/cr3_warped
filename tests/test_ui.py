"""Run production UI emitters and controls with canaries and deterministic HID.

Only SDK calls are replaced. Function bodies are extracted unchanged from
main.c so the tested code is the same code compiled for the console.
Run: python tests/test_ui.py (requires the ziglang Python package).
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    start = source.index("static ", source.index(name) - 20)
    brace = source.index("{", start)
    depth = 1
    pos = brace + 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[start:pos] + "\n"


PRELUDE = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef NDEBUG
#error UI assertions must be enabled
#endif
#include "props.h"
#include "story.h"
#include "audio.h"
#include "font_data.h"
#include "bottom_ui.h"
typedef uint32_t u32;
typedef struct { int dx, dy; } circlePosition;
typedef struct { int px, py; } touchPosition;
enum { KEY_A=1, KEY_B=2, KEY_Y=4, KEY_R=8, KEY_L=16, KEY_DDOWN=32,
       KEY_DUP=64, KEY_DLEFT=128, KEY_DRIGHT=256, KEY_TOUCH=512, KEY_X=1024 };
static u32 held, down;
static circlePosition stick;
static touchPosition touch;
static u32 hidKeysHeld(void) { return held; }
static u32 hidKeysDown(void) { return down; }
static void hidCircleRead(circlePosition *p) { *p=stick; }
static void hidTouchRead(touchPosition *p) { *p=touch; }
static level_vertex *s_textbuf, *s_bottom_buf;
static int s_textn, s_bottom_n;
static char_state s_player;
static int s_loaded_id = 11;
static const char *const level_names[64] = {[11]="TOAD VILLAGE"};
static float s_cam_yaw, s_cam_pitch, s_cam_dist=220, s_spawn_x,s_spawn_y,s_spawn_z;
static float s_save_toast_timer;
static char s_save_toast[48] = "SAVED TO SD (105%)!";
SaveData g_save;
game_stats g_stats;
int story_has_power(uint32_t f) { return (g_save.unlocked_powers & f) != 0; }
void audio_play_sfx(SfxId id) { (void)id; }
void audio_stop_bgm(void) {}
void props_spawn_landing_dust(float x,float y,float z) { (void)x;(void)y;(void)z; }
void props_update(float dt,char_state *p) { (void)dt;(void)p; }
int props_checkpoint_point(float *x,float *y,float *z) { (void)x;(void)y;(void)z;return 0; }
static int respawned;
static void start_entrance(void) { respawned++; }
int level_height_below(float x,float z,float max_y) {
    (void)x;(void)z; return max_y >= 0 ? 0 : -1000000;
}
enum { STATE_TITLE, STATE_ENTRANCE, STATE_GAMEPLAY, STATE_LEVEL_CLEAR,
       STATE_WARP_ROOM, STATE_LOGOS, STATE_INTRO, STATE_PAUSE };
static int s_active_chamber, s_warp_cursor, s_pause_index;
static bottom_ui_page s_bottom_page;
static const StoryPortal fixture_portal={0,11,0,0,STORY_ENTRY_NORMAL,0};
const StoryPortal *story_get_portal(int chamber,int slot) { (void)chamber;(void)slot;return &fixture_portal; }
static const StoryPortal *hub_portal_for_archive(int lid) { (void)lid;return &fixture_portal; }
StoryPortalStatus story_resolve_portal_id(int id,const StoryPortal **out) { (void)id;*out=&fixture_portal;return STORY_PORTAL_READY; }
int story_has_relic_targets(int lid) { return lid==11; }
void story_get_canonical_relic_times(int lid,uint32_t *s,uint32_t *g,uint32_t *p) { (void)lid;*s=60000;*g=50000;*p=40000; }
static const char *chamber_names[6]={"ONE","TWO","THREE","FOUR","FIVE","SIX"};
int story_get_boss_level_id(int index) { (void)index; return -1; }
int story_is_level_unlocked(int index) { (void)index; return 1; }
const prop_item *props_get_nearest_portal(float x,float y,float z,float *dist) {
    (void)x;(void)y;(void)z;*dist=999;return NULL;
}
static int s_state = STATE_TITLE;
static int s_bazooka_mode = 0;
static float shot_dx,shot_dy,shot_dz;
static int shots;
void props_spawn_projectile(float x,float y,float z,float dx,float dy,float dz) {
    (void)x;(void)y;(void)z;shot_dx=dx;shot_dy=dy;shot_dz=dz;shots++;
}
int props_check_exit(float px,float py,float pz) { (void)px;(void)py;(void)pz; return 0; }
int props_is_time_trial(void) { return 0; }
uint32_t props_get_time_trial_ms(void) { return 0; }
int props_boss_active(void) { return 0; }
int props_boss_hp(void) { return 0; }
void story_on_level_cleared(int level_id,int all_crates,uint32_t time_ms) {
    (void)level_id;(void)all_crates;(void)time_ms;
}
int story_save(void) { return 0; }
int level_move_horizontal(float *x, float *z, float foot_y,
                          float dx, float dz, float radius,
                          float body_height, float step_height) {
    (void)foot_y; (void)radius; (void)body_height; (void)step_height;
    *x += dx; *z += dz; return 0;
}
static int bottom_draw_calls;
static int render_text_bottom(void) { bottom_draw_calls++; return 0; }
'''


TESTS = r'''
typedef struct { uint64_t before; level_vertex vertices[TOP_UI_CAPACITY]; uint64_t after; } TopGuard;
typedef struct { uint64_t before; level_vertex vertices[BOTTOM_UI_CAPACITY]; uint64_t after; } BottomGuard;
static void bounds(const level_vertex *v,int count,int width) {
    for(int i=0;i<count;i++) {
        assert(v[i].x >= 0 && v[i].x <= width);
        assert(v[i].y >= 0 && v[i].y <= 240);
    }
}
int main(void) {
    TopGuard *top=calloc(1,sizeof(*top));
    BottomGuard *bottom=calloc(1,sizeof(*bottom)); assert(top && bottom);
    top->before=top->after=bottom->before=bottom->after=UINT64_C(0x92abcdef12345678);
    s_textbuf=top->vertices; s_bottom_buf=bottom->vertices;
    text_reset();
    // Dense screen-filling text and clipping at all four boundaries.
    for(int y=-2;y<245;y+=7) {
        text_draw(-3,y,1,"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ",1,1,1);

    }
    bounds(s_textbuf,s_textn,400);
    // Deliberately exhaust capacity through every emitter, preserving canaries.
    for(int i=0;i<100000;i++) {
        draw_rect_2d(0,0,10,10,1,1,1);
        draw_diamond_2d(10,10,5,0,1,1,1);
        draw_frame_2d(0,0,10,10,1,1,1,1);
        text_draw(0,0,1,"TEST",1,1,1);
    }
    assert(s_textn==TOP_UI_CAPACITY);
    draw_hud(); assert(s_textn == 0);
    assert(top->before==top->after && top->before==UINT64_C(0x92abcdef12345678));
    assert(bottom->before==bottom->after && bottom->before==top->before);
    memset(&g_save,0,sizeof(g_save));
    g_save.completion_pct=105; g_save.crystals_count=25;g_save.gems_count=45;g_save.relics_count=30;
    g_save.unlocked_powers=31;
    g_stats=(game_stats){.wumpa_count=99,.crates_broken=64,.crates_total=64,.lives=99,.aku_aku_tier=0,.aku_timer=0.0f}; s_save_toast_timer=2;
    draw_bottom_screen();
    assert(bottom_draw_calls==1 && s_bottom_n>0 && s_bottom_n<BOTTOM_UI_CAPACITY);
    bounds(s_bottom_buf,s_bottom_n,320);
    printf("Bottom HUD vertices: %d / %d\n",s_bottom_n,BOTTOM_UI_CAPACITY);
    bottom_ui_state small = {.level_name="TOAD VILLAGE", .message="SAVED", .powers=31};
    for(int limit=0;limit<BOTTOM_UI_CAPACITY;limit+=137) {
        memset(s_bottom_buf,0x5a,sizeof(bottom->vertices));
        int count=bottom_ui_build(s_bottom_buf,limit,&small);
        assert(count>=0 && count<=limit && count%3==0);
        unsigned char *next=(unsigned char *)(s_bottom_buf+limit);
        for(unsigned byte=0;byte<sizeof(level_vertex);byte++) assert(next[byte]==0x5a);
    }
    assert(bottom->before==bottom->after && bottom->before==top->before);
    // Every pixel of the touch row agrees with the shared rendered bounds.
    for(int y=0;y<240;y++) for(int x=0;x<320;x++) {
        int slot=-1;
        if(y>=168 && y<196) {
            if(x>=10 && x<78) slot=0;
            else if(x>=84 && x<152) slot=1;
            else if(x>=158 && x<234) slot=2;
            else if(x>=240 && x<310) slot=3;
        }
        const bottom_ui_action expected[4]={BOTTOM_UI_SAVE,BOTTOM_UI_LOAD,BOTTOM_UI_HUB,BOTTOM_UI_CAMERA};
        const bottom_ui_action paused[4]={BOTTOM_UI_RESUME,BOTTOM_UI_RESTART,BOTTOM_UI_HUB,BOTTOM_UI_TITLE};
        bottom_ui_action page=BOTTOM_UI_NONE;
        if(y>=218 && y<236) {
            if(x>=10 && x<152) page=BOTTOM_UI_STATUS_PAGE;
            else if(x>=160 && x<310) page=BOTTOM_UI_TRIAL_PAGE;
        }
        assert(bottom_ui_hit_test(x,y,0)==(page!=BOTTOM_UI_NONE?page:slot<0?BOTTOM_UI_NONE:expected[slot]));
        assert(bottom_ui_hit_test(x,y,1)==(page!=BOTTOM_UI_NONE?page:slot<0?BOTTOM_UI_NONE:paused[slot]));
    }
    char time_text[24];
    bottom_ui_format_time(time_text,sizeof(time_text),0,1); assert(!strcmp(time_text,"00:00.00"));
    bottom_ui_format_time(time_text,sizeof(time_text),0,0); assert(!strcmp(time_text,"--:--.--"));
    bottom_ui_format_time(time_text,sizeof(time_text),UINT32_MAX,1); assert(!strcmp(time_text,"71582:47.29"));
    small.page=BOTTOM_PAGE_TRIAL;small.trial_targets_available=1;small.trial_targets_ms[0]=60000;
    small.trial_targets_ms[1]=50000;small.trial_targets_ms[2]=40000;small.best_time_ms=45000;small.saved_relic_tier=2;
    s_bottom_n=bottom_ui_build(s_bottom_buf,BOTTOM_UI_CAPACITY,&small);
    assert(s_bottom_n>0 && s_bottom_n<BOTTOM_UI_CAPACITY);bounds(s_bottom_buf,s_bottom_n,320);
    // Button-origin gesture must never rotate the camera, including after crossing above.
    held=down=KEY_TOUCH;touch=(touchPosition){20,180};update_camera();
    down=0;touch=(touchPosition){20,100};update_camera();
    touch=(touchPosition){100,100};update_camera();assert(s_cam_yaw==0);
    held=0;update_camera();
    held=down=KEY_TOUCH;touch=(touchPosition){20,100};update_camera();
    down=0;touch=(touchPosition){30,100};update_camera();assert(s_cam_yaw>0);
    held=0;update_camera();s_cam_yaw=0;
    // D-pad moves player and no longer rotates camera or accidentally body-slams.
    memset(&s_player,0,sizeof(s_player));s_player.onground=1;
    held=KEY_DRIGHT;update_player(1.0f/60);assert(s_player.x>0);
    update_camera();assert(s_cam_yaw==0);
    s_player.y=100;s_player.vy=0;s_player.onground=0;
    held=down=KEY_DDOWN;update_player(1.0f/60);assert(!s_player.is_slamming);
    // A grounded jump must remain airborne rather than immediately snap to its floor.
    memset(&s_player,0,sizeof(s_player));s_player.onground=1;
    held=0;down=KEY_A;update_player(1.0f/60);
    assert(s_player.y>0 && s_player.vy>0 && !s_player.onground);
    assert(!respawned);
    // Exercise camera and movement together in gameplay, not only title state.
    // Held Up must continue into the original -Z route without a feedback orbit.
    s_state=STATE_GAMEPLAY;
    memset(&s_player,0,sizeof(s_player));s_player.onground=1;
    s_cam_yaw=0;s_cam_pitch=-0.38f;held=KEY_DUP;down=0;
    for(int i=0;i<180;i++) {
        float previous_z=s_player.z;
        update_camera();update_player(1.0f/60);
        assert(s_player.z<previous_z && fabsf(s_player.x)<0.001f);
        assert(s_cam_yaw==0);
    }
    // Sideways movement must not orbit the camera or turn into forward motion.
    memset(&s_player,0,sizeof(s_player));s_player.onground=1;
    held=KEY_DRIGHT;
    for(int i=0;i<60;i++) {update_camera();update_player(1.0f/60);}
    assert(s_player.x>220 && fabsf(s_player.z)<0.001f && s_cam_yaw==0);
    // Manual framing remains stable when movement resumes.
    s_cam_yaw=0.65f;s_cam_pitch=-0.5f;
    held=KEY_DUP;
    for(int i=0;i<60;i++) {update_camera();update_player(1.0f/60);}
    assert(s_cam_yaw==0.65f && s_cam_pitch==-0.5f);
    // View looks along negative local Z. Fire through the crosshair, including
    // rotated and tilted views, rather than behind the camera.
    g_save.unlocked_powers=POWER_BAZOOKA;
    const float angles[][2]={{0,0},{1.57079633f,0},{0,0.52359878f},{-0.8f,-0.4f}};
    for(unsigned i=0;i<sizeof(angles)/sizeof(angles[0]);i++) {
        memset(&s_player,0,sizeof(s_player));s_player.onground=1;
        s_cam_yaw=angles[i][0];s_cam_pitch=angles[i][1];
        held=KEY_L;down=KEY_B;int before=shots;update_player(1.0f/60);
        assert(shots==before+1);
        assert(fabsf(shot_dx+sinf(s_cam_yaw)*cosf(s_cam_pitch))<0.00001f);
        assert(fabsf(shot_dy-sinf(s_cam_pitch))<0.00001f);
        assert(fabsf(shot_dz+cosf(s_cam_yaw)*cosf(s_cam_pitch))<0.00001f);
    }
    s_cam_yaw=s_cam_pitch=0;held=KEY_L|KEY_DRIGHT;down=0;
    update_player(1.0f/60);assert(s_cam_yaw<0); // right aims toward +X
    s_cam_yaw=0;held=KEY_L|KEY_DLEFT;
    update_player(1.0f/60);assert(s_cam_yaw>0);
    s_cam_pitch=0;held=KEY_L|KEY_DUP;
    update_player(1.0f/60);assert(s_cam_pitch>0);
    s_cam_yaw=s_cam_pitch=0;held=KEY_L;stick=(circlePosition){150,150};
    update_player(1.0f/60);assert(s_cam_yaw<0 && s_cam_pitch>0);
    s_cam_yaw=s_cam_pitch=0;stick=(circlePosition){-150,-150};
    update_player(1.0f/60);assert(s_cam_yaw>0 && s_cam_pitch<0);
    free(top);free(bottom);
    puts("PASS: UI canaries, clipping, bottom HUD, touch, gameplay camera, movement and bazooka view alignment");
    return 0;
}
'''


def main():
    source = (ROOT / "source/main.c").read_text(encoding="utf-8")
    constants = "\n".join(re.findall(r"^#define (?:SCREEN_[WH]|BOTTOM_[WH]|TOP_UI_CAPACITY|BOTTOM_UI_CAPACITY) .*$", source, re.M))
    names = ["text_reset", "text_glyph_ex", "text_draw", "draw_rect_2d", "draw_frame_2d", "draw_diamond_2d",
             "draw_crosshair_2d", "update_player", "update_camera", "draw_hud", "draw_bottom_screen"]
    body = "\n".join(function(source, name + "(") for name in names)
    with tempfile.TemporaryDirectory(prefix="crash-ui-") as folder:
        test = Path(folder) / "test.c"
        exe = Path(folder) / "test.exe"
        test.write_text(constants + "\n" + PRELUDE + body + TESTS, encoding="utf-8")
        subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-UNDEBUG",
                        "-I" + str(ROOT / "source"), str(test), str(ROOT / "source/bottom_ui.c"), "-lm", "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
