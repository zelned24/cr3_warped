#include <assert.h>
#ifdef NDEBUG
#error Entity tests require assertions
#endif
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "audio.h"
void audio_play_sfx(SfxId id) { (void)id; }
int level_height(float x, float z) { (void)x; (void)z; return 0; }
int level_height_below(float x,float z,float max_y){(void)max_y;return level_height(x,z);}
int story_get_canonical_crate_count(int level_id) { (void)level_id; return 42; }
int story_get_chamber(int level_id) { (void)level_id; return 0; }
void story_on_boss_defeated(int boss_idx) { (void)boss_idx; }
#include "story.h"
SaveData g_save;
#include "props.c"
#include "char.c"

int main(void)
{
    /* Every authored entity keeps its ZDAT vertical placement even when a
     * lower floor exists. This catches flattened crate stacks and fruit. */
    props_init_level(11, 3034, 189, 25450);
    const level_entity_table_entry *authored=level_entities_find(11);
    assert(authored && authored->count>0);
    int instance=0, checked=0;
    for(int i=0;i<authored->count;i++) {
        const level_entity_def *ent=&authored->entities[i];
        if(ent->type==PROP_TIME_CLOCK)continue;
        assert(instance<props_count());
        const prop_item *item=props_get(instance++);
        float lift=ent->type==PROP_WUMPA ? 14.0f : ent->type==PROP_CRYSTAL ? 18.0f : 0.0f;
        assert(item->type==ent->type);
        assert(item->x==ent->x && item->z==ent->z);
        assert(fabsf(item->y-ent->y-lift)<0.001f);
        checked++;
    }
    assert(checked>42);
    level_vertex fallback[8190];
    assert(props_build_untextured_mesh(fallback,8190)>0);
    int all=props_build_mesh(fallback,8190);
    assert(props_build_missing_mesh(fallback,8190,0)==all);
    for(int i=0;i<s_prop_count;i++)
        if(props_textured_asset_index(s_props[i].type)<0)s_props[i].active=0;
    assert(props_build_untextured_mesh(fallback,8190)==0);
    props_init_level_facing(0,0,0,0,3.14159265f);
    assert(props_count()>0 && props_get(0)->z<0);
    props_init_level(0, 0, 0, 0);
    assert(props_count()>0 && props_get(0)!=NULL && props_get(-1)==NULL);
    assert(props_get(props_count())==NULL);
    level_vertex effects[8190];
    assert(props_build_effects_mesh(effects,8190)==0);
    props_set_vortex(1,0,60,0,1);
    assert(props_build_effects_mesh(effects,8190)==96);
    props_set_vortex(0,0,0,0,0);
    prop_item *tnt = NULL;
    for (int i = 0; i < s_prop_count; i++)
        if (s_props[i].type == PROP_CRATE_TNT) tnt = &s_props[i];
    assert(tnt);
    char_state player = {0};
    player.x = 5000;
    player.z = 5000;
    for (int i = 0; i < 8000; i++) props_update(1.0f / 60.0f, &player);
    assert(tnt->active && !tnt->armed && tnt->timer == 0);
    player.x = tnt->x;
    player.z = tnt->z;
    player.y = tnt->y + 24;
    player.vy = -1;
    props_update(0.01f, &player);
    assert(tnt->armed && tnt->active && player.vy > 0);
    player.x = player.z = 5000;
    props_update(2.99f, &player);
    assert(tnt->active);
    props_update(0.02f, &player);
    assert(!tnt->active && g_stats.crates_broken == 1);

    float cx=0,cy=0,cz=0;
    assert(!props_checkpoint_point(&cx,&cy,&cz));
    prop_item *checkpoint=NULL;
    for(int i=0;i<s_prop_count;i++)
        if(s_props[i].type==PROP_CRATE_CHECKPOINT)checkpoint=&s_props[i];
    assert(checkpoint);
    break_crate(checkpoint);
    assert(props_checkpoint_point(&cx,&cy,&cz));
    assert(cx==checkpoint->x && cy==checkpoint->y && cz==checkpoint->z);
    assert(!props_checkpoint_point(NULL,&cy,&cz));

    /* A draw count must contain whole triangles and stay inside the cap. */
    level_vertex mesh[128];
    for (int cap = 0; cap < 100; cap++) {
        memset(mesh, 0x5a, sizeof(mesh));
        level_vertex tail[128]; memcpy(tail, mesh, sizeof(mesh));
        int n = props_build_mesh(mesh, cap);
        assert(n >= 0 && n <= cap && n % 3 == 0);
        assert(memcmp(mesh + cap, tail + cap, (128 - cap) * sizeof(*mesh)) == 0);
    }
    assert(props_build_mesh(NULL, 100) == 0);
    int n = 0;
    add_cube(mesh, &n, 128, 0, 0, 0, 2, 2, 2, 1, 1, 1);
    assert(n == 36);
    for (int face = 0; face < 6; face++) {
        float normals[2][3];
        for (int t = 0; t < 2; t++) {
            level_vertex *v = mesh + face * 6 + t * 3;
            float ax = v[1].x - v[0].x, ay = v[1].y - v[0].y, az = v[1].z - v[0].z;
            float bx = v[2].x - v[0].x, by = v[2].y - v[0].y, bz = v[2].z - v[0].z;
            normals[t][0] = ay * bz - az * by;
            normals[t][1] = az * bx - ax * bz;
            normals[t][2] = ax * by - ay * bx;
        }
        float dot = 0;
        for (int k = 0; k < 3; k++) dot += normals[0][k] * normals[1][k];
        assert(dot > 0); /* no overlapping/opposed triangles in a bow-tie face */
    }
    level_vertex regular[CRASH_CORNER_COUNT], extended[CRASH_CORNER_COUNT];
    char_state animation = {0};
    animation.spin_duration = 0.35f; animation.spin_timer = 0.175f;
    assert(char_build(regular, CRASH_CORNER_COUNT, &animation) == CRASH_CORNER_COUNT);
    animation.spin_duration = 0.65f; animation.spin_timer = 0.325f;
    assert(char_build(extended, CRASH_CORNER_COUNT, &animation) == CRASH_CORNER_COUNT);
    for (int i = 0; i < CRASH_CORNER_COUNT; i++) {
        assert(fabsf(regular[i].x - extended[i].x) < 0.0001f);
        assert(fabsf(regular[i].y - extended[i].y) < 0.0001f);
        assert(fabsf(regular[i].z - extended[i].z) < 0.0001f);
    }
    assert(char_build(regular, CRASH_CORNER_COUNT - 1, &animation) == 0);
    /* Top contact is relative to the crate, independent of world elevation.
     * Previously dy was compared to the absolute top, breaking real levels. */
    const PropType jump_types[]={PROP_CRATE_WOOD,PROP_CRATE_TNT,PROP_CRATE_BOUNCE,PROP_CRATE_SPRING};
    const float elevations[]={0,200,2500,-600};
    for(unsigned k=0;k<4;k++)for(unsigned h=0;h<4;h++) {
        memset(s_props,0,sizeof(s_props));s_prop_count=1;
        s_particles[0].life=0;s_particle_count=0;
        s_props[0]=(prop_item){.type=jump_types[k],.x=10,.y=elevations[h],.z=10,
            .sx=24,.sy=24,.sz=24,.active=1,.value=5};
        player=(char_state){.x=10,.y=elevations[h]+24,.z=10,.vy=-100};
        g_stats.aku_timer=0;
        props_update(1.0f/60,&player);
        assert(player.y==elevations[h]+24);
        assert(player.vy==(jump_types[k]==PROP_CRATE_SPRING?660:420));
        assert(!player.onground);
        if(jump_types[k]==PROP_CRATE_TNT)assert(s_props[0].armed);
        if(jump_types[k]==PROP_CRATE_BOUNCE)assert(s_props[0].active && s_props[0].value==4);
    }
    /* Protection has a read-only visual companion, outside placed entities.
     * Source dimensions are preserved; changing view yaw rotates its offset. */
    prop_item mask;
    player=(char_state){.x=100,.y=200,.z=300,.anim_time=0.0f};
    int count_before=s_prop_count;
    g_stats.aku_aku_tier=0;
    assert(!props_aku_visual(&player,0,&mask));
    for(int tier=1;tier<=3;tier++) {
        g_stats.aku_aku_tier=tier;
        assert(props_aku_visual(&player,0,&mask));
        assert(mask.active && mask.type==PROP_AKU_MASK && mask.x==70 && mask.y==230 && mask.z==300);
        assert(mask.sx==1 && mask.sy==1 && mask.sz==1);
        assert(props_textured_asset_index(mask.type)==7 && props_textured_asset_index(PROP_CRATE_AKU)==-1);
        assert(props_aku_visual(&player,1.57079633f,&mask));
        assert(fabsf(mask.x-100)<0.001f && fabsf(mask.z-330)<0.001f);
    }
    player.x+=700;player.y+=50;player.anim_time=0.3f;
    assert(props_aku_visual(&player,0,&mask));
    assert(mask.x==770 && mask.y>280 && mask.y<282);
    assert(s_prop_count==count_before);
    g_stats.aku_aku_tier=0;
    assert(!props_aku_visual(&player,0,&mask));
    assert(!props_aku_visual(NULL,0,&mask) && !props_aku_visual(&player,0,NULL));
    puts("PASS entities: authored heights, crate jumps, TNT, bounded meshes, cube topology, spin and mask visibility");
    return 0;
}
