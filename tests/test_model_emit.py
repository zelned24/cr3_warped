"""Production native emission, checked against explicit PS1 GP0 byte fixtures.

Uses a mapped retained TGEO fixture and a stopped command/projection handoff.
The original game scheduler and visual parity are outside this regression.
"""
from pathlib import Path
import subprocess
import sys
import tempfile
from host_native_sources import all_sources

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "model_emit.h"
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);exit(1); } } while(0)
#define GEO 0x81000000u
#define SCRATCH 0x1f800000u
#define TABLE 0x80010000u
#define PRIM 0x80020000u
static gool_domain domain;
static model_resources bank;
static model_actor actor;
static model_triangle triangle;
static model_commands commands;
static model_command_projection projection;
static model_emit emit;
static uint32_t geo[64],scratch[640],table[2064],arena[32];
static gool_domain_lease leases[4];
static const uint32_t xy[3]={0x00200010,0x00400030,0x00600050};
static const uint32_t rgb[3]={0x00604020,0x00604020,0x00604020};
static void setup(unsigned mode,unsigned selector) {
    memset(&bank,0,sizeof(bank));memset(&actor,0,sizeof(actor));memset(&commands,0,sizeof(commands));
    memset(&projection,0,sizeof(projection));memset(&triangle,0,sizeof(triangle));
    memset(geo,0,sizeof(geo));memset(scratch,0x55,sizeof(scratch));
    for(unsigned i=0;i<2064;i++)table[i]=0xffffff;
    for(unsigned i=0;i<32;i++)arena[i]=0xdeadbeef;
    CHECK(!gool_domain_map_mutable(&domain,GEO,geo,sizeof(geo),&leases[0]));
    CHECK(!gool_domain_map_mutable(&domain,SCRATCH,scratch,sizeof(scratch),&leases[1]));
    CHECK(!gool_domain_map_mutable(&domain,TABLE,table,sizeof(table),&leases[2]));
    CHECK(!gool_domain_map_mutable(&domain,PRIM,arena,sizeof(arena),&leases[3]));
    geo[2]=2;geo[3]=4;geo[7]=GEO+128;geo[8]=GEO+140;
    geo[32]=0x12342010;geo[33]=0x00802211;geo[34]=0x40302412;geo[40]=0x111;
    bank.domain=&domain;bank.open=1;
    bank.slots[0]=(zdat_resource_slot){.address=GEO,.size=sizeof(geo),.generation=7,
        .references=1,.mapped=1,.state=ZDAT_RESOURCE_READY};
    triangle.flags=selector;
    for(unsigned i=0;i<3;i++)triangle.corners[i].attribute=rgb[i];
    actor.opened=1;actor.bank=&bank;actor.render_word=mode<<16;
    actor.topology=(model_triangles){.bank=&bank,.lease={.generation=7,.slot=0},
        .entry=GEO,.output=&triangle,.count=1,.opened=1,.retained=1,
        .uvs=GEO+128,.uv_count=1,.texture_names=GEO+160,.texture_count=1};
    commands=(model_commands){.actor=&actor,.triangle=triangle,.scratch=SCRATCH,.generation=1,
        .opened=1,.status=MODEL_COMMANDS_TRIANGLE};
    projection.owner=&commands;projection.pending=1;projection.generation=1;
    projection.visibility.admitted=projection.visibility.depth_written=1;
    projection.visibility.depth_mac=0x20000;
    for(unsigned i=0;i<3;i++) {
        projection.fifo.x[i]=(int16_t)xy[i];projection.fifo.y[i]=(int16_t)(xy[i]>>16);
        projection.fifo.depth[i+1]=0x1401;
    }
}
static void teardown(void) {
    CHECK(!model_emit_close(&emit));CHECK(!commands.handoffs);
    for(unsigned i=0;i<4;i++)CHECK(!gool_domain_release(&domain,leases[i]));
    CHECK(gool_domain_empty(&domain));
}
static int begin(unsigned flags,const uint32_t *far) {
    return model_emit_begin(&emit,&commands,&projection,PRIM,PRIM+sizeof(arena),
        TABLE,TABLE+sizeof(table),100,flags,0,far);
}
static void finish(void) {
    CHECK(commands.handoffs==1);
    CHECK(model_commands_consume(&commands)==MODEL_COMMANDS_INVALID);
    CHECK(model_commands_close(&commands)==MODEL_COMMANDS_INVALID);
    CHECK(model_emit_step(&emit,0)==MODEL_EMIT_PENDING&&emit.phase==0);
    CHECK(model_emit_step(&emit,1)==MODEL_EMIT_PENDING&&emit.phase==1);
    CHECK(scratch[0x120/4]==0x20000);
    int status=1;for(unsigned i=0;i<4&&status==1;i++)status=model_emit_step(&emit,1);
    CHECK(status==0);
    uint32_t saved[32];memcpy(saved,arena,sizeof(arena));
    CHECK(model_emit_step(&emit,20)==0&&!memcmp(saved,arena,sizeof(arena)));
}
static void golden_rgb(void) {
    setup(0,1);CHECK(begin(0,NULL)==1);finish();
    const uint32_t expected[]={0x09ffffff,0x36604020,0x00200010,0x12342010,
        0x00604020,0x00400030,0x00802211,0x00604020,0x00600050,0x40302412};
    CHECK(!memcmp(arena,expected,sizeof(expected))&&emit.next==PRIM+40);
    CHECK(table[(99*4+0x20)/4]==(PRIM&0xffffff));teardown();
    setup(0,1);geo[33]=0x00e02211;CHECK(begin(0,NULL)==1);finish();
    CHECK(arena[1]==0x34604020);teardown();
    setup(5,1);CHECK(begin(0,NULL)==1);finish();
    const uint32_t constant[]={0x04ffffff,0x20204088,0x00200010,0x00400030,0x00600050};
    CHECK(!memcmp(arena,constant,sizeof(constant))&&emit.next==PRIM+20&&table[8]==(PRIM&0xffffff));teardown();
}
static void mode1_boundaries(void) {
    const uint32_t far[3]={0,0,0};
    setup(1,1);projection.fifo.depth[1]=0xc00;projection.fifo.depth[2]=0x1000;projection.fifo.depth[3]=0x1400;
    CHECK(begin(0,far)==1);finish();
    const uint32_t expected[]={0x09ffffff,0x36000000,0x00200010,0x12342010,
        0x00302010,0x00400030,0x00802211,0x00604020,0x00600050,0x40302412};
    CHECK(!memcmp(arena,expected,sizeof(expected)));
    CHECK(scratch[0x124/4]==0&&scratch[0x128/4]==0x302010&&scratch[0x12c/4]==0x604020);teardown();
    setup(1,1);projection.fifo.depth[1]=0xc01;projection.fifo.depth[2]=0xc00;projection.fifo.depth[3]=1;
    CHECK(begin(0,far)==1);finish();CHECK(emit.skipped&&emit.next==PRIM&&arena[0]==0xdeadbeef);
    CHECK(!scratch[0x124/4]&&!scratch[0x128/4]&&!scratch[0x12c/4]);teardown();
    setup(1,1);commands.triangle.corners[0].attribute=0xab604020;
    commands.triangle.corners[1].attribute=0xcd604020;commands.triangle.corners[2].attribute=0xef604020;
    CHECK(begin(0,NULL)==1);finish();
    CHECK(arena[1]==0x36604020&&arena[4]==0xcd604020&&arena[7]==0xef604020);
    CHECK(scratch[0x124/4]==0xab604020&&scratch[0x128/4]==0xcd604020);teardown();
}
static void mode2_and7(void) {
    setup(2,0);CHECK(begin(0,NULL)==1);finish();
    const uint32_t state[]={0x06ffffff,0x32604020,0x00200010,0x00604020,0x00400030,
        0x00604020,0x00600050,0x02020000,0xe1000a20,0};
    CHECK(!memcmp(arena,state,sizeof(state))&&emit.next==PRIM+40);
    CHECK(table[(99*4+0x20)/4]==0x2001c);teardown();
    setup(2,1);geo[33]=0x00e02211;CHECK(begin(0,NULL)==1);finish();
    CHECK(arena[1]==0x36604020&&arena[6]==0x00a02211);teardown();
    const uint32_t far[3]={0,0,0};
    setup(7,0);CHECK(begin(2048,far)==1);finish();
    const uint32_t combined[]={0x0effffff,0xe1000a40,0,0x22404040,0x00200010,0x00400030,
        0x00600050,0xe1000a20,0,0x32302010,0x00200010,0x00302010,0x00400030,0x00302010,0x00600050};
    CHECK(!memcmp(arena,combined,sizeof(combined))&&emit.next==PRIM+60);teardown();
    setup(7,1);CHECK(begin(2048,far)==1);finish();
    CHECK(arena[0]==0x0fffffff&&arena[7]==0x36302010&&arena[12]==0x00a02211&&emit.next==PRIM+64);teardown();
    setup(7,0);CHECK(begin(0,NULL)==1);finish();CHECK(arena[1]==0x30604020&&emit.next==PRIM+28);teardown();
}
static void mode6(void) {
    setup(6,1);CHECK(begin(0,NULL)==MODEL_EMIT_INVALID);
    CHECK(model_emit_begin_depth_pair(&emit,&commands,&projection,PRIM,PRIM+sizeof(arena),TABLE,
        TABLE+sizeof(table),12,32,0,0,NULL)==1);finish();
    CHECK(scratch[0xf0/4]==12&&table[(11*4+0x20)/4]==0x20000);teardown();
    setup(6,1);commands.triangle.flags|=0x02000000;
    CHECK(model_emit_begin_depth_pair(&emit,&commands,&projection,PRIM,PRIM+sizeof(arena),TABLE,
        TABLE+sizeof(table),12,32,0,0,NULL)==1);finish();
    CHECK(scratch[0xf0/4]==32&&table[(31*4+0x20)/4]==0x20000);teardown();
}
static void normals(void) {
    for(unsigned mode=3;mode<=4;mode++)for(unsigned back=0;back<=1;back++) {
        setup(mode,1);gool_native_normal_primitive result;
        CHECK(!gool_native_primitive_normal(&domain,PRIM,PRIM+sizeof(arena),mode,0,back,
            GEO+140,xy,0x604020,&result));
        CHECK(!result.skipped&&result.size.bytes==32&&result.size.packet_words==7);
        CHECK(arena[2]==xy[0]&&arena[4]==xy[1]&&arena[6]==xy[2]);
        CHECK(arena[1]==((mode==3&&back)?0x24604020:0x26604020));
        CHECK(arena[5]==((mode==3&&back)?0x00e02211:back?0x00c02211:0x00a02211));teardown();
    }
    setup(4,1);geo[33]=0x00a02211;gool_native_normal_primitive skipped;
    CHECK(!gool_native_primitive_normal(&domain,PRIM,PRIM+sizeof(arena),4,0x10000000,1,
        GEO+140,xy,0x604020,&skipped));
    CHECK(skipped.skipped&&arena[1]==0xdeadbeef&&arena[2]==xy[0]&&arena[4]==xy[1]&&arena[6]==xy[2]);teardown();
}
static void live_vertex_cache(void) {
    setup(0,0);
    uint64_t tick=9;uint32_t native_tick=9;gool_domain_lease tick_lease;
    CHECK(!gool_domain_map_mutable(&domain,0x80068eec,&native_tick,4,&tick_lease));
    gool_native_actor native={.attached=1,.tick_source=&tick};
    model_pose_vertex pose[]={{0x00100001,0x11},{0x00200002,0x22},
                              {0x00300003,0x33},{0x00400004,0x44}};
    model_triangle chain[3]={0};
    chain[0].flags=0x80000000;chain[1].flags=0x00010000;chain[2].flags=0x40010000;
    for(unsigned corner=0;corner<3;corner++) {
        chain[0].corners[corner]=(model_triangle_corner){.vertex=corner,
            .vertex_command=4+corner*4,.attribute_stored=1,.attribute_index=0};
    }
    chain[0].corners[2].vertex_flags=0x04000000; /* LOAD_VERTEX slot0 */
    chain[1].corners[2]=(model_triangle_corner){.vertex=3,.vertex_command=16,
        .attribute_stored=1,.attribute_index=0};
    chain[2].corners[2]=(model_triangle_corner){.vertex=0,.vertex_command=20,
        .vertex_flags=0x04000000,.attribute_stored=1,.attribute_index=0};
    geo[12]=0;geo[13]=chain[0].flags;geo[14]=0;geo[15]=0;
    geo[16]=chain[1].flags;geo[17]=chain[2].flags;geo[18]=0xffffffff;
    actor.actor=&native;actor.tick=tick;actor.retained=1;
    actor.topology.output=chain;actor.topology.count=3;actor.topology.vertex_count=4;
    actor.topology.commands=GEO+48;actor.topology.command_bytes=28;
    actor.pose=(model_pose){.bank=&bank,.geometry=GEO,.geometry_identity={.generation=7,.slot=0},
        .output=pose,.opened=1,.count=4,.decoded=4};
    commands=(model_commands){0};
    CHECK(model_commands_begin(&commands,&actor,SCRATCH)==MODEL_COMMANDS_PENDING);
    CHECK(model_commands_step(&commands,1)==MODEL_COMMANDS_TRIANGLE);
    CHECK(commands.vertices[0].xy==0x00100001&&commands.vertices[1].xy==0x00200002);
    CHECK(commands.vertices[2].xy==0x00200002&&commands.vertices[2].z==0x22);
    CHECK(commands.triangle.corners[0].attribute==0x00100001);
    CHECK(commands.triangle.corners[1].attribute==0x00200002&&commands.triangle.corners[2].attribute==0x00200002);
    /* All three commands alias slot0, yet earlier resolved operands survive. */
    CHECK(scratch[0x140/4]==0x00200002);
    CHECK(model_commands_consume(&commands)==1&&model_commands_step(&commands,1)==2);
    CHECK(commands.vertices[0].xy==0x00200002&&commands.vertices[1].xy==0x00200002);
    CHECK(commands.vertices[2].xy==0x00400004&&commands.vertices[2].z==0x44);
    CHECK(commands.triangle.corners[0].attribute==0x00200002&&commands.triangle.corners[2].attribute==0x00400004);
    CHECK(model_commands_consume(&commands)==1&&model_commands_step(&commands,1)==2);
    CHECK(commands.vertices[0].xy==0x00200002&&commands.vertices[1].xy==0x00400004);
    CHECK(commands.vertices[2].xy==0x00400004&&commands.vertices[2].z==0x44);
    CHECK(commands.triangle.corners[0].attribute==0x00200002&&commands.triangle.corners[1].attribute==0x00400004);
    CHECK(model_commands_consume(&commands)==1&&model_commands_step(&commands,1)==0);
    CHECK(!model_commands_close(&commands)&&!actor.consumers);
    CHECK(!gool_domain_release(&domain,tick_lease));teardown();
}
int main(void) {
    golden_rgb();mode1_boundaries();mode2_and7();mode6();normals();live_vertex_cache();
    puts("PASS native emission: GP0 golden words, modes0..7, mode1 depth boundaries, scratch effects, handoff ownership, live cache aliasing, LOAD_VERTEX and strip carry");
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="crash-model-emit-") as temporary:
        folder = Path(temporary)
        (folder / "probe.c").write_text(HARNESS)
        executable = folder / "probe.exe"
        names = ("model_pose.c", "model_vertices.c", "model_triangles.c", "model_project.c",
                 "model_commands.c", "model_visibility.c", "model_emit.c")
        sources = [str(ROOT / "source" / name) for name in names]
        subprocess.run([sys.executable, "-m", "ziglang", "cc", "-O2", "-UNDEBUG",
                        "-I" + str(ROOT / "source"), str(folder / "probe.c"), *sources,
                        *all_sources(), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
