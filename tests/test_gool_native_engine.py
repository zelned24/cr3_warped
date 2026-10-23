"""Original engine scalar bootstrap and full1AB64 scratch publication replay."""
import ctypes as C
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import cb3
from psx_native import Image,ORIGINAL_SHA256
from test_gool_stack_native import replay

WRAPPER=r'''
#include <assert.h>
#include "gool_native_engine.h"
static gool_domain domain;
__declspec(dllexport) int run(uint8_t *globals,uint8_t *random,uint8_t *scratch,uint8_t *timer) {
 gool_domain_lease leases[6];uint32_t pointer=0x81000000;
 assert(!gool_domain_map_mutable(&domain,0x80068e98,globals,0x400,&leases[0]));
 assert(!gool_domain_map_mutable(&domain,0x80060a2c,random,8,&leases[1]));
 assert(!gool_domain_map_mutable(&domain,0x1f800050,scratch,0x158,&leases[2]));
 assert(!gool_domain_map_mutable(&domain,0x80061a7c,&pointer,4,&leases[3]));
 assert(!gool_domain_map_mutable(&domain,0x81000000,timer,0x20,&leases[4]));
 assert(!gool_native_engine_map_dispatch(&domain,&leases[5]));
 int e=gool_native_engine_defaults(&domain);
 if(!e)e=gool_native_engine_random_init(&domain);
 if(!e)e=gool_native_engine_prepare(&domain);
 for(unsigned i=6;i;i--)assert(!gool_domain_release(&domain,leases[i-1]));
 assert(gool_domain_empty(&domain));return e;
}
__declspec(dllexport) int level_run(uint8_t *globals,uint8_t *gp,int tail) {
 gool_domain_lease leases[2];
 assert(!gool_domain_map_mutable(&domain,0x80068e98,globals,0x400,&leases[0]));
 assert(!gool_domain_map_mutable(&domain,0x80060ab0,gp,8,&leases[1]));
 int e=tail?gool_native_engine_frame_flags(&domain):gool_native_engine_level_begin(&domain);
 for(unsigned i=2;i;i--)assert(!gool_domain_release(&domain,leases[i-1]));
 assert(gool_domain_empty(&domain));return e;
}
__declspec(dllexport) int game_run(uint8_t *globals,uint8_t *save,uint8_t *usage,unsigned mode) {
 gool_domain_lease leases[3];
 assert(!gool_domain_map_mutable(&domain,0x80068e98,globals,0x400,&leases[0]));
 assert(!gool_domain_map_mutable(&domain,0x8006c2d8,save,0x200,&leases[1]));
 assert(!gool_domain_map_mutable(&domain,0x8006aee8,usage,0x200,&leases[2]));
 int e=gool_native_engine_new_game(&domain,mode);
 for(unsigned i=3;i;i--)assert(!gool_domain_release(&domain,leases[i-1]));
 assert(gool_domain_empty(&domain));return e;
}
__declspec(dllexport) int camera_run(uint8_t *globals,uint8_t *camera) {
 gool_domain_lease leases[2];
 assert(!gool_domain_map_mutable(&domain,0x80068e98,globals,0x400,&leases[0]));
 assert(!gool_domain_map_mutable(&domain,0x80065bc8,camera,0x300,&leases[1]));
 int e=gool_native_engine_camera_reset(&domain);
 for(unsigned i=2;i;i--)assert(!gool_domain_release(&domain,leases[i-1]));
 assert(gool_domain_empty(&domain));return e;
}
'''

