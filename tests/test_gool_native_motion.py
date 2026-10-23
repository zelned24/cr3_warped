"""Differential native1FAC8 motion controller against original MIPS words."""
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
import cb3
from psx_native import Image,ORIGINAL_SHA256
from test_gool_stack_native import replay

HARNESS=r'''
#include "gool_native_motion.h"
int fixture(uint32_t current,uint32_t target,uint32_t rate,uint32_t factor,
            uint32_t actor,uint8_t *bytes,uint32_t *result) {
 gool_domain domain={0};gool_domain_lease a,b,c;
 uint32_t owner=0x81100000,frame[7]={0};frame[6]=factor;
 int e=gool_domain_map_mutable(&domain,0x80061a7c,&owner,4,&a);if(e)return e;
 e=gool_domain_map_mutable(&domain,owner,frame,sizeof(frame),&b);if(e)return e;
 e=gool_domain_map_mutable(&domain,0x81200000,bytes,1024,&c);if(e)return e;
 return gool_native_motion_angle(&domain,current,target,rate,actor,result);
}
'''

def main():
    image=Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes())
    assert image.sha256==ORIGINAL_SHA256
    rng=random.Random(0x1fac8)
    with tempfile.TemporaryDirectory(prefix='crash-native-motion-') as folder:
        path=Path(folder);(path/'fixture.c').write_text(HARNESS);dll=path/'fixture.dll'
        subprocess.run([sys.executable,'-m','ziglang','cc','-shared','-O3','-Wall','-Wextra','-Werror',
                        '-I',str(ROOT/'source'),str(path/'fixture.c'),
                        str(ROOT/'source/gool_native_motion.c'),str(ROOT/'source/gool_domain.c'),
                        '-o',str(dll)],check=True)
        lib=C.CDLL(str(dll))
        lib.fixture.argtypes=[C.c_uint32]*5+[C.c_void_p,C.POINTER(C.c_uint32)]
        for case in range(4096):
            current=rng.getrandbits(32)
            target=(current+[0,1,0x7ff,0x800,0x801,0xfff,rng.getrandbits(32)][case%7])&0xffffffff
            rate=([0,1,256,1020,0x7fffffff,0x80000000,0xffffffff][case%7]
                  if case%2 else rng.getrandbits(32))
            factor=[0,1,34,101,102,103,0xffffffff,rng.getrandbits(32)][case%8]
            actor=0x81200000 if case%3 else 0
            data=bytearray(rng.randbytes(1024))
            flags=rng.getrandbits(32)
            flags=flags|0x20000000 if case%2 else flags&~0x20000000
            struct.pack_into('<I',data,0xac,flags)
            memory={0x80061a7c:0x81100000,0x81100018:factor}
            memory.update({0x81200000+i:struct.unpack_from('<I',data,i)[0] for i in range(0,1024,4)})
            regs=replay(image,0x8001fac8,{4:current,5:target,6:rate,7:actor},memory)
            actual=(C.c_uint8*1024).from_buffer_copy(data);result=C.c_uint32()
            status=lib.fixture(current,target,rate,factor,actor,actual,C.byref(result))
            expected=b''.join(struct.pack('<I',memory[0x81200000+i]) for i in range(0,1024,4))
            assert status==0 and result.value==regs[2] and bytes(actual)==expected,(case,status,result.value,regs[2])
        _ctypes.FreeLibrary(lib._handle)
    print('PASS: 4096 original1FAC8 native motion angle/flags comparisons')

if __name__=='__main__':main()
