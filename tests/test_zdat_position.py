"""Replay the original path position instruction block against production C."""
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
import zdat
from psx_native import Image,ORIGINAL_SHA256
from test_gool_stack_native import replay

WRAPPER=r'''
#include <assert.h>
#include <string.h>
#include "zdat_position.h"
void check_view(void) {
    uint8_t data[36]={36,0,0,0};data[12]=1;
    data[16]=0x4b;data[18]=12;data[20]=6;data[21]=6;data[22]=1;data[24]=1;
    data[28]=0xff;data[29]=0xff;data[30]=2;data[32]=3;
    zdat_database database;assert(!zdat_database_bind(&database,(zdat_span){data,36}));
    zdat_entity entity={.shift=4};assert(!zdat_series_at(&database,0,&entity.path));
    zdat_zone zone={.has_offset=1,.offset={100,200,300}};
    int32_t output[3]={7,8,9};assert(!zdat_entity_position_native(&zone,&entity,0,output));
    assert(output[0]==84*256&&output[1]==232*256&&output[2]==348*256);
    int32_t before[3];memcpy(before,output,sizeof(before));
    assert(zdat_entity_position_native(&zone,&entity,1,output)==ZDAT_RANGE);
    assert(!memcmp(output,before,sizeof(before)));
    zone.has_offset=0;assert(zdat_entity_position_native(&zone,&entity,0,output)==ZDAT_INVALID);
    assert(!memcmp(output,before,sizeof(before)));zone.has_offset=1;
    entity.path.format=7;assert(zdat_entity_position_native(&zone,&entity,0,output)==ZDAT_FORMAT);
    assert(!memcmp(output,before,sizeof(before)));
    int16_t point[3]={-32768,0,32767};
    int32_t offsets[3]={INT32_MAX,INT32_MIN,-1},expected[3];
    assert(!zdat_position_native(point,offsets,31,expected));
    assert(!zdat_position_native(point,offsets,31,offsets));
    assert(!memcmp(offsets,expected,sizeof(offsets)));
}
'''

def main():
    image=Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes())
    assert image.sha256==ORIGINAL_SHA256
    with tempfile.TemporaryDirectory(prefix='crash-path-position-') as directory:
        tmp=Path(directory);dll=tmp/'position.dll'
        (tmp/'wrapper.c').write_text(WRAPPER)
        subprocess.run([sys.executable,'-m','ziglang','cc','-shared','-O3','-UNDEBUG',
                        '-Wall','-Wextra','-Werror','-I'+str(ROOT/'source'),str(tmp/'wrapper.c'),
                        str(ROOT/'source/zdat_position.c'),str(ROOT/'source/zdat_runtime.c'),
                        '-o',str(dll)],check=True)
        lib=C.CDLL(str(dll))
        lib.zdat_position_native.argtypes=[C.POINTER(C.c_int16),C.POINTER(C.c_int32),C.c_uint32,C.POINTER(C.c_int32)]
        lib.check_view()
        checked=0
        def compare(point,offset,shift):
            nonlocal checked
            packed=struct.pack('<3h',*point)+bytes(2)
            memory={0x81000000+i:struct.unpack_from('<I',packed,i)[0] for i in (0,4)}
            memory.update({0x81000100+i*4:n&0xffffffff for i,n in enumerate(offset)})
            replay(image,0x800207b0,{6:0x81000000,30:0x81000100,22:shift,21:0x81000200},
                   memory,stops=(0x800207f8,))
            output=(C.c_int32*3)(7,8,9)
            assert lib.zdat_position_native((C.c_int16*3)(*point),(C.c_int32*3)(*offset),shift,output)==0
            assert [n&0xffffffff for n in output]==[memory[0x81000200+i*4] for i in range(3)]
            checked+=1
        for level in cb3.all_level_ids():
            archive=cb3.load_level(level,cache=False)
            for entry in archive.of_type(7):
                zone=zdat.parse_zdat(entry)
                assert zone.offset_vector is not None
                for entity in zone.objects:
                    for point in entity.path:
                        compare(point,zone.offset_vector,entity.path_scale_shift)
        original=checked;rng=random.Random(207176)
        for case in range(4096):
            compare(tuple(rng.randint(-32768,32767) for _ in range(3)),
                    tuple(rng.randint(-(1<<31),(1<<31)-1) for _ in range(3)),rng.getrandbits(32))
        _ctypes.FreeLibrary(lib._handle)
    print(f'PASS native path position: {original} original points +4096 wrapping/variable-shift MIPS cases; bounded views and aliasing')

if __name__=='__main__':main()
