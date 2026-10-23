"""Production frame and eight-root owners with explicit GPU/VM boundaries.

Actual packet traversal, pass planning, tree traversal and destruction execute.
GPU and individual object execution are boundary doubles; this does not prove
hardware execution or supply the still-missing engine bootstrap providers.
"""
from pathlib import Path
import importlib.util
import os
import subprocess
import tempfile
from test_model_packets import SDK

ROOT = Path(__file__).resolve().parents[1]
EXTRA_SDK = r'''
typedef struct {int unused;} C3D_RenderTarget;
enum {C3D_CLEAR_ALL=3};
int C3D_FrameBegin(int flags);
void C3D_FrameEnd(int flags);
int C3D_FrameDrawOn(C3D_RenderTarget *target);
void C3D_RenderTargetClear(C3D_RenderTarget *target,int flags,unsigned color,unsigned depth);
'''
HARNESS = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "native_runtime.h"
#define CHECK(c) do {if(!(c)){fprintf(stderr,"FAIL %d %s\n",__LINE__,#c);exit(1);}}while(0)
static int gpu_open,gpu_pending,reject_next,begins,ends,clears,submits,retires,finished,recorded;
static gool_domain domain;
static native_frame frame;
static model_texture_cache cache;
static C3D_RenderTarget target;
int C3D_FrameBegin(int flags){CHECK(!flags&&!gpu_open);if(reject_next){reject_next=0;return 0;}gpu_pending=0;gpu_open=1;begins++;return 1;}
void C3D_FrameEnd(int flags){CHECK(!flags&&gpu_open);gpu_open=0;gpu_pending=1;ends++;}
int C3D_FrameDrawOn(C3D_RenderTarget *t){CHECK(gpu_open&&t==&target);return 1;}
void C3D_RenderTargetClear(C3D_RenderTarget *t,int flags,unsigned color,unsigned depth){CHECK(gpu_open&&t==&target&&flags==C3D_CLEAR_ALL&&color==123&&!depth);clears++;}
int model_draw_init(model_draw *d){d->initialized=1;return 0;}
int model_draw_begin(model_draw *d){CHECK(!gpu_open&&!gpu_pending&&!d->recording&&!d->submitted&&!d->vertex_count);d->recording=1;return 0;}
int model_draw_submit(model_draw *d,uint32_t budget){CHECK(gpu_open&&budget&&d->recording&&d->vertex_count==3);d->submitted=1;submits++;return 0;}
int model_draw_retire(model_draw *d){CHECK(!gpu_open);if(d->submitted){CHECK(C3D_FrameBegin(0));C3D_FrameEnd(0);gpu_pending=0;retires++;}d->submitted=d->recording=0;d->vertex_count=0;return 0;}
int model_draw_close(model_draw *d){int e=model_draw_retire(d);d->initialized=0;return e;}
int model_draw_record_packet_triangle(model_draw *d,const model_packet_command *c,const model_material_uv *m,const C3D_Mtx *v,float depth,const model_draw_state *s,model_texture_cache *tc,model_texture_handle h){
 (void)m;(void)v;(void)depth;(void)s;(void)tc;(void)h;CHECK(d->recording&&!gpu_open&&c->kind==MODEL_PACKET_TRIANGLE);
 if(d->vertex_count)return MODEL_DRAW_CAPACITY;
 d->vertex_count+=3;recorded++;return 0;
}
int model_texture_cache_request(model_texture_cache *c,const model_material_uv *m,model_texture_handle *h){(void)c;(void)m;(void)h;return -999;}
int model_texture_cache_poll(model_texture_cache *c,uint32_t b){(void)c;(void)b;return -999;}
int model_texture_cache_evict(model_texture_cache *c,model_texture_handle h){(void)c;(void)h;return -999;}
int model_stream_begin(model_stream *o,model_actor *a,const model_stream_controls *c){(void)o;(void)a;(void)c;return -999;}
int model_stream_step(model_stream *o,uint32_t b){(void)o;(void)b;return -999;}
int model_stream_close(model_stream *o){CHECK(!o->opened);return 0;}
static void finish(void *c){CHECK(c==&frame&&gpu_open&&submits==2);finished++;}
static void frame_test(void){
 uint32_t ot=0x2000,arena[]={0x0600201c,0x30030201,0x200010,0x60504,0x400030,0x90807,0x600050,
                          0x06ffffff,0x30030201,0x200010,0x60504,0x400030,0x90807,0x600050};
 model_packet_span spans[2]={{.address=0x80001000,.bytes=4},{.address=0x80002000,.bytes=sizeof(arena)}};
 CHECK(!gool_domain_map_mutable(&domain,spans[0].address,&ot,4,&spans[0].lease));
 CHECK(!gool_domain_map_mutable(&domain,spans[1].address,arena,sizeof(arena),&spans[1].lease));
 CHECK(!native_frame_init(&frame));cache.opened=1;
 model_present_controls controls={.cache=&cache,.prior_vertices=24993,.depth=.5f};
 CHECK(native_frame_begin(&frame,&target,123,&domain,spans,0x1000,&controls,finish,&frame)==1);
 reject_next=1;int status=1;
 for(unsigned n=0;n<1000&&status==1;n++)status=native_frame_step(&frame,1,17);
 CHECK(!status&&recorded==2&&submits==2&&retires==2&&clears==1&&finished==1);
 CHECK(frame.vertices==24999&&begins==ends&&!gpu_open&&!gpu_pending);
 for(unsigned i=0;i<2;i++){uint32_t refs;CHECK(!gool_domain_references(&domain,spans[i].lease,&refs)&&refs==1);CHECK(!gool_domain_release(&domain,spans[i].lease));}
 CHECK(!native_frame_close(&frame)&&gool_domain_empty(&domain));
}
/* VM boundaries deliberately yield once per object. Real forest/deletion
 * algorithms must retain the same visit and stop-audio job across yields. */
