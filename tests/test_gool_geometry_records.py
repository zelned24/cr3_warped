"""Differential selector15 record/no-op paths against original SCUS code."""
import _ctypes
import ctypes as C
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from psx_native import Image,ORIGINAL_SHA256
import cb3
from test_gool_stack_native import replay

HARNESS=r'''
#include "gool_native_geometry.h"
#include "gool_native_bounds.h"
#include "gool_search.h"
#include "zdat_lookup.h"
#include <stdlib.h>
/* These unrelated selector services must never be entered by this fixture. */
int gool_atan2(int32_t a,int32_t b,int32_t *c){(void)a;(void)b;(void)c;abort();}
int gool_search_serial(uint32_t a,uint32_t b,uint32_t c,gool_search_read d,void *e,uint32_t *f)
{(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;abort();}
int gool_search_visit(uint32_t a,uint32_t b,gool_search_read c,void *d,gool_search_predicate e,void *f,uint32_t *g)
{(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;abort();}
int gool_tree_reparent(uint32_t a,uint32_t b,const gool_tree_services *c,uint32_t d)
{(void)a;(void)b;(void)c;(void)d;abort();}
int zdat_lookup_find(zdat_resources *a,uint32_t b,uint32_t c,uint32_t d,int32_t *e,zdat_resource_lease *f)
{(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;abort();}
int zdat_resources_release(zdat_resources *a,zdat_resource_lease b){(void)a;(void)b;abort();}
int gool_native_bounds_svtx(gool_domain *a,uint32_t b,uint32_t c,uint32_t *d)
{(void)a;(void)b;(void)c;(void)d;abort();}
int gool_native_bounds_fallback(gool_domain *a,uint32_t b,uint32_t *c)
{(void)a;(void)b;(void)c;abort();}
int gool_native_bounds_contains(gool_domain *a,uint32_t b,uint32_t c,int *d)
{(void)a;(void)b;(void)c;(void)d;abort();}
int gool_native_bounds_pop(gool_domain *a){(void)a;abort();}
int gool_native_registry_read(void *r,uint32_t a,uint32_t *v)
{return gool_domain_read32(((gool_native_registry*)r)->domain,a,v);}
int gool_native_registry_write(void *r,uint32_t a,uint32_t v)
{return gool_domain_write32(((gool_native_registry*)r)->domain,a,v);}
int fixture(uint8_t *object,uint8_t *records,uint32_t sub,uint32_t operand) {
    gool_domain domain={0};gool_domain_lease a,b;
    gool_native_actor *actors[1]={0};
    gool_native_registry registry={.domain=&domain,.actors=actors};
    int e=gool_domain_map_mutable(&domain,0x81200000,object,1024,&a);if(e)return e;
    e=gool_domain_map_mutable(&domain,0x8006b000,records,4096,&b);if(e)return e;
    e=gool_native_geometry_resolved(&registry,0x81200000,0x1cf00000|(sub<<15),operand,0);
    int f=gool_domain_release(&domain,b);if(f)return f;
    f=gool_domain_release(&domain,a);return f?f:e;
}
'''

def main():
    image=Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes())
    assert image.sha256==ORIGINAL_SHA256
    rng=random.Random(173150)
    node=0x81200000
    with tempfile.TemporaryDirectory(prefix='crash-geometry-records-') as folder:
        path=Path(folder);(path/'fixture.c').write_text(HARNESS)
        dll=path/'fixture.dll'
        subprocess.run([sys.executable,'-m','ziglang','cc','-shared','-O3','-Wall','-Wextra','-Werror',
                        '-I',str(ROOT/'source'),str(path/'fixture.c'),
                        str(ROOT/'source/gool_native_geometry.c'),str(ROOT/'source/gool_domain.c'),
                        str(ROOT/'source/gool_native_engine.c'),
                        '-o',str(dll)],check=True)
        lib=C.CDLL(str(dll));lib.fixture.argtypes=[C.c_void_p,C.c_void_p,C.c_uint32,C.c_uint32]
        subs=[0,1,4,6,7,8,9,11,21,31,19,20]
        for case in range(768):
            sub=subs[case%len(subs)]
            object_data=bytearray(rng.randbytes(1024));records=bytearray(rng.randbytes(4096))
            index=[0,1,39,40,255,0x7fffff,0xffffff][case%7]
            # Include native's unrestricted low-byte record selector.
            argument=((index<<8)|(case%256))&0xffffffff
            struct.pack_into('<I',object_data,0xbc,node+0x280)
            struct.pack_into('<I',object_data,0x180,argument)
            if sub in (19,20) and index<40 and case%3:
                record=0x8006b0e8+index*36+(argument&255)*9
                struct.pack_into('<I',object_data,0xbc,(record+12)&~3)
            # Exercise operand/stack and record/stack aliasing explicitly.
            operand=node+0x180
            if case%5==0:
                operand=node+0x27c
                struct.pack_into('<I',object_data,0x27c,argument)
            memory={node+i:struct.unpack_from('<I',object_data,i)[0] for i in range(0,1024,4)}
            memory.update({0x8006b000+i:struct.unpack_from('<I',records,i)[0] for i in range(0,4096,4)})
            for i in range(21):memory[0x80010318+i*4]=image.word(0x80010318+i*4)
            regs=replay(image,0x80017ca8,{4:node,5:sub,6:argument,29:0x810f0000},memory)
            memory[node+0xd4]=regs[2]
            actual_object=(C.c_uint8*1024).from_buffer_copy(object_data)
            actual_records=(C.c_uint8*4096).from_buffer_copy(records)
            status=lib.fixture(actual_object,actual_records,sub,operand)
            expected_object=b''.join(struct.pack('<I',memory[node+i]) for i in range(0,1024,4))
            expected_records=b''.join(struct.pack('<I',memory[0x8006b000+i]) for i in range(0,4096,4))
            if status or bytes(actual_object)!=expected_object or bytes(actual_records)!=expected_records:
                _ctypes.FreeLibrary(lib._handle)
                raise AssertionError((case,sub,status,'object' if bytes(actual_object)!=expected_object else 'records'))
        _ctypes.FreeLibrary(lib._handle)
    print('PASS: 768 original selector15 packed-record/no-op comparisons')

if __name__=='__main__':main()
