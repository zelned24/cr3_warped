"""Original1AC74/1ACAC/1AD68 differential replay with explicit query fixtures."""
import ctypes as C
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import cb3
from psx_native import Image, ORIGINAL_SHA256
from test_gool_stack_native import replay

WRAPPER = r'''
#include "gool_native_activation_path.h"
static gool_domain domain;
static uint32_t calls,queries[6],lists[2],counts[2],pending,query_active;
static int query(void *ctx,uint32_t database,uint32_t key,uint32_t position,uint32_t *list,uint32_t *count) {
 (void)ctx;if(calls>=2||query_active)return -900;
 queries[calls*3]=database;queries[calls*3+1]=key;queries[calls*3+2]=position;
 *list=lists[calls];*count=counts[calls];calls++;query_active=1;pending=2;return 1;
}
static int query_step(void *ctx,uint32_t budget){(void)ctx;(void)budget;return pending--?1:0;}
static int cancel(void *ctx){(void)ctx;query_active=0;return 0;}
__declspec(dllexport) int run(uint8_t *memory,uint32_t position,uint32_t direction,uint32_t count0,uint32_t count1) {
 gool_domain_lease lease;int e=gool_domain_map_mutable(&domain,0x80067000,memory,0x4000,&lease);if(e)return e;
 gool_native_activation reset={0};e=gool_native_activation_reset_begin(&reset,&domain,0x80069000,64);
 for(unsigned i=0;e==1&&i<10000;i++)e=gool_native_activation_step(&reset,1);
 gool_native_activation_close(&reset);
 calls=query_active=0;lists[0]=0x8006a000;lists[1]=0x8006a100;counts[0]=count0;counts[1]=count1;
 gool_native_activation_path owner={0};
 gool_native_activation_path_services s={0,query,query_step,cancel};
 if(!e)e=gool_native_activation_path_begin(&owner,&domain,0x80069000,64,0x81000000,position,direction,&s);
 for(unsigned i=0;e==1&&i<100000;i++)e=gool_native_activation_path_step(&owner,1+(i%3));
 if(!e&&calls!=2)e=-901;
 int cleanup=gool_native_activation_path_close(&owner);if(!e)e=cleanup;
 cleanup=gool_domain_release(&domain,lease);return e?e:cleanup;
}
__declspec(dllexport) uint32_t query_word(uint32_t index){return index<6?queries[index]:0;}
'''

def main():
    image = Image.parse((Path(cb3.ISO) / 'SCUS_942.44').read_bytes())
    assert image.sha256 == ORIGINAL_SHA256
    rng = random.Random(0x1ad68)
    with tempfile.TemporaryDirectory(prefix='crash-activation-') as directory:
        folder = Path(directory)
        (folder / 'wrapper.c').write_text(WRAPPER)
        dll = folder / 'activation.dll'
        subprocess.run([sys.executable, '-m', 'ziglang', 'cc', '-shared', '-O2',
                        '-Wall', '-Wextra', '-Werror', '-I' + str(ROOT / 'source'),
                        str(folder / 'wrapper.c'), str(ROOT / 'source/gool_native_activation.c'),
                        str(ROOT / 'source/gool_native_activation_path.c'),
                        str(ROOT / 'source/gool_domain.c'), '-o', str(dll)], check=True)
        lib = C.CDLL(str(dll))
        lib.run.argtypes = [C.POINTER(C.c_uint8), C.c_uint32, C.c_uint32, C.c_uint32, C.c_uint32]
        lib.query_word.argtypes = [C.c_uint32]
        lib.query_word.restype = C.c_uint32
        for case in range(256):
            data = bytearray(0x4000)
            for i in range(64):
                struct.pack_into('<III', data, 0xa38 + i * 12,
                                 rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32))
            struct.pack_into('<I', data, 0x221c, case % 9)
            counts = [rng.randrange(0, 13), rng.randrange(0, 13)]
            for k, count in enumerate(counts):
                for i in range(count):
                    struct.pack_into('<I', data, 0x3000 + k * 0x100 + i * 4,
                                     (rng.randrange(16) << 8) | rng.randrange(256) | (rng.randrange(256) << 24))
            memory = {0x80067000 + i: struct.unpack_from('<I', data, i)[0]
                      for i in range(0, len(data), 4)}
            position = rng.getrandbits(32)
            direction = [0, 1, 2, 3, 0xffffffff][case % 5]
            replay(image, 0x8001ac74, {28: 0x80069000}, memory, max_steps=4096)
            queries = []
            def query(regs, ram):
                k = len(queries)
                queries.extend(regs[4:7])
                index = k // 3
                regs[2] = 0x8006a000 + index * 0x100
                ram[ram[regs[29] + 0x10]] = counts[index]
                assert ram[regs[29] + 0x14] == 1
            replay(image, 0x8001ad68,
                   {4: 0x81000000, 5: position, 6: direction, 28: 0x80069000, 29: 0x81200000},
                   memory, hooks={0x80032710: query}, max_steps=30000)
            actual = (C.c_uint8 * len(data)).from_buffer_copy(data)
            assert lib.run(actual, position, direction, *counts) == 0
            expected = b''.join(struct.pack('<I', memory[0x80067000 + i]) for i in range(0, len(data), 4))
            assert bytes(actual) == expected, case
            assert [lib.query_word(i) for i in range(6)] == queries
        if sys.platform == 'win32':
            import _ctypes
            _ctypes.FreeLibrary(lib._handle)
    print('PASS native activation: 256 reset/path/registration MIPS cases with asynchronous queries')

if __name__ == '__main__':
    main()
