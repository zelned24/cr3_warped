"""Production zone retirement composition with explicit signal/audio boundaries.

Runs actual eight-root predicates and1A844 destruction. Signal boundary doubles
check dedicated state/router ownership, continuation identity and cancellation;
this regression does not claim execution of original GOOL signal bytecode.
"""
from pathlib import Path
import importlib.util
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gool_native_zone_retirement.h"
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL %d %s\n",__LINE__,#c);exit(1);}}while(0)
static gool_domain domain;
static unsigned starts,polls,router_closes,state_closes,delivery_closes,audio_calls,cancels,detaches;
static unsigned router_block,cancel_block,stop_error;
static uint32_t signaled;
int gool_native_registry_read(void *c,uint32_t a,uint32_t *v){(void)c;return gool_domain_read32(&domain,a,v);}
int gool_native_registry_write(void *c,uint32_t a,uint32_t v){(void)c;return gool_domain_write32(&domain,a,v);}
int gool_native_factory_actor(void *c,uint32_t n,gool_native_actor **a){(void)c;(void)n;(void)a;return -999;}
int gool_native_factory_retired(gool_native_factory *f,uint32_t node){uint32_t type;CHECK(node==0x1000&&!gool_domain_read32(&domain,node,&type)&&!type&&audio_calls==2);f->slots[0].actor.attached=0;detaches++;return 0;}
int gool_native_state_bind(gool_native_state *s,gool_native_registry *r,gool_native_pool *p,const gool_native_state_engine *e,uint32_t b){CHECK(r&&p&&e->actor==gool_native_factory_actor&&b==7&&!s->bound);s->bound=1;return 0;}
int gool_native_delivery_bind(gool_native_delivery *d,gool_native_factory *f,gool_native_state *s,uint32_t a,uint32_t b){CHECK(f&&s->bound&&a==0xc0000000&&b==7&&!d->bound);d->bound=1;return 0;}
int gool_native_router_bind(gool_native_router *r,gool_native_factory *f,gool_native_route_frame *a,uint32_t n,uint32_t b){CHECK(f&&a&&n==2&&b==0xc0000100&&!r->bound);r->bound=1;return 0;}
int gool_native_delivery_begin_native(gool_native_delivery *d,uint32_t sender,uint32_t node,int32_t signal,uint32_t count,uint32_t args){CHECK(d->bound&&!sender&&signal==0x1a00&&!count&&!args);signaled=node;starts++;return 1;}
int gool_native_router_engine_delivery_step(gool_native_router *r,gool_native_delivery *d,uint32_t b){CHECK(r->bound&&d->bound&&b==1);polls++;return router_block?1:0;}
int gool_native_router_close(gool_native_router *r){if(r->bound){if(router_block==2)return -777;r->bound=0;router_closes++;}return 0;}
int gool_native_state_close(gool_native_state *s){if(s->bound){CHECK(router_closes>state_closes);s->bound=0;state_closes++;}return 0;}
int gool_native_delivery_close(gool_native_delivery *d){if(d->bound){CHECK(state_closes>delivery_closes);d->bound=0;delivery_closes++;}return 0;}
static int stop(void *c,uint32_t node){CHECK(c==&domain&&node==0x1000);audio_calls++;if(stop_error)return -555;return audio_calls&1?1:0;}
static int cancel_audio(void *c,uint32_t node){CHECK(c==&domain&&node==0x1000);cancels++;return cancel_block?1:0;}
int main(void){
 uint32_t roots[20]={0},object[0x104/4]={0},usage[1]={1},protected=0,detached=0;
 uint64_t clock=4;gool_domain_lease leases[2];
 gool_native_zone_retirement owner={0};gool_native_factory factory={0};gool_native_registry registry={.domain=&domain};
 gool_native_pool pool={0};gool_native_factory_slot slots[1]={0};gool_native_route_frame frames[2]={0};
 factory.open=1;factory.registry=&registry;factory.pool=&pool;factory.engine.tick=&clock;factory.slots=slots;factory.count=1;
 slots[0].node=0x1000;slots[0].actor.attached=1;
 for(unsigned i=0;i<9;i++)roots[i*2]=2;roots[1]=0x1000;roots[19]=4;
 object[0]=1;object[0x18/4]=0x9000;object[0x44/4]=0x80068ea0;
 CHECK(!gool_domain_map_mutable(&domain,0x80068ea0,roots,sizeof(roots),&leases[0]));
 CHECK(!gool_domain_map_mutable(&domain,0x1000,object,sizeof(object),&leases[1]));
 gool_destroy_services destroy={.protected_object=&protected,.detached_object=&detached,.usage=usage,.usage_count=1,.free_root=0x80068ee0,.scan_limit=96};
 gool_native_retirement_audio audio={.context=&domain,.stop_audio=stop,.cancel_audio=cancel_audio};
 CHECK(!gool_native_zone_retirement_bind(&owner,&factory,frames,2,0xc0000000,7,100,&destroy,&audio));
 gool_zone_retire retirement={0};CHECK(gool_zone_retire_begin(&retirement,0x9000,&owner.services)==1);
 router_block=1;int status=1;
 for(unsigned n=0;n<100&&starts==0;n++)CHECK(gool_zone_retire_step(&retirement,1)==1);
 CHECK(starts==1&&signaled==0x1000);
 for(unsigned n=0;n<4;n++)CHECK(gool_zone_retire_step(&retirement,1)==1);
 CHECK(starts==1&&!audio_calls);
 router_block=0;
 for(unsigned n=0;n<1000&&status==1;n++)status=gool_zone_retire_step(&retirement,1);
 CHECK(!status&&object[0]==0&&audio_calls==2&&detaches==1&&usage[0]==0);
 CHECK(retirement.native_result==-255&&starts==1&&router_closes==1&&state_closes==1&&delivery_closes==1);
 CHECK(!gool_zone_retire_close(&retirement));
 /* Direct signal cancellation preserves all deeper storage on rejection. */
 router_block=2;CHECK(owner.services.signal(&owner,0x1000,0x1a00)==1);
 CHECK(owner.services.cancel(&owner,0x1000,0)==-777&&owner.signal_owned&&owner.state.bound&&owner.delivery.bound);
 router_block=0;CHECK(!owner.services.cancel(&owner,0x1000,0)&&!owner.signal_owned);
 /* Real audio cancellation can itself suspend; no successful deletion result
  * is published while stopping voices remains pending or has failed. */
 object[0]=1;audio_calls=0;stop_error=1;int32_t result=1234;status=1;
 for(unsigned n=0;n<20&&status==1;n++)status=owner.services.retire(&owner,0x1000,0,&result);
 CHECK(status==-555&&result==1234&&owner.audio_owned&&object[0]==1);
 cancel_block=1;CHECK(owner.services.cancel(&owner,0x1000,1)==1&&owner.audio_owned);
 cancel_block=0;CHECK(!owner.services.cancel(&owner,0x1000,1)&&!owner.audio_owned&&cancels==2);
 /* A changed engine tick faults before signal dispatch can start. */
 clock++;unsigned prior_starts=starts;CHECK(owner.services.signal(&owner,0x1000,0x1a00)==GOOL_NATIVE_ZONE_RETIREMENT_TIMELINE&&starts==prior_starts);
 CHECK(!gool_native_zone_retirement_close(&owner));
 for(unsigned i=0;i<2;i++)CHECK(!gool_domain_release(&domain,leases[i]));
 CHECK(gool_domain_empty(&domain));
 puts("PASS native retirement: dedicated signal continuation, postorder destruction, voice/actor ownership, native result and deepest-first cancellation");
}
'''


def main():
    zig = importlib.util.find_spec("ziglang")
    if zig is None or zig.origin is None:
        raise SystemExit("ziglang is required")
    compiler = Path(zig.origin).parent / ("zig.exe" if os.name == "nt" else "zig")
    with tempfile.TemporaryDirectory(prefix="crash-native-retirement-") as folder:
        directory = Path(folder)
        (directory / "test.c").write_text(HARNESS)
        executable = directory / "test.exe"
        sources = [ROOT / "source" / name for name in (
            "gool_native_zone_retirement.c", "gool_zone_retire.c", "gool_destroy.c", "gool_tree.c", "gool_domain.c")]
        subprocess.run([str(compiler), "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                        "-I", str(ROOT / "source"), str(directory / "test.c"),
                        *map(str, sources), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
