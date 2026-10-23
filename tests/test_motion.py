"""Regressions for real movement/entrance functions and extracted animation clips."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def extract(source, name):
    match = re.search(r"^static\s+\w+\s+" + name + r"\([^;{}]*\)\s*\{", source, re.M)
    if match is None:
        raise ValueError("Function definition missing: " + name)
    end = source.index("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


PRELUDE = r'''
#include <assert.h>
#ifdef NDEBUG
#error Motion regressions require assertions
#endif
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "char.c"
#include "props.h"
#include "story.h"
#include "audio.h"
typedef unsigned u32;
typedef struct { int dx,dy; } circlePosition;
enum { KEY_A=1, KEY_B=2, KEY_Y=4, KEY_R=8, KEY_L=16, KEY_DDOWN=32,
       KEY_DUP=64, KEY_DLEFT=128, KEY_DRIGHT=256, KEY_START=512, KEY_X=1024 };
enum { STATE_ENTRANCE, STATE_GAMEPLAY, STATE_LEVEL_CLEAR };
static int s_state;
static int s_bazooka_mode;
static int s_loaded_id;
static char_state s_player;
static float s_cam_yaw,s_cam_pitch,s_spawn_x=10,s_spawn_y,s_spawn_z=20,s_spawn_yaw=3.14159265f;
static float s_entrance_timer;
static u32 held, down;
static circlePosition stick;
static int portal_active,landed;
static unsigned powers;
game_stats g_stats;
static u32 hidKeysDown(void) { return down; }
static u32 hidKeysHeld(void) { return held; }
static void hidCircleRead(circlePosition *p) { *p=stick; }
int story_has_power(uint32_t p) { return (powers&p)!=0; }
void audio_play_sfx(SfxId s) { (void)s; }
void audio_stop_bgm(void) {}
static void play_level_theme(int id) { (void)id; }
void props_set_vortex(int active,float x,float y,float z,float strength) {
    (void)x;(void)y;(void)z;(void)strength;portal_active=active;
}
void props_spawn_landing_dust(float x,float y,float z) { (void)x;(void)y;(void)z;landed++; }
void props_update(float dt,char_state *p) { (void)dt;(void)p; }
int props_checkpoint_point(float *x,float *y,float *z) { (void)x;(void)y;(void)z;return 0; }
void props_spawn_projectile(float x,float y,float z,float dx,float dy,float dz) {
    (void)x;(void)y;(void)z;(void)dx;(void)dy;(void)dz;
}
int props_check_exit(float px,float py,float pz) { (void)px;(void)py;(void)pz; return 0; }
int props_is_time_trial(void) { return 0; }
uint32_t props_get_time_trial_ms(void) { return 0; }
void story_on_level_cleared(int level_id,int all_crates,uint32_t time_ms) {
    (void)level_id;(void)all_crates;(void)time_ms;
}
int story_save(void) { return 0; }
int level_height_below(float x,float z,float max_y) {
    (void)x;(void)z;return max_y>=0 ? 0 : -1000000;
}
int level_move_horizontal(float *x, float *z, float foot_y,
                          float dx, float dz, float radius,
                          float body_height, float step_height) {
    (void)foot_y; (void)radius; (void)body_height; (void)step_height;
    *x += dx; *z += dz; return 0;
}
'''

TESTS = r'''
static float run(int count,float dt) {
    memset(&s_player,0,sizeof(s_player));s_player.onground=1;
    held=KEY_DUP;down=0;
    for(int i=0;i<count;i++)update_player(dt);
    return -s_player.z;
}
int main(void) {
    float d60=run(60,1.0f/60);
    assert(d60>220 && d60<260);
    assert(s_player.walk>6 && s_player.walk<12); // previously ~62 radians/sec
    float d30=run(30,1.0f/30);
    assert(fabsf(d60-d30)<5);
    memset(&s_player,0,sizeof(s_player));s_player.onground=1;
    held=KEY_DRIGHT;down=0;update_player(1.0f/60);
    assert(s_player.vx>20 && s_player.vx<25); // bounded acceleration, no instant max speed
    run(60,1.0f/60);held=0;
    for(int i=0;i<20;i++)update_player(1.0f/60);
    assert(fabsf(s_player.vz)<0.001f);
    held=0;down=KEY_A;update_player(1.0f/60);down=0;
    assert(s_player.vy>0 && !s_player.onground && s_player.air_time>0);
    for(int i=0;i<90;i++)update_player(1.0f/60);
    assert(s_player.onground && s_player.y==0);

    level_vertex first[CRASH_CORNER_COUNT],later[CRASH_CORNER_COUNT];
    held=down=0;start_entrance();
    assert(s_state==STATE_ENTRANCE && portal_active && s_player.y==70);
    char_build(first,CRASH_CORNER_COUNT,&s_player);
    update_entrance(0.10f);
    char_build(later,CRASH_CORNER_COUNT,&s_player);
    assert(memcmp(first,later,sizeof(first))!=0 && s_player.spin_timer<0.3f);
    for(int i=0;i<90;i++)update_entrance(1.0f/60);
    assert(s_state==STATE_GAMEPLAY && !portal_active && landed>0);
    assert(s_player.y==s_spawn_y && s_player.x==s_spawn_x && s_player.z==s_spawn_z);
    assert(s_player.yaw==s_spawn_yaw);

    char_state pose={0};pose.onground=1;pose.vz=260;pose.walk=1.0f;
    char_build(first,CRASH_CORNER_COUNT,&pose);
    pose.vz=130;char_build(later,CRASH_CORNER_COUNT,&pose);
    assert(memcmp(first,later,sizeof(first))!=0); // original run differs from walk
    pose=(char_state){0};pose.vy=400;pose.air_time=0.05f;
    char_build(first,CRASH_CORNER_COUNT,&pose);
    pose.air_time=0.3f;char_build(later,CRASH_CORNER_COUNT,&pose);
    assert(memcmp(first,later,sizeof(first))!=0); // take-off and upward clips
    puts("PASS motion: acceleration, braking, 30/60 Hz, stride, original run/jump clips, visible entrance progression");
    return 0;
}
'''


def main():
    source = (ROOT / "source/main.c").read_text()
    body = "\n".join(extract(source, name) for name in
                     ("start_entrance", "update_player", "update_entrance"))
    with tempfile.TemporaryDirectory(prefix="crash-motion-") as directory:
        test = Path(directory) / "test.c"
        executable = Path(directory) / "test.exe"
        test.write_text(PRELUDE + body + TESTS)
        subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O2",
                        "-Wall", "-Wextra", "-Werror", "-UNDEBUG", "-I" + str(ROOT / "source"),
                        str(test), "-lm", "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
