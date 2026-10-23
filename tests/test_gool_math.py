"""Compare compiled scalar/angular approach against original MIPS instructions.

The fixture interpreter includes delay slots and integer wrapping. It does not
model PS1 timing, the world scheduler, or a default frame-factor service.
"""
import ctypes
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest
from host_native_sources import all_sources

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from psx_native import Image, ORIGINAL_SHA256
from test_gool_stack_native import replay, MASK
import cb3
from gool_vm import VM, VMError

VM_WRAPPER = r'''
#include "gool_vm.h"
#include "gool_math.h"
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif
static void pack(uint8_t *p,uint32_t n) {
    for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(i*8));
}
static int read_member(void *ctx,const gool_operand *arg,int32_t *value) {
    if(arg->kind!=GOOL_ARG_VARIABLE||arg->value<0x25||arg->value>0x26)
        return GOOL_ARG_UNBOUND;
    *value=((int32_t *)ctx)[arg->value-0x25];return 0;
}
API int scalar(int c,int t,int r){return gool_math_approach(c,t,r);}
API int angle(int c,int t,int r,int f){return gool_math_approach_angle(c,t,r,f);}
API int dispatch(int32_t c,int32_t t,int32_t r,int32_t f,
                 uint32_t op,uint32_t kind,int32_t *out) {
    uint8_t code[4],constants[8];
    uint32_t mode=kind&3u;
    uint32_t low=mode==1?0xe25u:mode==2?0xe1fu:0;
    uint32_t high=mode==1?0xe26u:mode==2?0xe1fu:mode==3?0xbf0u:1;
    pack(code,op<<24|high<<12|low);pack(constants,(uint32_t)c);pack(constants+4,(uint32_t)t);
    gool_data data={.code=code,.code_words=1,.constants=constants,
                    .constant_words=2,.stack_start=64};
    int32_t members[2]={c,t};
    gool_operand_context ctx={.base=&data,.read=read_member,.context=members};
    gool_vm vm;
    int status=gool_vm_init(&vm,&data,0,&ctx,NULL,0);
    if(status)return status;
    if(!(kind&0x100))gool_vm_bind_frame_factor(&vm,&f);
    if(mode==2){gool_stack_push(&vm.stack,c);gool_stack_push(&vm.stack,t);}
    if(mode==3){gool_stack_push(&vm.stack,t);gool_stack_push(&vm.stack,r);}
    status=gool_vm_step(&vm);
    out[1]=(int32_t)gool_stack_height(&vm.stack);out[2]=(int32_t)vm.pc;
    if(!status)status=gool_stack_pop(&vm.stack,&out[0]);
    return status;
}
'''


def signed(value):
    return ctypes.c_int32(value).value


