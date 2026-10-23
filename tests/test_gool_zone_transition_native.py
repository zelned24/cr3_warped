"""Production215E8 composes retained exact activation queries across samples.

Other native callees are explicit suspended fixtures, not implemented cameras.
"""
from pathlib import Path
import subprocess
import sys
import tempfile
from host_native_sources import all_sources

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "gool_native_zone_transition.h"
#include "gool_native_activation_query.h"
static gool_domain domain;
static zdat_resources bank;
static gool_native_registry registry;
static uint8_t zone[65536],globals[512],gp[1024],table[768],level[4],player[4],reset[4];
static gool_domain_lease mappings[7];
static gool_native_activation_query query;
static uint32_t *result;
static unsigned active,pending,calls,paths;
static void put(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static int begin(void *ctx,const gool_native_zone_transition_call *c,uint32_t *out) {
 assert(ctx==&calls&&!active);active=1;pending=2;result=out;calls++;
 /* Internal1AD68 must never escape to this native-call boundary. */
 assert(c->address!=0x8001ad68);
 switch(c->address) {
 case 0x80032edc:assert(c->arguments[1]==0x27f);*out=4;break;
 case 0x80022c9c:paths++;*out=0;break;
 case 0x80032df8:case 0x80014468:case 0x8001ae64:case 0x80021e60:
 case 0x80012eac:case 0x8002437c:case 0x80028740:case 0x80032710:case 0x80025c0c:
  *out=0;break;
 default:assert(!"unexpected native fixture call");
 }
 return 1;
}
static int step(void *ctx,uint32_t budget){assert(ctx==&calls&&active&&result&&budget);return pending--?1:0;}
static int cancel(void *ctx){assert(ctx==&calls&&active);active=0;result=0;return 0;}
static int signal(void *ctx,uint32_t node,uint32_t sig){(void)ctx;(void)node;(void)sig;assert(0);return -999;}
static int retire(void *ctx,uint32_t node,uint32_t n,int32_t *v){(void)ctx;(void)node;(void)n;(void)v;assert(0);return -999;}
static int retire_cancel(void *ctx,uint32_t node,int n){(void)ctx;(void)node;(void)n;assert(0);return -999;}
static void map(unsigned i,uint32_t address,void *bytes,uint32_t size){assert(!gool_domain_map_mutable(&domain,address,bytes,size,&mappings[i]));}
int main(void) {
 bank=(zdat_resources){.domain=&domain,.open=1};
 bank.slots[0]=(zdat_resource_slot){.generation=1,.address=0x81000000,.size=sizeof(zone),.state=ZDAT_RESOURCE_READY};
 map(0,0x81000000,zone,sizeof(zone));map(1,0x80065c90,globals,sizeof(globals));
 map(2,0x8006c000,gp,sizeof(gp));map(3,0x80067a38,table,sizeof(table));
 map(4,0x80068f3c,level,sizeof(level));map(5,0x80060a90,player,sizeof(player));
 map(6,0x80061a70,reset,sizeof(reset));
 registry=(gool_native_registry){.domain=&domain,.zones=&bank};
 query=(gool_native_activation_query){.source={&domain,8},.bank=&bank};
 put(zone+16,0x81000100);put(player,0x81200000);
 uint8_t *db=zone+0x1000;put(db,64);put(db+4,0x81000000);db[12]=2;
 db[16]=0x3b;db[17]=1;db[18]=20;db[20]=0x23;db[21]=4;db[22]=2;
 db[24]=0x3c;db[25]=1;db[26]=36;db[28]=0x23;db[29]=4;db[30]=2;
 db[32]=1;db[36]=1;put(db+40,0xa001);put(db+44,0xb002);
 db[48]=1;db[52]=1;put(db+56,0xc003);put(db+60,0xd004);
 gool_native_zone_transition_services s={.context=&calls,.registry=&registry,
  .retirement={.domain=&domain,.max_nodes=64,.signal=signal,.retire=retire,.cancel=retire_cancel},
  .activation_query={&query,gool_native_activation_query_begin,gool_native_activation_query_step,gool_native_activation_query_cancel},
  .neighbor_limit=64,.path_limit=64,.activation_capacity=64,.call_begin=begin,.call_step=step,.call_cancel=cancel};
 gool_native_zone_transition owner={0};
 int e=gool_native_zone_transition_begin(&owner,0x81000000,0x81001000,0x100,0,0x8006c000,&s);
 assert(e==1);assert(gool_native_zone_transition_step(&owner,0)==1);
 for(unsigned i=0;e==1&&i<10000;i++)e=gool_native_zone_transition_step(&owner,1+i%3);
 if(e||paths!=2||active||query.count||query.active)
  fprintf(stderr,"transition status=%d phase=%u paths=%u call=%u query=%u/%d\n",e,owner.phase,paths,active,query.count,query.active);
 assert(e==0&&paths==2&&!active&&query.count==0&&!query.active);
 uint32_t count;assert(!gool_domain_read32(&domain,0x8006c21c,&count));assert(count==3);
 uint32_t expected[6]={0xc003,0x13c,0xa001,0x13b,0xd004,0x13c};
 for(unsigned i=0;i<3;i++) {
  uint32_t value,marker;assert(!gool_domain_read32(&domain,0x80067a38+i*12,&value));
  assert(!gool_domain_read32(&domain,0x80067a40+i*12,&marker));
  assert(value==expected[i*2]&&marker==expected[i*2+1]);
 }
 assert(bank.slots[0].references==1);assert(!gool_native_zone_transition_close(&owner));
 assert(!bank.slots[0].references);
 for(unsigned i=7;i;i--)assert(!gool_domain_release(&domain,mappings[i-1]));
 assert(gool_domain_empty(&domain));return 0;
}
'''

def main():
    with tempfile.TemporaryDirectory(prefix='crash-zone-native-') as directory:
        folder = Path(directory)
        source, executable = folder / 'check.c', folder / 'check.exe'
        source.write_text(HARNESS)
        subprocess.run([sys.executable, '-m', 'ziglang', 'cc', '-O2', '-UNDEBUG',
                        '-Wall', '-Wextra', '-Werror', '-I' + str(ROOT / 'source'),
                        str(source), *all_sources(), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
    print('PASS native zone transition: reset, exact activation path traversal, asynchronous native calls and retained owners')

if __name__ == '__main__':
    main()
