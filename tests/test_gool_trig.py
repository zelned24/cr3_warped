"""Compile original integer trig and compare actual MIPS dispatch/store paths."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import unittest
from host_native_sources import all_sources

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import cb3
from extract_gool_trig import table_source
from psx_native import Image, ORIGINAL_SHA256
from test_gool_stack_native import replay, MASK

VM_WRAPPER = r'''
#include "gool_vm.h"
#include "gool_trig.h"
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif
typedef struct { int32_t source,destination; uint32_t trace; } members;
static void pack(uint8_t *p,uint32_t n) {
    for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));
}
static int read_member(void *context,const gool_operand *arg,int32_t *value) {
    members *m=context;
    if(arg->kind!=GOOL_ARG_VARIABLE||arg->value!=0x26)return GOOL_ARG_UNBOUND;
    m->trace=(m->trace<<8)|0x26;*value=m->source;return 0;
}
static int write_member(void *context,const gool_operand *arg,int32_t value) {
    members *m=context;
    if(arg->kind!=GOOL_ARG_VARIABLE||arg->value!=0x25)return GOOL_ARG_UNBOUND;
    m->trace=(m->trace<<8)|0x25;m->destination=value;return 0;
}
API int sine(int a){return gool_trig_sin(a);}
API int cosine(int a){return gool_trig_cos(a);}
API int dispatch(int32_t angle,uint32_t opcode,uint32_t kind,int32_t *out) {
    uint32_t low=kind==1?0xe1fu:kind==4?0xe27u:0xe25u;
    uint32_t high=kind==1||kind==4?0xe1fu:kind==2?0xe26u:kind==3?0xe27u:0;
    uint8_t code[4],constant[4];
    pack(code,opcode<<24|high<<12|low);pack(constant,(uint32_t)angle);
    gool_data data={.code=code,.code_words=1,.constants=constant,
                    .constant_words=1,.stack_start=64};
    members m={.source=angle,.destination=12345};
    gool_operand_context ctx={.base=&data,.context=&m,
                              .read=read_member,.write=write_member};
    gool_vm vm;
    int status=gool_vm_init(&vm,&data,0,&ctx,NULL,0);
    if(status)return status;
    if(kind==1||kind==4)gool_stack_push(&vm.stack,angle);
    status=gool_vm_step(&vm);
    out[0]=m.destination;out[1]=(int32_t)gool_stack_height(&vm.stack);
    out[2]=(int32_t)vm.pc;out[3]=(int32_t)m.trace;
    if(!status&&kind==1)status=gool_stack_pop(&vm.stack,&out[0]);
    return status;
}
'''


class NativeTrig(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes())
        if cls.image.sha256 != ORIGINAL_SHA256:
            raise AssertionError('wrong original executable')
        cls.temp = tempfile.TemporaryDirectory(prefix='crash-native-trig-')
        folder = Path(cls.temp.name)
        wrapper = folder/'exports.c'
        wrapper.write_text(VM_WRAPPER)
        library = folder/('trig.dll' if os.name == 'nt' else 'trig.so')
        subprocess.run([sys.executable, '-m', 'ziglang', 'cc', '-shared', '-O3',
            '-ffast-math', '-fno-finite-math-only', '-Wall', '-Wextra', '-Werror',
            '-I'+str(ROOT/'source'), str(wrapper),
            *all_sources(),
            '-o', str(library)], check=True)
        cls.lib = ctypes.CDLL(str(library))
        cls.lib.sine.argtypes = cls.lib.cosine.argtypes = [ctypes.c_int32]
        cls.lib.dispatch.argtypes = [ctypes.c_int32, ctypes.c_uint32,
                                      ctypes.c_uint32, ctypes.POINTER(ctypes.c_int32)]
        raw = cls.image.read(0x8005c91c, 2050)
        cls.table_ram = {0x8005c91c+n: int.from_bytes(raw[n:n+4], 'little')
                         for n in range(0, len(raw), 4)}

    @classmethod
    def tearDownClass(cls):
        if os.name == 'nt':
            import _ctypes
            _ctypes.FreeLibrary(cls.lib._handle)
        cls.temp.cleanup()

    def compare(self, angle, opcode, kind):
        obj, constants, sp = 0x80070000, 0x80090000, 0x800b0000
        low, high = {'member': (0xe25, 0), 'stack': (0xe1f, 0xe1f),
                     'source_member': (0xe25, 0xe26)}[kind]
        memory = dict(self.table_ram)
        memory.update({constants: angle & MASK, obj+0xd8: angle & MASK,
                       sp-4: angle & MASK, obj+0xd4: 0xfeedface})
        inst = opcode << 24 | high << 12 | low
        regs = replay(self.image, self.image.word(0x8005d120+4*opcode),
            {5: inst, 16: obj, 20: constants, 22: sp}, memory, stops=(0x80038e80,))
        self.assertEqual(regs[22], sp)
        stored = memory[sp-4 if kind == 'stack' else obj+0xd4]
        result = self.lib.sine(angle) if opcode == 0x2b else self.lib.cosine(angle)
        self.assertEqual(result, ctypes.c_int32(stored).value, (angle, hex(opcode), kind))
        mode = ('member', 'stack', 'source_member').index(kind)
        output = (ctypes.c_int32*4)()
        self.assertEqual(self.lib.dispatch(angle, opcode, mode, output), 0)
        self.assertEqual(list(output), [result, int(kind == 'stack'), 1,
                                       {'member': 0x25, 'stack': 0, 'source_member': 0x2625}[kind]])

    def test_vm_errors_preserve_native_operand_order(self):
        for op in (0x2b, 0x2c):
            output = (ctypes.c_int32*4)()
            self.assertEqual(self.lib.dispatch(1000, op, 3, output), -12)
            self.assertEqual(list(output), [12345, 0, 0, 0])
            self.assertEqual(self.lib.dispatch(1000, op, 4, output), -12)
            self.assertEqual(list(output), [12345, 0, 0, 0])

    def test_all_4096_angles_and_wrapping_native_dispatch(self):
        rng = random.Random(0x942442b2c)
        angles = list(range(4096)) + [-1, -1024, -2048, -4096,
            -0x80000000, 0x7fffffff] + [ctypes.c_int32(rng.getrandbits(32)).value
                                      for _ in range(1024)]
        for opcode in (0x2b, 0x2c):
            for kind in ('member', 'stack', 'source_member'):
                for angle in angles:
                    self.compare(angle, opcode, kind)

    def test_extraction_and_evidence_match_original(self):
        self.assertEqual((ROOT/'source/gool_trig_table.h').read_text(), table_source(self.image))
        evidence = json.loads((ROOT/'tests/gool_trig_native_evidence.json').read_text())
        self.assertEqual(evidence['source_sha256'], self.image.sha256)
        raw = self.image.read(0x8005c91c, 2050)
        self.assertEqual(bytes.fromhex(evidence['quarter_wave']['raw_hex']), raw)
        self.assertEqual(evidence['quarter_wave']['sha256'], hashlib.sha256(raw).hexdigest())
        self.assertEqual(struct.unpack_from('<h', raw, 2048)[0], 4096)
        for window in evidence['windows'].values():
            self.assertEqual(window['raw_hex'],
                self.image.read(window['start'], window['end']-window['start']).hex())

    def test_replay_signed_halfword_and_trapping_negation(self):
        class Instructions:
            def __init__(self, instruction):
                self.instructions = {0x2000: instruction, 0x2004: 0x03e00008, 0x2008: 0}
            def word(self, address):
                return self.instructions[address]
        for offset, expected in ((0, -32768), (2, -2)):
            regs = replay(Instructions(0x84a40000+offset), 0x2000,
                          {5: 0x3000}, {0x3000: 0xfffe8000})
            self.assertEqual(ctypes.c_int32(regs[4]).value, expected)
        regs = replay(Instructions(0x00042022), 0x2000, {4: -4096}, {})
        self.assertEqual(regs[4], 4096)
        with self.assertRaisesRegex(AssertionError, 'SUB overflow'):
            replay(Instructions(0x00042022), 0x2000, {4: -0x80000000}, {})


if __name__ == '__main__':
    unittest.main()
