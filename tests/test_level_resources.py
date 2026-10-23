"""Verify level-owned original resource sessions with production host threads."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import ctypes as C
import _ctypes
import random
import struct

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import cb3
from test_nsf_stream import SDK
from test_gool_stack_native import replay
from psx_native import Image, ORIGINAL_SHA256

HARNESS = r'''
#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include "level_resources.h"
#include "nsf_resource.h"
int test_allocations,test_cache_failure,test_thread_failure,test_alloc_failure;
DWORD test_worker_id;
static uint8_t nsd[65536],expected[65536];
static uint32_t word(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void ready(nsf_resource_ticket ticket){
 for(unsigned i=0;i<10000;i++){
  nsf_stream_state state;int error;assert(!nsf_resource_poll(ticket,&state,&error));
  if(state==NSF_STREAM_READY){assert(!error);return;}
  assert(state==NSF_STREAM_QUEUED||state==NSF_STREAM_READING);Sleep(1);
 }
 assert(!"level worker timeout");
}
int main(int argc,char **argv){
 assert(argc>=4);assert(level_resources_level()==-1&&!level_resources_close());
 unsigned count=0;
 for(int arg=3;arg<argc;arg++){
  unsigned id=(unsigned)strtoul(argv[arg],NULL,0);char path[1024];
  assert(!level_resources_open(argv[1],id));assert(level_resources_level()==(int)id&&test_allocations==1);
  snprintf(path,sizeof(path),"%s/S%07X.NSD",argv[1],id);FILE *f=fopen(path,"rb");assert(f);
  size_t size=fread(nsd,1,sizeof(nsd),f);assert(size>=0x528&&!fclose(f));
  uint32_t eid=word(nsd+0x524),cid=word(nsd+0x520);nsf_resource_ticket ticket;
  assert(!nsf_resource_request(eid,&ticket));ready(ticket);
  nsf_resource_view view={0};assert(!nsf_resource_acquire(ticket,&view));assert(!nsf_resource_release_ticket(ticket));
  snprintf(path,sizeof(path),"%s/S%07X.NSF",argv[1],id);f=fopen(path,"rb");assert(f);
  assert(!fseek(f,(long)(cid>>1)*65536,SEEK_SET));assert(fread(expected,1,sizeof(expected),f)==sizeof(expected)&&!fclose(f));
  assert(!memcmp(view.page,expected,sizeof(expected)));
  assert(level_resources_open(NULL,id)==LEVEL_RESOURCES_INVALID&&level_resources_level()==(int)id);
  assert(level_resources_open(argv[1],64)==LEVEL_RESOURCES_INVALID&&level_resources_level()==(int)id);
  assert(level_resources_close()==NSF_STREAM_BUSY&&level_resources_level()==(int)id);
  assert(level_resources_open(argv[1],id)==NSF_STREAM_BUSY&&level_resources_level()==(int)id);
  assert(!memcmp(view.page,expected,sizeof(expected)));
  assert(!nsf_resource_release_view(&view));assert(!level_resources_close());
  assert(level_resources_level()==-1&&!test_allocations);count++;
 }
 /* Each malformed directory has a valid-looking filename pair but a distinct
  * physical or metadata failure. No unusable level may become current. */
 for(unsigned id=1;id<=5;id++){
  assert(level_resources_open(argv[2],id)<0);assert(level_resources_level()==-1&&!test_allocations);
 }
 test_thread_failure=1;assert(level_resources_open(argv[1],(unsigned)strtoul(argv[3],NULL,0))<0);
 test_thread_failure=0;assert(level_resources_level()==-1&&!test_allocations);
 assert(!level_resources_open(argv[1],(unsigned)strtoul(argv[3],NULL,0)));
 assert(!level_resources_close()&&!test_allocations);
 printf("PASS level resources: %u original sessions, retained reload/close leases, missing/malformed assets and failed worker recovery\n",count);
}
'''

CACHE_HARNESS = r'''
#include "gool_native_zone_transition.h"
static gool_domain cache_domain;
static zdat_native cache_database={.domain=&cache_domain,.chain_limit=ZDAT_NATIVE_CHAIN_MAX};
static zdat_resources cache_zones,cache_models;
static uint8_t zone_bytes[4*65536],model_bytes[4*65536],misc_bytes[4*65536],reference_word[4];
static zdat_native *level_native_database(void){return &cache_database;}
static zdat_resources *level_native_zones(void){return &cache_zones;}
static zdat_resources *level_native_models(void){return &cache_models;}
static int native_read(uint32_t address,uint32_t *value){return gool_domain_read32(&cache_domain,address,value);}
static int native_write(uint32_t address,uint32_t value){return gool_domain_write32(&cache_domain,address,value);}
static int native_missing(uint32_t address){(void)address;return -240;}
/*PRODUCTION_OWNER*/
/*PRODUCTION_HELPERS*/
static int prefetch_run(uint32_t database,uint32_t position,uint32_t direction,unsigned budget){
 memset(&s_native.prefetch,0,sizeof(s_native.prefetch));
 s_native.prefetch.busy=1;s_native.prefetch.database=database;
 s_native.prefetch.position=position;s_native.prefetch.direction=direction;
 for(unsigned i=0;i<10000;i++){
  int status=native_prefetch_step(budget);
  if(status!=1)return status;
  Sleep(1);
 }
 assert(!"native prefetch timeout");return -1;
}
static void pins(unsigned expected){
 unsigned soft=0,total=0;
 for(unsigned i=0;i<NATIVE_MAIN_PAGES;i++){soft+=s_native.pages[i].soft;total+=s_native.pages[i].total;}
 assert(soft==expected&&total==expected);
}
static void native_cache_check(const char *directory){
 assert(!level_resources_open(directory,11));
 nsd_resource_index index;assert(!level_resources_nsd(&index));
 uint32_t eid=word(index.data+0x520+index.count*8+0x2dc);
 nsf_resource_ticket ticket;assert(!nsf_resource_request(eid,&ticket));ready(ticket);
 nsf_resource_view view={0};assert(!nsf_resource_acquire(ticket,&view));
 assert(!zdat_resources_open(&cache_zones,&cache_domain,zone_bytes,0x81000000));
 assert(!zdat_resources_import(&cache_zones,&view));
 assert(!nsf_resource_release_view(&view)&&!nsf_resource_release_ticket(ticket));
 uint32_t zone,header,info,database;
 zdat_resource_lease zone_lease;
 assert(!zdat_resources_acquire(&cache_zones,eid,&zone,&zone_lease));
 assert(!native_read(zone+16,&header));assert(!native_read(header+0x184,&info));
 assert(!native_read(zone+16+info*4,&database));
 assert(!prefetch_run(database,0,2,1));pins(182);
 char path[1024];snprintf(path,sizeof(path),"%s/S000000B.NSF",directory);
 FILE *f=fopen(path,"rb");assert(f);unsigned pages=0;
 for(unsigned i=0;i<NATIVE_MAIN_PAGES;i++)if(s_native.pages[i].valid){
  assert(!fseek(f,(long)(s_native.pages[i].cid>>1)*65536,SEEK_SET));
  assert(fread(expected,1,65536,f)==65536);
  assert(!memcmp(expected,s_native.page_storage[i],65536));pages++;
 }
 assert(pages==25&&!fclose(f));
 assert(!prefetch_run(database,0,2,64));pins(364);
 assert(!prefetch_run(database,0xffffff00u,1,17));pins(182);
 assert(!prefetch_run(database,0xffffff00u,1,1));pins(0);
 /* Consume the same retained page through the production main adapter and
  * native bank. Relocated bytes must survive source cache replacement. */
 assert(!zdat_resources_open_models(&cache_models,&cache_domain,model_bytes,0x83000000));
 assert(!zdat_resources_open_models(&s_native.miscellaneous,&cache_domain,misc_bytes,0x84000000));
 s_native.miscellaneous.allowed_types=1u<<4;
 gool_domain_lease reference_lease;
 assert(!gool_domain_map_mutable(&cache_domain,0x82000000,reference_word,4,&reference_lease));
 unsigned imported=0;
 for(uint32_t i=0;i<index.count&&!imported;i++){
  uint32_t cid=word(index.data+0x520+i*8),candidate=word(index.data+0x524+i*8);
  int page=native_page_find(cid);if(page<0)continue;
  nsf_resource_view candidate_view={0};
  assert(!nsf_resource_parse_page(s_native.page_storage[page],65536,cid,candidate,&candidate_view));
  if(candidate_view.entry_type!=1&&candidate_view.entry_type!=2)continue;
  uint32_t result=0; s_native.resource_reference=0x82000000;
  assert(!native_resource_cached(candidate,&result));assert(word(reference_word)==result);
  assert(cache_models.slots[0].references==1&&zdat_resources_can_close(&cache_models)==ZDAT_RESOURCE_BUSY);
  memset(s_native.page_storage[page],0,65536);
  assert(!native_read(result,&eid)&&eid==0x0100ffffu);
  assert(!zdat_resources_release(&cache_models,s_native.resources[0].lease));
  memset(s_native.resources,0,sizeof(s_native.resources));imported=1;
 }
 assert(imported);
 /* Texture chunks have no entry directory: load the original bytes and
  * retain a page handle without presenting it as a relocated model entry. */
 unsigned texture_loaded=0;
 for(uint32_t i=0;i<index.count&&!texture_loaded;i++) {
  uint32_t candidate=word(index.data+0x524+i*8);
  assert(!nsf_resource_request(candidate,&ticket));ready(ticket);
  nsf_resource_view texture={0};assert(!nsf_resource_acquire(ticket,&texture));
  if(texture.page_type==1u) {
   s_native.resource_reference=0x82000000;uint32_t result,actual,cid;
   assert(!native_resource_texture(&texture,&result));assert(word(reference_word)==result);
   assert(!native_page_identity(result,&actual,&cid));
   assert(actual==candidate&&cid==ticket.cid);
   int page=native_page_find(cid);assert(page>=0&&s_native.pages[page].total);
   assert(!memcmp(s_native.page_storage[page],texture.page,65536));
   uint8_t before=s_native.pages[page].total;
   assert(!native_resource_cached(candidate,&result));
   assert(s_native.pages[page].total==before+1);texture_loaded=1;
  }
  assert(!nsf_resource_release_view(&texture)&&!nsf_resource_release_ticket(ticket));
 }
 assert(texture_loaded);
 /* Worker failure keeps the pending ticket releasable and publishes no page. */
 memset(s_native.pages,0,sizeof(s_native.pages));test_cache_failure=1;
 assert(prefetch_run(database,0,2,1)==NSF_STREAM_CACHE);pins(0);
 assert(!native_prefetch_release_io());test_cache_failure=0;
 memset(&s_native.prefetch,0,sizeof(s_native.prefetch));
 assert(!zdat_resources_release(&cache_zones,zone_lease));
 assert(!zdat_resources_close(&cache_models)&&!zdat_resources_close(&cache_zones));
 assert(!zdat_resources_close(&s_native.miscellaneous));
 assert(!gool_domain_release(&cache_domain,reference_lease));assert(gool_domain_empty(&cache_domain));
 assert(!level_resources_close()&&!test_allocations);
 puts("PASS main native resource lists: 182 entries/25 original pages, pin/release budgets, cached native import and worker failure cleanup");
}
'''


def camera_checks(tmp):
    main_source = (ROOT/'source/main.c').read_text()
    camera = 'static uint32_t native_camera_shift8' + main_source.split(
        'static uint32_t native_camera_shift8',1)[1].split('static int native_page_identity',1)[0]
    fixture = r'''
#include <assert.h>
#include <string.h>
#include "zdat_native.h"
static gool_domain domain;
static zdat_native source={.domain=&domain,.chain_limit=128};
static int native_read(uint32_t a,uint32_t *v){return gool_domain_read32(&domain,a,v);}
static int native_write(uint32_t a,uint32_t v){return gool_domain_write32(&domain,a,v);}
/*CAMERA*/
void bind_fixture(uint8_t *zone,uint8_t *db,uint8_t *camera,uint8_t *globals,uint8_t *out,uint8_t *aux){
 memset(&domain,0,sizeof(domain));gool_domain_lease lease;
 assert(!gool_domain_map_mutable(&domain,0x81000000,zone,1024,&lease));
 assert(!gool_domain_map_mutable(&domain,0x81100000,db,512,&lease));
 assert(!gool_domain_map_mutable(&domain,0x81200000,camera,512,&lease));
 assert(!gool_domain_map_mutable(&domain,0x80065d00,globals,256,&lease));
 assert(!gool_domain_map_mutable(&domain,0x81400000,out,32,&lease));
 assert(!gool_domain_map_mutable(&domain,0x81500000,aux,16,&lease));
}
int run_camera(uint32_t position,uint32_t aux,uint32_t *result){
 return native_camera_position(&source,0x81100000,position,0x81400000,aux?0x81500000:0,result);
}
uint32_t query(uint32_t db,uint32_t key,uint32_t element,uint32_t *length){
 zdat_native_value value;assert(!zdat_native_select(&source,db,(int32_t)key,0,element,&value));
 *length=value.length;return value.address;
}
uint32_t scalar(uint32_t db,uint32_t key){uint32_t value;assert(!zdat_native_scalar(&source,db,key,&value));return value;}
'''.replace('/*CAMERA*/',camera)
    src=tmp/'camera.c';dll=tmp/'camera.dll';src.write_text(fixture)
    subprocess.run([sys.executable,'-m','ziglang','cc','-shared','-O2','-UNDEBUG','-Wall','-Wextra','-Werror',
                    '-I'+str(ROOT/'source'),str(src),str(ROOT/'source/gool_domain.c'),
                    str(ROOT/'source/zdat_native.c'),'-o',str(dll)],check=True)
    lib=C.CDLL(str(dll));lib.bind_fixture.argtypes=[C.c_void_p]*6
    lib.run_camera.argtypes=[C.c_uint32,C.c_uint32,C.POINTER(C.c_uint32)]
    lib.query.argtypes=[C.c_uint32]*3+[C.POINTER(C.c_uint32)];lib.query.restype=C.c_uint32
    lib.scalar.argtypes=[C.c_uint32]*2;lib.scalar.restype=C.c_uint32
    image=Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes());assert image.sha256==ORIGINAL_SHA256
    rng=random.Random(0x28740)
    def database(points,rotation=False):
        blob=bytearray(512);struct.pack_into('<3I',blob,0,512,0x81000000,0)
        series=[(0x4b,6,6,b''.join(struct.pack('<3h',*p) for p in points),len(points))]
        if not rotation:series += [(0x173,4,4,bytes(4),1),(0x27f,1,1,bytes(len(points)),len(points))]
        struct.pack_into('<H',blob,12,len(series));cursor=64
        for i,(key,fmt,stride,payload,length) in enumerate(series):
            struct.pack_into('<HHBBH',blob,16+i*8,key,cursor-12,fmt,stride,1)
            struct.pack_into('<H',blob,cursor,length);cursor=(cursor+5)&~3
            blob[cursor:cursor+len(payload)]=payload;cursor=(cursor+len(payload)+3)&~3
        return blob
    for case in range(192):
        n=0 if case%12==0 else 4
        points=[tuple(rng.randint(-32768,32767) for _ in range(3)) for _ in range(n)]
        rotations=[tuple(rng.randint(-32768,32767) for _ in range(3)) for _ in range(n*2)]
        db=database(points);cam=database(rotations,True)
        zone=bytearray(1024);struct.pack_into('<4I',zone,16,0x81000100,0x81000300,0x81100000,0x81200000)
        struct.pack_into('<I',zone,0x284,2)
        struct.pack_into('<3I',zone,0x300,*[rng.getrandbits(32) for _ in range(3)])
        globals_=bytearray(256);struct.pack_into('<2I',globals_,0x64,0x81100000 if case%2 else 0,0x81200000)
        out=bytearray(rng.randbytes(32));aux=bytearray(rng.randbytes(16))
        position=((case%4)<<8)+[0,1,127,255][(case//4)%4];auxiliary=case%3!=0
        chunks=[(0x81000000,zone),(0x81100000,db),(0x81200000,cam),(0x80065d00,globals_),
                (0x81400000,out),(0x81500000,aux)]
        arrays=[(C.c_uint8*len(data)).from_buffer_copy(data) for _,data in chunks]
        lib.bind_fixture(*arrays);memory={}
        for address,data in chunks:memory.update({address+i:struct.unpack_from('<I',data,i)[0] for i in range(0,len(data),4)})
        def select(regs,mem):
            length=C.c_uint32();assert regs[6]==0
            regs[2]=lib.query(regs[4],regs[5],regs[7],C.byref(length))
        def count(regs,mem):
            length=C.c_uint32();address=lib.query(regs[4],regs[5],0,C.byref(length));regs[2]=length.value if address else 0
        def scalar(regs,mem):regs[2]=lib.scalar(regs[4],regs[5])
        regs=replay(image,0x80028740,{4:0x81100000,5:position,6:0x81400000,
                    7:0x81500000 if auxiliary else 0,29:0x81600000},memory,
                    hooks={0x80032710:select,0x80032edc:count,0x80032df8:scalar},max_steps=2000)
        result=C.c_uint32();assert not lib.run_camera(position,auxiliary,C.byref(result)),case
        assert result.value==regs[2],(case,hex(result.value),hex(regs[2]))
        for index,address,length in [(4,0x81400000,32),(5,0x81500000,16)]:
            expected=b''.join(struct.pack('<I',memory[address+i]) for i in range(0,length,4))
            assert bytes(arrays[index])==expected,(case,index)
    _ctypes.FreeLibrary(lib._handle)
    print('PASS main camera28740: 192 original MIPS traces, fractional wrap/angles, final/empty paths and optional auxiliary output')


def main():
    ids = cb3.all_level_ids()
    with tempfile.TemporaryDirectory(prefix='crash-level-resources-') as folder:
        tmp = Path(folder)
        camera_checks(tmp)
        original = tmp / 'original'
        bad = tmp / 'bad'
        original.mkdir()
        bad.mkdir()
        for level in ids:
            for source in cb3.level_paths(level):
                source = Path(source)
                target = original / source.name
                try:
                    os.link(source, target)
                except OSError:
                    shutil.copyfile(source, target)
        source_nsf, source_nsd = map(Path, cb3.level_paths(ids[0]))
        nsd = source_nsd.read_bytes()
        #1 missing NSD;2 missing NSF;3 truncated NSD;4 page-span overflow;
        #5 NSF length that is not an exact original raw page multiple.
        (bad / 'S0000001.NSF').write_bytes(bytes(65536))
        (bad / 'S0000002.NSD').write_bytes(nsd)
        (bad / 'S0000003.NSD').write_bytes(nsd[:100])
        (bad / 'S0000003.NSF').write_bytes(bytes(65536))
        overflow = bytearray(nsd)
        overflow[0x520:0x524] = (0x7fffffff).to_bytes(4, 'little')
        (bad / 'S0000004.NSD').write_bytes(overflow)
        (bad / 'S0000004.NSF').write_bytes(bytes(65536))
        (bad / 'S0000005.NSD').write_bytes(nsd)
        (bad / 'S0000005.NSF').write_bytes(bytes(65537))
        (tmp / '3ds.h').write_text(SDK)
        main_source = (ROOT / 'source/main.c').read_text()
        declarations = main_source.split('    struct {uint32_t cid;uint8_t soft,total;int valid;} pages', 1)[1]
        declarations = '    struct {uint32_t cid;uint8_t soft,total;int valid;} pages' + declarations.split('    gool_domain_lease mappings', 1)[0]
        owner = next(line for line in main_source.splitlines() if line.startswith('enum { NATIVE_MAIN_UNIMPLEMENTED'))
        owner += '\nstatic struct {\n' + declarations
        owner += '\nstruct {zdat_resources *bank;zdat_resource_lease lease;uint32_t address,reference;} resources[16];'
        owner += '\nzdat_resources miscellaneous;uint32_t resource_reference;\n} s_native;\n'
        helpers = main_source.split('static int native_page_identity', 1)[1].split('static int native_resource_step', 1)[0]
        cache = CACHE_HARNESS.replace('/*PRODUCTION_OWNER*/', owner).replace(
            '/*PRODUCTION_HELPERS*/', 'static int native_page_identity' + helpers)
        harness = HARNESS.replace('int main(int argc,char **argv){', cache + '\nint main(int argc,char **argv){')
        harness = harness.replace(' unsigned count=0;', ' native_cache_check(argv[1]);\n unsigned count=0;')
        (tmp / 'test.c').write_text(harness)
        exe = tmp / 'level_resources.exe'
        subprocess.run([sys.executable, '-m', 'ziglang', 'cc', '-std=c11', '-O3', '-UNDEBUG',
                        '-Wall', '-Wextra', '-Werror', '-I' + str(tmp), '-I' + str(ROOT / 'source'),
                        str(tmp / 'test.c')] + [str(ROOT / 'source' / source) for source in
                         ('level_resources.c', 'nsf_resource.c', 'nsf_stream.c', 'nsf_page.c',
                          'gool_domain.c','zdat_native.c','zdat_resources.c','zdat_runtime.c','zone_transition.c')]
                       + ['-o', str(exe)], check=True)
        subprocess.run([str(exe), str(original), str(bad)] + [str(i) for i in ids], check=True)
    main_source = (ROOT / 'source/main.c').read_text()
    assert 'level_resources_open(' in main_source and 'level_resources_close(' in main_source


if __name__ == '__main__':
    main()
