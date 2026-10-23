"""Exercise the production presentation cursor with deterministic queue pressure.

Production DMA traversal, GP0 decoding, pass planning and presentation are
linked unchanged. Explicit draw/cache boundary doubles isolate backpressure;
this regression does not certify GPU submission, texture residency or timing.
"""
from pathlib import Path
import importlib.util
import os
import subprocess
import tempfile
from test_model_packets import SDK

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "model_present.h"
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
static gool_domain domain;
static model_draw draw;
static model_texture_cache cache;
static model_present owner;
static model_packet_span spans[2];
static uint32_t ot[4], arena[128];
static model_packet_binding bindings[2];
static unsigned recorded, capacity_after, requests, polls, evictions;
static uint8_t alphas[8];
static int request_result;
/* Queue/cache boundaries deliberately inject pressure without duplicating
 * packet decoding or presentation algorithms. */
int model_draw_record_packet_triangle(model_draw *d,const model_packet_command *c,
    const model_material_uv *m,const C3D_Mtx *v,float depth,const model_draw_state *s,
    model_texture_cache *tc,model_texture_handle handle) {
    (void)m;(void)v;(void)depth;(void)tc;(void)handle;
    CHECK(d==&draw&&c->kind==MODEL_PACKET_TRIANGLE);
    if(recorded==capacity_after)return MODEL_DRAW_CAPACITY;
    CHECK(recorded<8);alphas[recorded++]=s->alpha_reference;return 0;
}
int model_texture_cache_request(model_texture_cache *tc,const model_material_uv *m,model_texture_handle *h) {
    CHECK(tc==&cache&&m->texture_eid==0x111);requests++;
    if(!request_result)*h=(model_texture_handle){.generation=1,.slot=0};return request_result;
}
int model_texture_cache_poll(model_texture_cache *tc,uint32_t budget) {
    CHECK(tc==&cache&&budget==17);polls++;return 1;
}
int model_texture_cache_evict(model_texture_cache *tc,model_texture_handle h) {
    CHECK(tc==&cache&&h.generation);evictions++;return 0;
}
static model_present_controls setup(unsigned semi,unsigned textured,unsigned prior) {
    memset(&draw,0,sizeof(draw));memset(&cache,0,sizeof(cache));
    memset(ot,0,sizeof(ot));memset(arena,0,sizeof(arena));
    draw.initialized=draw.recording=cache.opened=1;
    recorded=requests=polls=evictions=0;capacity_after=8;request_result=0;
    spans[0]=(model_packet_span){.address=0x80001000,.bytes=sizeof(ot)};
    spans[1]=(model_packet_span){.address=0x80002000,.bytes=sizeof(arena)};
    CHECK(!gool_domain_map_mutable(&domain,spans[0].address,ot,sizeof(ot),&spans[0].lease));
    CHECK(!gool_domain_map_mutable(&domain,spans[1].address,arena,sizeof(arena),&spans[1].lease));
    ot[0]=0x2000;
    if(textured) {
        uint32_t payload[]={((0x34|semi*2)<<24)|0x30201,0x200010,0x12342010,
                           0x60504,0x400030,0x08a02211,0x90807,0x600050,0x40302412};
        arena[0]=0x09ffffff;memcpy(arena+1,payload,sizeof(payload));
    } else {
        uint32_t payload[]={0x30030201,0x200010,0x60504,0x400030,0x90807,0x600050};
        arena[0]=0x06ffffff;memcpy(arena+1,payload,sizeof(payload));
    }
    bindings[0]=(model_packet_binding){0x80002000,0,0x111};
    return (model_present_controls){.draw=&draw,.cache=&cache,.bindings=bindings,
        .binding_count=1,.prior_vertices=prior,.depth=0.5f};
}
static void teardown(void) {
    CHECK(!model_present_close(&owner));
    for(unsigned i=0;i<2;i++) {
        uint32_t refs;CHECK(!gool_domain_references(&domain,spans[i].lease,&refs)&&refs==1);
        CHECK(!gool_domain_release(&domain,spans[i].lease));
    }
    CHECK(gool_domain_empty(&domain));
}
static int run(void) {
    int status=MODEL_PRESENT_PENDING;
    for(unsigned i=0;i<2048&&status==MODEL_PRESENT_PENDING;i++)status=model_present_step(&owner,1,17);
    return status;
}
static void frame_budget(void) {
    model_present_controls c=setup(0,0,24996);
    CHECK(model_present_begin(&owner,&domain,spans,0x1000,&c)==1);
    CHECK(run()==MODEL_PRESENT_DONE&&recorded==1&&owner.frame_vertices==24999);teardown();
    c=setup(1,1,24996);
    CHECK(model_present_begin(&owner,&domain,spans,0x1000,&c)==1);
    CHECK(run()==MODEL_PRESENT_VERTEX_LIMIT&&recorded==0&&requests==0);
    CHECK(owner.frame_vertices==24996&&owner.triangles==0);
    CHECK(model_present_step(&owner,100,17)==MODEL_PRESENT_VERTEX_LIMIT);teardown();
    c=setup(0,0,25000);
    CHECK(model_present_begin(&owner,&domain,spans,0x1000,&c)==MODEL_PRESENT_INVALID);teardown();
}
static void drain_between_stp_passes(void) {
    model_present_controls c=setup(1,1,24993);capacity_after=1;
    CHECK(model_present_begin(&owner,&domain,spans,0x1000,&c)==1);
    CHECK(run()==MODEL_PRESENT_DRAIN&&recorded==1&&owner.pass==1);
    CHECK(owner.frame_vertices==24996&&owner.triangles==0&&owner.word==0);
    CHECK(alphas[0]==255&&owner.status==MODEL_PRESENT_PENDING);
    CHECK(model_present_step(&owner,20,17)==MODEL_PRESENT_DRAIN&&recorded==1);
    /* Model caller draining its queue. The presentation cursor must preserve
     * frame-wide accounting and resume at STP, never replay opaque geometry. */
    capacity_after=8;
    CHECK(run()==MODEL_PRESENT_DONE&&recorded==2&&alphas[1]==128);
    CHECK(owner.frame_vertices==24999&&owner.triangles==1&&owner.passes==2&&requests==1);
    CHECK(!evictions);teardown();
}
static void asynchronous_texture_pressure(void) {
    model_present_controls c=setup(0,1,0);request_result=NSF_STREAM_FULL;
    CHECK(model_present_begin(&owner,&domain,spans,0x1000,&c)==1);
    /* Stream-slot pressure is retryable without consuming or recording. */
    for(unsigned i=0;i<40;i++)CHECK(model_present_step(&owner,1,17)==MODEL_PRESENT_PENDING);
    CHECK(requests&&recorded==0&&!polls&&owner.word==0&&owner.frame_vertices==0);
    request_result=MODEL_TEXTURE_CACHE_BUSY;
    CHECK(model_present_step(&owner,1,17)==MODEL_PRESENT_PENDING&&polls==1);
    request_result=0;CHECK(run()==MODEL_PRESENT_DONE&&recorded==1&&owner.frame_vertices==3);teardown();
    c=setup(1,1,0);request_result=MODEL_TEXTURE_CACHE_FULL;
    CHECK(model_present_begin(&owner,&domain,spans,0x1000,&c)==1);
    CHECK(run()==MODEL_PRESENT_DRAIN&&recorded==0&&owner.pass==0&&owner.frame_vertices==0);
    request_result=0;CHECK(run()==MODEL_PRESENT_DONE&&recorded==2);teardown();
}
int main(void) {
    frame_budget();drain_between_stp_passes();asynchronous_texture_pressure();
    puts("PASS production presentation: frame vertex limit, atomic pass reservation, ordered drain continuation, transient texture pressure and retained DMA ownership");
}
'''


def main():
    zig = importlib.util.find_spec("ziglang")
    if zig is None or zig.origin is None:
        raise SystemExit("ziglang is required")
    compiler = Path(zig.origin).parent / ("zig.exe" if os.name == "nt" else "zig")
    with tempfile.TemporaryDirectory(prefix="crash-present-") as folder:
        directory = Path(folder)
        (directory / "citro3d.h").write_text(SDK)
        (directory / "test.c").write_text(HARNESS)
        executable = directory / "test.exe"
        sources = [ROOT / "source" / name for name in
                   ("model_present.c", "model_packets.c", "model_packet_plan.c", "gool_domain.c")]
        subprocess.run([str(compiler), "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                        "-I", str(directory), "-I", str(ROOT / "source"), str(directory / "test.c"),
                        *map(str, sources), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
