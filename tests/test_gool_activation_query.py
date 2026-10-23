"""Resident activation-query ownership with actual domain and zone-bank code."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <assert.h>
#include <string.h>
#include "gool_native_activation_query.h"
static gool_domain domain;
static zdat_resources bank;
static uint8_t storage[4*65536];
static void w32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static void database(unsigned index,uint32_t fallback,uint16_t key) {
 uint8_t *p=storage+index*65536+0x100;memset(p,0,64);
 w32(p,64);w32(p+4,0x81000000+index*0x20000);w32(p+8,fallback);
 p[12]=1;p[16]=(uint8_t)key;p[17]=(uint8_t)(key>>8);p[18]=12;
 p[20]=0x23;p[21]=4;p[22]=1;p[24]=2; /* length2, exact time3 */
 p[26]=3;w32(p+28,0x12345678);w32(p+32,0x23456789);
}
int main(void) {
 assert(!zdat_resources_open(&bank,&domain,storage,0x81000000));
 for(unsigned i=0;i<2;i++) {
  zdat_resource_slot *s=&bank.slots[i];
  *s=(zdat_resource_slot){.generation=i+1,.address=0x81000000+i*0x20000,
   .size=65536,.eid=i*2+1,.state=ZDAT_RESOURCE_READY};
  assert(!gool_domain_map_mutable(&domain,s->address,storage+i*65536,65536,&s->mapping));s->mapped=1;
 }
 database(0,0x81020100,0x13b);database(1,0,0x13c);
 gool_native_activation_query q={.source={&domain,8},.bank=&bank};
 uint32_t list=99,count=99;
 assert(!gool_native_activation_query_begin(&q,0x81000100,0x13b,0x3ff,&list,&count));
 assert(list==0x8100011c&&count==2&&q.count==1);
 assert(bank.slots[0].references==1&&!bank.slots[1].references);
 assert(zdat_resources_evict(&bank,1)==ZDAT_RESOURCE_BUSY);
 assert(gool_native_activation_query_begin(&q,0x81000100,0x13b,0x300,&list,&count)<0);
 assert(!gool_native_activation_query_step(&q,1));
 /* Failed cancellation keeps the actual outstanding lease for retry. */
 bank.slots[0].generation=7;assert(gool_native_activation_query_cancel(&q)<0);
 assert(q.active&&q.count==1);bank.slots[0].generation=1;
 assert(!gool_native_activation_query_cancel(&q));assert(!bank.slots[0].references);
 assert(!gool_native_activation_query_begin(&q,0x81000100,0x13c,0x300,&list,&count));
 assert(list==0x8102011c&&count==2&&q.count==2);
 assert(bank.slots[0].references==1&&bank.slots[1].references==1);
 assert(!gool_native_activation_query_cancel(&q));
 assert(!gool_native_activation_query_begin(&q,0x81000100,0x13b,0x400,&list,&count));
 assert(!list&&!count);assert(!gool_native_activation_query_cancel(&q));
 /* Unused corrupt fallback is not traversed after a local key match. */
 database(0,0x81000100,0x13b);
 assert(!gool_native_activation_query_begin(&q,0x81000100,0x13b,0x300,&list,&count));
 assert(!gool_native_activation_query_cancel(&q));
 list=count=99;
 assert(gool_native_activation_query_begin(&q,0x81000100,0x13c,0x300,&list,&count)==ZDAT_NATIVE_CYCLE);
 assert(list==99&&count==99&&q.count==1&&q.active);
 assert(!gool_native_activation_query_cancel(&q));
 database(0,0x81020100,0x13b);bank.slots[1].state=ZDAT_RESOURCE_LOADING;
 assert(gool_native_activation_query_begin(&q,0x81000100,0x13c,0x300,&list,&count)<0);
 assert(q.active&&q.count==1&&list==99&&count==99);
 assert(!gool_native_activation_query_cancel(&q));bank.slots[1].state=ZDAT_RESOURCE_READY;
 assert(!zdat_resources_close(&bank));assert(gool_domain_empty(&domain));
 return 0;
}
'''

def main():
    with tempfile.TemporaryDirectory(prefix='crash-activation-query-') as directory:
        folder = Path(directory)
        source, executable = folder / 'check.c', folder / 'check.exe'
        source.write_text(HARNESS)
        names = ['gool_native_activation_query.c', 'gool_domain.c', 'zdat_native.c',
                 'zdat_resources.c', 'zdat_runtime.c', 'zone_transition.c',
                 'nsf_resource.c', 'runtime_archive.c']
        subprocess.run([sys.executable, '-m', 'ziglang', 'cc', '-O2', '-UNDEBUG', '-Wall', '-Wextra',
                        '-Werror', '-I' + str(ROOT / 'source'), str(source),
                        *[str(ROOT / 'source' / name) for name in names],
                        str(ROOT / 'tests/host_nsf_stream.c'), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
    print('PASS activation query: resident/fallback ownership, exact samples, cancellation retries and faults')

if __name__ == '__main__':
    main()
