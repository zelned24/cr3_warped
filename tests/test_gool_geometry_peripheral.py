"""Original17CA8 overlay ABI; overlay bodies are explicit fixture boundaries."""
import ctypes as C
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import test_gool_geometry_records as base

EXTRA=r'''
static uint32_t calls[16],used;
static int peripheral(void *ctx,uint32_t entry,const uint32_t *args,uint32_t n,uint32_t *result) {
 (void)ctx;calls[used++]=entry;calls[used++]=n;
 for(uint32_t i=0;i<n;i++)calls[used++]=args[i];
 *result=entry^0x12345678u;return 0;
}
uint32_t call_word(uint32_t index){return calls[index];}
uint32_t call_count(void){return used;}
int peripheral_fixture(uint8_t *object,uint8_t *records,uint32_t sub) {
 gool_domain domain={0};gool_domain_lease a,b;gool_native_actor *actors[1]={0};
 gool_native_registry registry={.domain=&domain,.actors=actors};
 int e=gool_domain_map_mutable(&domain,0x81200000,object,1024,&a);if(e)return e;
 e=gool_domain_map_mutable(&domain,0x8006b000,records,4096,&b);if(e)return e;
 gool_native_geometry_services services={.peripheral=peripheral,.peripheral_owner_address=0x8006b090};
 used=0;return gool_native_geometry_resolved(&registry,0x81200000,0x1cf00000|(sub<<15),0x81200180,&services);
}
'''

def main():
    image=base.Image.parse((Path(base.cb3.ISO)/'SCUS_942.44').read_bytes())
    assert image.sha256==base.ORIGINAL_SHA256
    rng=random.Random(1700)
    with tempfile.TemporaryDirectory(prefix='crash-native-peripheral-') as folder:
        path=Path(folder);(path/'fixture.c').write_text(base.HARNESS+EXTRA);dll=path/'fixture.dll'
        subprocess.run([sys.executable,'-m','ziglang','cc','-shared','-O3','-Wall','-Wextra','-Werror',
                        '-I',str(base.ROOT/'source'),str(path/'fixture.c'),
                        str(base.ROOT/'source/gool_native_geometry.c'),str(base.ROOT/'source/gool_domain.c'),
                        str(base.ROOT/'source/gool_native_engine.c'),
                        '-o',str(dll)],check=True)
        lib=C.CDLL(str(dll));lib.peripheral_fixture.argtypes=[C.c_void_p,C.c_void_p,C.c_uint32]
        lib.call_word.restype=C.c_uint32
        counts={0x80071590:1,0x800717d8:1,0x800718d0:0,0x80071930:0,0x80071974:0,
                0x80071c8c:1,0x80072094:1,0x80071abc:2,0x800719d8:0,0x80071d38:1,
                0x80071da8:1,0x80071880:1}
        for case in range(440):
            sub=[2,3,5,10,12,13,14,15,16,17,18][case%11]
            value=rng.getrandbits(32);owner=0 if case%3==0 else rng.getrandbits(32)
            obj=bytearray(1024);rec=bytearray(4096)
            struct.pack_into('<I',obj,0x180,value);struct.pack_into('<I',rec,0x90,owner)
            memory={0x8006b090:owner}
            memory.update({0x80010318+n*4:image.word(0x80010318+n*4) for n in range(21)})
            expected=[]
            def hook(entry):
                def invoke(regs,memory):
                    count=counts[entry];expected.extend([entry,count]+regs[4:4+count])
                    regs[2]=entry^0x12345678
                return invoke
            regs=base.replay(image,0x80017ca8,{4:0x81200000,5:sub,6:value,28:0x8006b000,29:0x810f0000},
                             memory,hooks={entry:hook(entry) for entry in counts})
            actual_obj=(C.c_uint8*1024).from_buffer_copy(obj)
            actual_rec=(C.c_uint8*4096).from_buffer_copy(rec)
            status=lib.peripheral_fixture(actual_obj,actual_rec,sub)
            actual=[lib.call_word(n) for n in range(lib.call_count())]
            assert status==0 and actual==expected,(case,status,actual,expected)
            assert struct.unpack_from('<I',bytes(actual_obj),0xd4)[0]==regs[2],case
        base._ctypes.FreeLibrary(lib._handle)
    print('PASS: 440 original17CA8 peripheral ABI/owner/return comparisons')

if __name__=='__main__':main()
