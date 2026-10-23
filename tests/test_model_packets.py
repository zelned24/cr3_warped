"""Run production DMA decoding and pass planning against explicit GP0 fixtures.

Only citro3d declarations are substituted; no renderer algorithm is copied.
The packet fixtures encode the public PS1 GP0 format, including compound
mode-7 packets, zero-word OT nodes and malformed DMA chains.
"""
from pathlib import Path
import importlib.util
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SDK = r'''
#ifndef TEST_CITRO3D_H
#define TEST_CITRO3D_H
typedef int GPU_CULLMODE, GPU_TESTFUNC, GPU_WRITEMASK, GPU_BLENDEQUATION, GPU_BLENDFACTOR;
enum { GPU_CULL_NONE, GPU_ALWAYS, GPU_WRITE_COLOR, GPU_BLEND_ADD,
       GPU_ONE, GPU_ZERO, GPU_EQUAL, GPU_GREATER, GPU_CONSTANT_COLOR,
       GPU_BLEND_REVERSE_SUBTRACT };
typedef struct { float values[16]; } C3D_Mtx;
typedef struct { int unused; } C3D_Tex, DVLB_s, shaderProgram_s;
#endif
'''
HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "model_draw.h"
#undef assert
#define assert(condition) do { if(!(condition)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#condition); exit(1); } } while(0)
static gool_domain domain;
static uint32_t ot[8], arena[128];
static model_packet_span spans[2];
static model_packets owner;
static void setup(void) {
    memset(ot,0,sizeof(ot));memset(arena,0,sizeof(arena));
    spans[0]=(model_packet_span){.address=0x80001000,.bytes=sizeof(ot)};
    spans[1]=(model_packet_span){.address=0x80002000,.bytes=sizeof(arena)};
    assert(!gool_domain_map_mutable(&domain,spans[0].address,ot,sizeof(ot),&spans[0].lease));
    assert(!gool_domain_map_mutable(&domain,spans[1].address,arena,sizeof(arena),&spans[1].lease));
}
static void teardown(void) {
    assert(!model_packets_close(&owner));
    for(unsigned i=0;i<2;i++) {
        uint32_t refs=0;assert(!gool_domain_references(&domain,spans[i].lease,&refs)&&refs==1);
        assert(!gool_domain_release(&domain,spans[i].lease));
    }
    assert(gool_domain_empty(&domain));
}
static int finish(void) {
    int status=MODEL_PACKETS_PENDING;
    for(unsigned i=0;i<4096&&status==MODEL_PACKETS_PENDING;i++)status=model_packets_step(&owner,1);
    return status;
}
static void fixture(unsigned opcode) {
    /* RGB bytes and screen words deliberately differ at each corner. */
    unsigned textured=(opcode>>2)&1, gouraud=(opcode>>4)&1, n=1;
    arena[n++]=(opcode<<24)|0x030201;
    for(unsigned i=0;i<3;i++) {
        if(i&&gouraud)arena[n++]=0x030201+i*0x030303;
        arena[n++]=0xfff00010+i*0x00010001;
        if(textured)arena[n++]=(i==0?0x12340000:i==1?0x08a00000:0x40300000)|(0x2010+i*0x0201);
    }
    arena[0]=((n-1)<<24)|0xffffff;
}
static void decode_families(void) {
    const unsigned opcodes[]={0x20,0x22,0x24,0x26,0x27,0x30,0x32,0x34,0x36,0x37};
    for(unsigned k=0;k<sizeof(opcodes)/sizeof(*opcodes);k++) {
        setup();fixture(opcodes[k]);
        ot[0]=0x1004;ot[1]=0x2000;
        assert(model_packets_begin(&owner,&domain,spans,0x1000)==MODEL_PACKETS_PENDING);
        assert(model_packets_step(&owner,0)==MODEL_PACKETS_PENDING&&owner.nodes==0);
        assert(finish()==MODEL_PACKETS_PACKET);
        model_packet_command c;
        assert(!model_packets_decode(&owner,0,&c));
        assert(c.opcode==opcodes[k]&&c.kind==MODEL_PACKET_TRIANGLE);
        assert(c.textured==((opcodes[k]>>2)&1)&&c.gouraud==((opcodes[k]>>4)&1));
        assert(c.semi_transparent==((opcodes[k]>>1)&1)&&c.raw_texture==(opcodes[k]&1));
        assert(c.next_word==owner.count);
        for(unsigned i=0;i<3;i++) {
            assert(c.sxy[i]==0xfff00010+i*0x00010001);
            assert(c.colors[i]==0x030201+(c.gouraud?i*0x030303:0));
        }
        model_packet_command saved=c;
        assert(model_packets_step(&owner,100)==MODEL_PACKETS_PACKET);
        assert(!model_packets_decode(&owner,0,&c)&&!memcmp(&c,&saved,sizeof(c)));
        assert(model_packets_consume(&owner)==MODEL_PACKETS_PENDING);
        assert(finish()==MODEL_PACKETS_DONE);
        teardown();
    }
}
static void compound_and_plan(void) {
    setup();
    uint32_t payload[]={0xe1000a40,0,0x22080808,1,2,3,
        0xe1000a20,0,0x32030201,4,0x060504,5,0x090807,6};
    arena[0]=0x0effffff;memcpy(arena+1,payload,sizeof(payload));
    assert(model_packets_begin(&owner,&domain,spans,0x2000)==1&&finish()==2);
    unsigned word=0,triangles=0,state_changes=0,padding=0;uint32_t state=0;
    while(word<owner.count) {
        model_packet_command c;model_draw_packet_passes p;
        assert(!model_packets_decode(&owner,word,&c));
        assert(!model_draw_packet_plan(&c,state,&p));state=p.draw_mode;
        if(c.kind==MODEL_PACKET_TRIANGLE){triangles++;assert(p.count==1);}
        else if(c.kind==MODEL_PACKET_DRAW_MODE){state_changes++;assert(!p.count);}
        else padding++;
        word=c.next_word;
    }
    assert(triangles==2&&state_changes==2&&padding==2&&state==0xa20);
    teardown();
    /* Native opaque texels and STP texels must never both blend. */
    for(unsigned blend=0;blend<4;blend++) {
        model_packet_command c={.kind=MODEL_PACKET_TRIANGLE,.textured=1,.semi_transparent=1};
        c.uv[1]=(blend<<5|0x100)<<16;
        model_draw_packet_passes p;assert(!model_draw_packet_plan(&c,0x3600,&p));
        assert(p.count==2&&p.states[0].alpha_reference==255&&p.states[1].alpha_reference==128);
        assert(p.states[0].source_color==GPU_ONE&&p.states[0].destination_color==GPU_ZERO);
        assert(p.states[0].alpha_test==GPU_EQUAL&&p.states[1].alpha_test==GPU_EQUAL);
        assert(!p.states[0].depth_enabled&&!p.states[1].depth_enabled&&p.states[0].rgb_scale==2);
        if(blend==0)assert(p.states[1].blend_constant==0x80808080);
        if(blend==1)assert(p.states[1].source_color==GPU_ONE&&p.states[1].destination_color==GPU_ONE);
        if(blend==2)assert(p.states[1].color_equation==GPU_BLEND_REVERSE_SUBTRACT);
        if(blend==3)assert(p.states[1].blend_constant==0x40404040);
        c.raw_texture=1;assert(!model_draw_packet_plan(&c,0,&p)&&p.states[0].rgb_scale==1);
        c.semi_transparent=0;assert(!model_draw_packet_plan(&c,0,&p)&&p.count==1&&p.states[0].alpha_test==GPU_GREATER);
    }
}
static void provenance(void) {
    model_packet_command c={.kind=MODEL_PACKET_TRIANGLE,.textured=1,
        .uv={0x12342010,0x08a02211,0x40302412}};
    const model_packet_binding bindings[]={{0x80002000,0,0x111},{0x80002000,9,0x333},{0x80002040,0,0x555}};
    model_material_uv m;
    assert(!model_packets_bound_material(&c,0x80002000,9,bindings,3,&m));
    assert(m.texture_eid==0x333&&m.clut==0x1234&&m.texpage==0x08a0&&m.depth==8&&m.blend==1);
    assert(m.u[3]==0x30&&m.v[3]==0x40&&m.min_u==0x10&&m.max_v==0x40);
    model_material_uv sentinel=m;
    assert(model_packets_bound_material(&c,0x80002000,8,bindings,3,&m)<0&&!memcmp(&m,&sentinel,sizeof(m)));
}
static void rejection(void) {
    setup();ot[0]=0x1000;
    assert(model_packets_begin(&owner,&domain,spans,0x1000)==1);
    assert(finish()==MODEL_PACKETS_CYCLE);
    assert(model_packets_step(&owner,1)==MODEL_PACKETS_CYCLE);teardown();
    setup();arena[127]=0x01ffffff;
    assert(model_packets_begin(&owner,&domain,spans,0x21fc)==1&&finish()==MODEL_PACKETS_INVALID);teardown();
    setup();ot[0]=0x2001;
    assert(model_packets_begin(&owner,&domain,spans,0x1000)==1&&finish()==MODEL_PACKETS_INVALID);teardown();
    setup();fixture(0x34);arena[0]=0x08ffffff;
    assert(model_packets_begin(&owner,&domain,spans,0x2000)==1&&finish()==2);
    model_packet_command c;memset(&c,0x55,sizeof(c));model_packet_command saved=c;
    assert(model_packets_decode(&owner,0,&c)==MODEL_PACKETS_INVALID&&!memcmp(&c,&saved,sizeof(c)));teardown();
    setup();model_packet_span aliases[2]={spans[0],{.address=0x81001000,.bytes=sizeof(arena)}};
    assert(!gool_domain_map_mutable(&domain,aliases[1].address,arena,sizeof(arena),&aliases[1].lease));
    assert(model_packets_begin(&owner,&domain,aliases,0x1000)==MODEL_PACKETS_INVALID);
    assert(!gool_domain_release(&domain,aliases[1].lease));teardown();
    setup();fixture(0x34);arena[1]=0x48000000;
    assert(model_packets_begin(&owner,&domain,spans,0x2000)==1&&finish()==2);
    memset(&c,0x55,sizeof(c));saved=c;
    assert(model_packets_decode(&owner,0,&c)==MODEL_PACKETS_INVALID&&!memcmp(&c,&saved,sizeof(c)));teardown();
}
int main(void) {
    decode_families();compound_and_plan();provenance();rejection();
    puts("PASS production DMA: GP0 families, compound mode7, STP passes, provenance, cycles, bounds and retained spans");
}
'''


def main():
    zig = importlib.util.find_spec("ziglang")
    if zig is None or zig.origin is None:
        raise SystemExit("ziglang is required")
    compiler = Path(zig.origin).parent / ("zig.exe" if os.name == "nt" else "zig")
    with tempfile.TemporaryDirectory(prefix="crash-packets-") as folder:
        directory = Path(folder)
        (directory / "citro3d.h").write_text(SDK)
        (directory / "test.c").write_text(HARNESS)
        executable = directory / "test.exe"
        subprocess.run([str(compiler), "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                        "-I", str(directory), "-I", str(ROOT / "source"), str(directory / "test.c"),
                        str(ROOT / "source/model_packets.c"), str(ROOT / "source/model_packet_plan.c"),
                        str(ROOT / "source/gool_domain.c"), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