def main():
    image=Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes());assert image.sha256==ORIGINAL_SHA256
    rng=random.Random(0x1ab64)
    with tempfile.TemporaryDirectory(prefix='crash-engine-') as directory:
        folder=Path(directory);(folder/'check.c').write_text(WRAPPER);dll=folder/'check.dll'
        subprocess.run([sys.executable,'-m','ziglang','cc','-shared','-O2','-UNDEBUG',
                        '-Wall','-Wextra','-Werror','-I'+str(ROOT/'source'),str(folder/'check.c'),
                        str(ROOT/'source/gool_native_engine.c'),str(ROOT/'source/gool_domain.c'),
                        '-o',str(dll)],check=True)
        lib=C.CDLL(str(dll));lib.run.argtypes=[C.POINTER(C.c_uint8)]*4
        lib.level_run.argtypes=[C.POINTER(C.c_uint8),C.POINTER(C.c_uint8),C.c_int]
        for case in range(512):
            arrays=[bytearray(rng.randbytes(n)) for n in (0x400,8,0x158,0x20)]
            factor=[0,1,66,102,103,0xffffffff,rng.getrandbits(32)][case%7]
            struct.pack_into('<I',arrays[3],0x18,factor)
            memory={0x80061a7c:0x81000000}
            bases=(0x80068e98,0x80060a2c,0x1f800050,0x81000000)
            for base,data in zip(bases,arrays):
                memory.update({base+i:struct.unpack_from('<I',data,i)[0] for i in range(0,len(data),4)})
            memory.update({0x8005d120+i:image.word(0x8005d120+i) for i in range(0,0x300,4)})
            replay(image,0x8002fc50,{},memory)
            replay(image,0x8003079c,{},memory)
            replay(image,0x8001ab64,{29:0x81200000},memory,max_steps=4096)
            actual=[(C.c_uint8*len(data)).from_buffer_copy(data) for data in arrays]
            assert lib.run(*actual)==0
            for base,data in zip(bases,actual):
                expected=b''.join(struct.pack('<I',memory[base+i]) for i in range(0,len(data),4))
                assert bytes(data)==expected,(case,hex(base))
        for case in range(512):
            arrays=[bytearray(rng.randbytes(n)) for n in (0x400,8)]
            bases=(0x80068e98,0x80060ab0)
            memory={}
            for base,data in zip(bases,arrays):
                memory.update({base+i:struct.unpack_from('<I',data,i)[0] for i in range(0,len(data),4)})
            # Stop before the prefix's first dependent load/call. The ordinary
            # resource branch is exercised from its real function entry.
            memory.update({0x80061040:0x81100000,0x80061044:0x81100020,
                           0x81100000:0x12345678})
            replay(image,0x80021084,{28:0x80060878,29:0x81200000},memory,stops=(0x800210e8,))
            actual=[(C.c_uint8*len(data)).from_buffer_copy(data) for data in arrays]
            assert lib.level_run(*actual,0)==0
            for base,data in zip(bases,actual):
                expected=b''.join(struct.pack('<I',memory[base+i]) for i in range(0,len(data),4))
                assert bytes(data)==expected,('level prefix',case,hex(base))
            flags=rng.getrandbits(32)
            struct.pack_into('<I',actual[0],0x70,flags);memory[0x80068f08]=flags
            replay(image,0x80016cac,{},memory,stops=(0x80016cc0,))
            assert lib.level_run(*actual,1)==0
            expected=b''.join(struct.pack('<I',memory[0x80068e98+i]) for i in range(0,0x400,4))
            assert bytes(actual[0])==expected,('frame flags',case)
        lib.game_run.argtypes=[C.POINTER(C.c_uint8)]*3+[C.c_uint]
        lib.camera_run.argtypes=[C.POINTER(C.c_uint8)]*2
        def clear(regs,memory):
            assert regs[5]==0 and regs[6]==0x200
            for offset in range(0,regs[6],4):memory[regs[4]+offset]=0
            regs[2]=regs[4]
        for case in range(384):
            arrays=[bytearray(rng.randbytes(n)) for n in (0x400,0x200,0x200)]
            bases=(0x80068e98,0x8006c2d8,0x8006aee8);memory={}
            for base,data in zip(bases,arrays):
                memory.update({base+i:struct.unpack_from('<I',data,i)[0] for i in range(0,len(data),4)})
            mode=case%3
            replay(image,0x8002fca0,{4:mode,29:0x81200000},memory,
                   hooks={0x80048100:clear},max_steps=1024)
            actual=[(C.c_uint8*len(data)).from_buffer_copy(data) for data in arrays]
            assert lib.game_run(*actual,mode)==0
            for base,data in zip(bases,actual):
                expected=b''.join(struct.pack('<I',memory[base+i]) for i in range(0,len(data),4))
                assert bytes(data)==expected,('new game',case,hex(base))
        for case in range(192):
            arrays=[bytearray(rng.randbytes(n)) for n in (0x400,0x300)]
            bases=(0x80068e98,0x80065bc8);memory={}
            for base,data in zip(bases,arrays):
                memory.update({base+i:struct.unpack_from('<I',data,i)[0] for i in range(0,len(data),4)})
            calls=[]
            def renderer(regs,memory):calls.append((regs[4],regs[5]))
            replay(image,0x800188ec,{29:0x81200000},memory,
                   hooks={0x8004f724:renderer,0x8004f704:renderer},max_steps=1024)
            assert calls[0][0]==0x120 and calls[1]==(0,0)
            actual=[(C.c_uint8*len(data)).from_buffer_copy(data) for data in arrays]
            assert lib.camera_run(*actual)==0
            for base,data in zip(bases,actual):
                expected=b''.join(struct.pack('<I',memory[base+i]) for i in range(0,len(data),4))
                assert bytes(data)==expected,('camera reset',case,hex(base))
        if sys.platform=='win32':
            import _ctypes
            _ctypes.FreeLibrary(lib._handle)
    print('PASS engine bootstrap:512 original2FC50/3079C/1AB64 traces, actual dispatch copy, clamp and untouched bytes')
    print('PASS new game:384 original2FCA0 traces; camera:192 original188EC/18DA8 traces')
    print('PASS level flags:512 original21084 prefixes and16CAC frame restores, with surrounding bytes preserved')

if __name__=='__main__':main()
