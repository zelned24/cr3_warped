"""Differential original32710 mode1 timing/list selection, not interpolation."""
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
#include "zdat_native.h"
static gool_domain domain;
__declspec(dllexport) int select_exact(uint8_t *bytes,uint32_t size,uint32_t key,
 uint32_t position,uint32_t *address,uint32_t *length) {
 gool_domain_lease lease;
 int e=gool_domain_map_mutable(&domain,0x81000000,bytes,size,&lease);if(e)return e;
 zdat_native source={&domain,8};zdat_native_value out;
 e=zdat_native_select_exact(&source,0x81000000,(int32_t)key,position,&out);
 if(!e){*address=out.address;*length=out.length;}
 int cleanup=gool_domain_release(&domain,lease);return e?e:cleanup;
}
'''

def main():
    image = Image.parse((Path(cb3.ISO) / 'SCUS_942.44').read_bytes())
    assert image.sha256 == ORIGINAL_SHA256
    rng = random.Random(327101)
    with tempfile.TemporaryDirectory(prefix='crash-zdat-exact-') as directory:
        folder = Path(directory)
        (folder / 'wrapper.c').write_text(WRAPPER)
        dll = folder / 'exact.dll'
        subprocess.run([sys.executable, '-m', 'ziglang', 'cc', '-shared', '-O2',
                        '-Wall', '-Wextra', '-Werror', '-I' + str(ROOT / 'source'),
                        str(folder / 'wrapper.c'), str(ROOT / 'source/zdat_native.c'),
                        str(ROOT / 'source/gool_domain.c'), '-o', str(dll)], check=True)
        lib = C.CDLL(str(dll))
        lib.select_exact.argtypes = [C.POINTER(C.c_uint8), C.c_uint32, C.c_uint32,
                                    C.c_uint32, C.POINTER(C.c_uint32), C.POINTER(C.c_uint32)]
        checked = 0
        for case in range(512):
            blob = bytearray(512)
            values = rng.randrange(1, 9)
            variable = bool(case & 1)
            timed = case % 7 != 0
            times = sorted(rng.sample(range(-20, 21), values))
            lengths = [rng.randrange(0, 5) for _ in times]
            if not variable:
                lengths = [lengths[0]] * values
            struct.pack_into('<I', blob, 0, len(blob))
            struct.pack_into('<h', blob, 12, 1)
            struct.pack_into('<hHBBH', blob, 16, 0x13b, 12,
                             3 | (0x40 if variable else 0) | (0x20 if timed else 0), 4, values)
            sizes = lengths if variable else lengths[:1]
            struct.pack_into('<' + 'H' * len(sizes), blob, 24, *sizes)
            if timed:
                struct.pack_into('<' + 'h' * values, blob, 24 + 2 * len(sizes), *times)
            for sample in [times[0] - 1, times[-1] + 1, *times, rng.randrange(-20, 21)]:
                for fraction in (0, 255):
                    position = ((sample << 8) + fraction) & 0xffffffff
                    memory = {0x81000000 + i: struct.unpack_from('<I', blob, i)[0]
                              for i in range(0, len(blob), 4)}
                    sp, count_address = 0x81200000, 0x81300000
                    memory.update({sp + 0x10: count_address, sp + 0x14: 1,
                                   sp + 0x18: 0, sp + 0x1c: 0, sp + 0x20: 0,
                                   count_address: 0xdeadbeef})
                    regs = replay(image, 0x80032710,
                                  {4: 0x81000000, 5: 0x13b, 6: position, 7: 0, 29: sp},
                                  memory, max_steps=4096)
                    address, length = C.c_uint32(99), C.c_uint32(99)
                    data = (C.c_uint8 * len(blob)).from_buffer_copy(blob)
                    assert lib.select_exact(data, len(blob), 0x13b, position,
                                            C.byref(address), C.byref(length)) == 0
                    assert address.value == regs[2], (case, sample, fraction, address.value, regs[2])
                    if address.value:
                        assert length.value == memory[count_address]
                    checked += 1
        if sys.platform == 'win32':
            import _ctypes
            _ctypes.FreeLibrary(lib._handle)
    print(f'PASS exact native32710: {checked} MIPS timing/list comparisons')

if __name__ == '__main__':
    main()
