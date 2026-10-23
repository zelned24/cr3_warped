"""Original1E4BC/20184 differential; matrix GTE is an explicit integer hook."""
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
#include "gool_native_movement.h"
int fixture(uint8_t *object,uint8_t *parameters,uint8_t *previous,uint8_t *globals,uint32_t factor) {
 gool_domain domain={0};gool_domain_lease leases[6];uint32_t owner=0x81100000,frame[7]={0};frame[6]=factor;
 int e=gool_domain_map_mutable(&domain,0x81200000,object,1024,&leases[0]);if(e)return e;
 e=gool_domain_map_mutable(&domain,0x81300000,parameters,24,&leases[1]);if(e)return e;
 e=gool_domain_map_mutable(&domain,0x81400000,previous,12,&leases[2]);if(e)return e;
 e=gool_domain_map_mutable(&domain,0x80065b00,globals,1024,&leases[3]);if(e)return e;
 e=gool_domain_map_mutable(&domain,0x80061a7c,&owner,4,&leases[4]);if(e)return e;
 e=gool_domain_map_mutable(&domain,owner,frame,sizeof(frame),&leases[5]);if(e)return e;
 return gool_native_movement_control(&domain,0x81200000,0x81300000,0x81400000);
}
'''
MASK=0xffffffff
def signed(value):
    return value if value<0x80000000 else value-0x100000000
def half(memory,address):
    value=(memory[address&~3]>>((address&3)*8))&65535
    return value if value<32768 else value-65536
def matrix_hook(regs,memory):
    left,right=regs[4:6]
    a=[[half(memory,left+r*6+c*2) for c in range(3)] for r in range(3)]
    b=[[half(memory,right+r*6+c*2) for c in range(3)] for r in range(3)]
    result=[max(-32768,min(32767,sum(a[r][k]*b[k][c] for k in range(3))>>12))&65535
            for r in range(3) for c in range(3)]
    for i,value in enumerate(result):
        address=left+i*2;shift=(address&3)*8
        memory[address&~3]=(memory.get(address&~3,0)&~(65535<<shift))|(value<<shift)

def main():
    image=Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes());assert image.sha256==ORIGINAL_SHA256
    rng=random.Random(0x1e4bc)
    with tempfile.TemporaryDirectory(prefix='crash-native-movement-') as folder:
        path=Path(folder);(path/'fixture.c').write_text(HARNESS);dll=path/'fixture.dll'
        subprocess.run([sys.executable,'-m','ziglang','cc','-shared','-O3','-Wall','-Wextra','-Werror',
            '-I',str(ROOT/'source'),str(path/'fixture.c'),
            *[str(ROOT/'source'/name) for name in ['gool_native_movement.c','gool_native_motion.c',
                'gool_native_matrix.c','gool_trig.c','gool_domain.c']],'-o',str(dll)],check=True)
        lib=C.CDLL(str(dll));lib.fixture.argtypes=[C.c_void_p]*4+[C.c_uint32]
        table={address:image.word(address) for address in range(0x8005c91c,0x8005d120,4)}
        for case in range(1024):
            obj=bytearray(rng.randbytes(1024));parameters=bytearray(rng.randbytes(24))
            previous=bytearray(rng.randbytes(12));globals=bytearray(rng.randbytes(1024))
            strength=[0,152,153,256,0xffffffff,rng.getrandbits(32)][case%6]
            direction=[8,0,7,rng.getrandbits(32)][case%4]
            struct.pack_into('<I',globals,0x108,strength);struct.pack_into('<I',globals,0x10c,direction)
            factor=[0,34,102,103,0xffffffff,rng.getrandbits(32)][case%6]
            memory=dict(table);memory.update({0x80061a7c:0x81100000,0x81100018:factor})
            for address,data in [(0x81200000,obj),(0x81300000,parameters),(0x81400000,previous),(0x80065b00,globals)]:
                memory.update({address+i:struct.unpack_from('<I',data,i)[0] for i in range(0,len(data),4)})
            replay(image,0x8001e4bc,{4:0x81200000,5:0x81300000,6:0x81400000,29:0x810f0000},memory,
                   hooks={0x8004f404:matrix_hook},max_steps=2000)
            arrays=[(C.c_uint8*len(data)).from_buffer_copy(data) for data in [obj,parameters,previous,globals]]
            status=lib.fixture(*arrays,factor)
            expected=b''.join(struct.pack('<I',memory[0x81200000+i]) for i in range(0,1024,4))
            if status or bytes(arrays[0])!=expected:
                differences=[(hex(i),hex(struct.unpack_from('<I',bytes(arrays[0]),i)[0]),hex(memory[0x81200000+i]))
                             for i in range(0,1024,4) if bytes(arrays[0])[i:i+4]!=expected[i:i+4]]
                raise AssertionError((case,status,differences))
        _ctypes.FreeLibrary(lib._handle)
    print('PASS: 1024 original1E4BC/20184 movement/controller comparisons with explicit GTE matrix hook')

if __name__=='__main__':main()