static gool_native_actor actors[2];
static unsigned visits[2],audio_calls,notify_calls,factory_retirements;
int gool_native_factory_retired(gool_native_factory *f,uint32_t node){uint32_t type;CHECK(node==0x1000&&audio_calls==2&&!gool_domain_read32(&domain,node,&type)&&!type);CHECK(f->slots[0].actor.attached);f->slots[0].actor.attached=0;factory_retirements++;return 0;}
int gool_native_factory_actor(void *c,uint32_t node,gool_native_actor **a){(void)c;CHECK(node==0x1000||node==0x1200);*a=&actors[(node-0x1000)/0x200];return 0;}
int gool_native_registry_read(void *c,uint32_t a,uint32_t *v){(void)c;return gool_domain_read32(&domain,a,v);}
int gool_native_registry_write(void *c,uint32_t a,uint32_t v){(void)c;return gool_domain_write32(&domain,a,v);}
int gool_native_object_bind(gool_native_object *o,gool_native_factory *f,gool_native_route_frame *r,uint32_t n,uint32_t a,const gool_native_object_services *s){(void)r;(void)n;(void)a;(void)s;o->bound=1;o->factory=f;return 0;}
int gool_native_object_begin(gool_native_object *o,gool_native_actor *a,uint32_t requested){CHECK(requested==1&&!o->active);o->actor=a;o->active=1;o->phase=0;visits[(a->node-0x1000)/0x200]++;return 1;}
int gool_native_object_step(gool_native_object *o,uint32_t b){CHECK(b==1);if(!o->phase++){return 1;}o->active=0;o->native_result=o->actor->node==0x1000?-25:-255;return 0;}
int gool_native_object_close(gool_native_object *o){o->bound=o->active=0;return 0;}
static int notify(void *c,uint32_t node,uint32_t signal){(void)c;CHECK(node==0x1000&&signal==0x1a00);notify_calls++;return 0;}
static int stop_audio(void *c,uint32_t node){(void)c;CHECK(node==0x1000);audio_calls++;return audio_calls==1?GOOL_DESTROY_PROVIDER_PENDING:0;}
static void runtime_test(void){
 native_runtime owner={0};gool_native_factory factory={0};gool_native_registry registry={0};gool_native_factory_slot slots[1]={0};
 uint64_t clock=9;uint32_t roots[20]={0},objects[256]={0},usage[1]={0},protected=0,detached=0;
 gool_domain_lease mappings[2];
 for(unsigned i=0;i<9;i++)roots[i*2]=2;
 roots[1]=0x1000;roots[19]=9;objects[0]=objects[128]=1;objects[0x44/4]=objects[128+0x44/4]=0x80068ea0;
 objects[0x48/4]=0x1200;
 actors[0].node=0x1000;actors[1].node=0x1200;
 CHECK(!gool_domain_map_mutable(&domain,0x80068ea0,roots,sizeof(roots),&mappings[0]));
 CHECK(!gool_domain_map_mutable(&domain,0x1000,objects,sizeof(objects),&mappings[1]));
 factory.open=1;factory.engine.tick=&clock;factory.registry=&registry;factory.slots=slots;factory.count=1;slots[0].node=0x1000;slots[0].actor.attached=1;
 gool_destroy_services retirement={.protected_object=&protected,.detached_object=&detached,.usage=usage,.usage_count=1,.free_root=0x80068ee0,.scan_limit=96,.notify=notify,.stop_audio=stop_audio};
 CHECK(!native_runtime_bind(&owner,&factory,NULL,0,0,NULL,&retirement));
 CHECK(native_runtime_begin(&owner,1)==1);
 int status=1;for(unsigned n=0;n<1000&&status==1;n++){status=native_runtime_step(&owner,1);if(audio_calls==1)CHECK(native_runtime_close(&owner)==NATIVE_RUNTIME_BUSY);}
 CHECK(!status&&visits[0]==1&&visits[1]==1&&audio_calls==2&&!notify_calls&&factory_retirements==1);
 CHECK(objects[0]==0&&roots[1]==0x1200&&roots[17]==0x1000);
 CHECK(native_runtime_begin(&owner,1)==1);clock++;
 CHECK(native_runtime_step(&owner,1)==GOOL_NATIVE_OBJECT_TIMELINE);clock--;
 CHECK(native_runtime_step(&owner,100)==GOOL_NATIVE_OBJECT_TIMELINE&&visits[1]==1);
 CHECK(!native_runtime_close(&owner));
 for(unsigned i=0;i<2;i++)CHECK(!gool_domain_release(&domain,mappings[i]));
 CHECK(gool_domain_empty(&domain));
}
int main(void){frame_test();runtime_test();puts("PASS native runtime: eight-root visits, suspended audio retirement, real packet drain ownership, GPU barriers and aggregate frame budget");}
'''


def main():
    zig = importlib.util.find_spec("ziglang")
    if zig is None or zig.origin is None:
        raise SystemExit("ziglang is required")
    compiler = Path(zig.origin).parent / ("zig.exe" if os.name == "nt" else "zig")
    with tempfile.TemporaryDirectory(prefix="crash-native-runtime-") as folder:
        directory = Path(folder)
        (directory / "citro3d.h").write_text(SDK + EXTRA_SDK)
        (directory / "test.c").write_text(HARNESS)
        executable = directory / "test.exe"
        sources = [ROOT / "source" / name for name in (
            "native_runtime.c", "model_present.c", "model_packets.c", "model_packet_plan.c",
            "gool_domain.c", "gool_tree.c", "gool_forest.c", "gool_destroy.c")]
        subprocess.run([str(compiler), "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                        "-I", str(directory), "-I", str(ROOT / "source"), str(directory / "test.c"),
                        *map(str, sources), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