class NativeApproach(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = Image.parse((Path(cb3.ISO) / 'SCUS_942.44').read_bytes())
        if cls.image.sha256 != ORIGINAL_SHA256:
            raise AssertionError('wrong original executable')
        cls.temp = tempfile.TemporaryDirectory(prefix='crash-gool-math-')
        folder = Path(cls.temp.name)
        wrapper = folder / 'export.c'
        wrapper.write_text(VM_WRAPPER)
        library = folder / ('math.dll' if os.name == 'nt' else 'math.so')
        subprocess.run([sys.executable, '-m', 'ziglang', 'cc', '-shared', '-O3',
            '-ffast-math', '-fno-finite-math-only', '-Wall', '-Wextra', '-Werror',
            '-I'+str(ROOT/'source'), str(wrapper),
            *all_sources(),
            '-o', str(library)], check=True)
        cls.lib = ctypes.CDLL(str(library))
        cls.lib.scalar.argtypes = [ctypes.c_int32] * 3
        cls.lib.angle.argtypes = [ctypes.c_int32] * 4
        cls.lib.dispatch.argtypes = [ctypes.c_int32]*4+[ctypes.c_uint32]*2+[ctypes.POINTER(ctypes.c_int32)]

    @classmethod
    def tearDownClass(cls):
        if os.name == 'nt':
            import _ctypes
            _ctypes.FreeLibrary(cls.lib._handle)
        cls.temp.cleanup()

    def compare(self, opcode, current, target, rate, factor, kind):
        obj, constants, sp, fp = 0x80070000, 0x80090000, 0x800b0000, 0x800a0000
        low, high = {'constant': (0, 1), 'member': (0xe25, 0xe26),
                     'stack': (0xe1f, 0xe1f), 'custom': (0, 0xbf0)}[kind]
        instruction = opcode << 24 | high << 12 | low
        memory = {constants: current & MASK, constants+4: target & MASK,
                  obj+0xd4: current & MASK, obj+0xd8: target & MASK,
                  sp-8: target & MASK if kind == 'custom' else current & MASK,
                  sp-4: rate & MASK if kind == 'custom' else target & MASK,
                  fp+0x54: factor & MASK}
        regs = replay(self.image, self.image.word(0x8005d120+opcode*4),
            {5: instruction, 16: obj, 20: constants, 22: sp, 30: fp}, memory,
            stops=(0x80038e80,))
        expected_sp = sp-4 if kind in ('custom', 'stack') else sp+4
        self.assertEqual(regs[22], expected_sp)
        actual_rate = rate if kind == 'custom' else 256
        actual = (self.lib.scalar(current, target, actual_rate) if opcode == 0x22
                  else self.lib.angle(current, target, actual_rate, factor))
        self.assertEqual(actual, signed(memory[expected_sp-4]),
            (hex(opcode), current, target, rate, factor, kind))
        mode=('constant','member','stack','custom').index(kind)
        output=(ctypes.c_int32*3)()
        self.assertEqual(self.lib.dispatch(current,target,rate,factor,opcode,mode,output),0)
        self.assertEqual(list(output),[actual,1,1])
        model=VM([instruction],constants=[current,target],members={0x25:current,0x26:target},
                 frame_factor=[factor])
        if mode==2:model.stack.push(current);model.stack.push(target)
        if mode==3:model.stack.push(target);model.stack.push(rate)
        self.assertEqual(model.step(),'running')
        self.assertEqual(model.stack.pop(),actual)

    def test_unbound_frame_factor_is_error_after_operand_consumption(self):
        output=(ctypes.c_int32*3)()
        self.assertEqual(self.lib.dispatch(1,2,3,4,0x25,0x103,output),-12)
        self.assertEqual(list(output)[1:],[0,0])
        vm=VM([0x25bf0000],constants=[1]);vm.stack.push(2);vm.stack.push(3)
        with self.assertRaisesRegex(VMError,'frame factor'):vm.step()
        self.assertEqual(vm.stack.height,0)

    def test_16384_original_dispatch_comparisons(self):
        rng = random.Random(0x942442225)
        edges = [0, 1, -1, 255, 256, -256, 1024, 2047, 2048, 4095, 4096,
                 -4096, 0x7fffffff, -0x80000000, 0x40000000, -0x40000000]
        for op in (0x22, 0x25):
            for kind in ('constant', 'member', 'stack', 'custom'):
                for case in range(2048):
                    current = edges[case % 16] if case < 256 else signed(rng.getrandbits(32))
                    target = edges[(case // 16) % 16] if case < 256 else signed(rng.getrandbits(32))
                    rate = edges[(case // 3) % 16] if case < 256 else signed(rng.getrandbits(32))
                    factor = edges[(case // 7) % 16] if case < 256 else signed(rng.getrandbits(32))
                    self.compare(op, current, target, rate, factor, kind)

    def test_evidence_matches_original_bytes(self):
        evidence = json.loads((ROOT/'tests/gool_math_native_evidence.json').read_text())
        self.assertEqual(evidence['source_sha256'], self.image.sha256)
        for window in evidence['windows'].values():
            self.assertEqual(self.image.read(window['start'], window['end']-window['start']).hex(),
                             window['raw_hex'])
        self.assertEqual(self.image.word(0x8005d120+0x22*4), 0x80039480)
        self.assertEqual(self.image.word(0x8005d120+0x25*4), 0x80039518)


if __name__ == '__main__':
    unittest.main()
