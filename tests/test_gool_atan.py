"""Compare production atan and opcode execution with original MIPS bytes."""
import ctypes as ct
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

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import cb3
from psx_native import Image,ORIGINAL_SHA256
from test_gool_stack_native import replay,MASK
from gool_vm import VM,VMError
from extract_gool_atan import table_source

WRAPPER=r'''
#include "gool_vm.h"
#include "gool_atan.h"
__declspec(dllexport) int angle(int32_t y,int32_t x,int32_t *out){return gool_atan2(y,x,out);}
static void pack(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
__declspec(dllexport) int dispatch(int32_t y,int32_t x,unsigned stack,int32_t *out){
 uint8_t code[4],constants[8];pack(code,stack==2?0x2de1fbe0:stack?0x2de1fe1f:0x2d001000);
 pack(constants,(uint32_t)y);pack(constants+4,(uint32_t)x);
 gool_data data={.code=code,.code_words=1,.constants=constants,.constant_words=2,.stack_start=64};
 gool_operand_context ctx={.base=&data};gool_vm vm;
 int status=gool_vm_init(&vm,&data,0,&ctx,NULL,0);if(status)return status;
 if(stack){if(stack!=2)gool_stack_push(&vm.stack,y);gool_stack_push(&vm.stack,x);}
 status=gool_vm_step(&vm);out[1]=(int32_t)gool_stack_height(&vm.stack);out[2]=(int32_t)vm.pc;
 if(!status)status=gool_stack_pop(&vm.stack,&out[0]);return status;
}
'''


class NativeAtan(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image=Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes())
        assert cls.image.sha256==ORIGINAL_SHA256
        cls.tmp=tempfile.TemporaryDirectory(prefix='crash-atan-')
        p=Path(cls.tmp.name);(p/'export.c').write_text(WRAPPER)
        lib=p/'atan.dll'
        subprocess.run([sys.executable,'-m','ziglang','cc','-shared','-O3','-Wall','-Wextra',
            '-Werror','-I'+str(ROOT/'source'),str(p/'export.c'),
            *all_sources(),'-o',str(lib)],check=True)
        cls.lib=ct.CDLL(str(lib))
        cls.lib.angle.argtypes=[ct.c_int32,ct.c_int32,ct.POINTER(ct.c_int32)]
        cls.lib.dispatch.argtypes=[ct.c_int32,ct.c_int32,ct.c_uint,ct.POINTER(ct.c_int32)]
        raw=cls.image.read(0x8005f6b8,2050)
        cls.ram={0x8005f6b8+i:int.from_bytes(raw[i:i+4],'little') for i in range(0,len(raw),4)}

    @classmethod
    def tearDownClass(cls):
        if os.name=='nt':
            import _ctypes
            _ctypes.FreeLibrary(cls.lib._handle)
        cls.tmp.cleanup()

    def test_original_quadrants_thresholds_and_dispatch(self):
        rng=random.Random(0x942442d)
        edge=[0,1,-1,1023,-1023,1024,2097151,2097152,2147483647,-2147483647]
        pairs=[(y,x) for y in edge for x in edge]
        pairs += [(rng.randrange(-2147483647,2147483648),rng.randrange(-2147483647,2147483648)) for _ in range(5000)]
        for y,x in pairs:
            expected=ct.c_int32(replay(self.image,0x8004f794,{4:y,5:x},dict(self.ram))[2]).value
            out=ct.c_int32(0x1234)
            self.assertEqual(self.lib.angle(y,x,ct.byref(out)),0)
            self.assertEqual(out.value,expected,(y,x))
            for stack in (0,1):
                sp=0x800b0000;constants=0x80090000
                memory=dict(self.ram);memory.update({constants:y&MASK,constants+4:x&MASK,sp-8:y&MASK,sp-4:x&MASK})
                inst=0x2de1fe1f if stack else 0x2d001000
                class Code:
                    def word(inner,address):return self.image.word(address)
                regs=replay(Code(),0x80039828,{5:inst,20:constants,22:sp,30:0x1f800000},memory,stops=(0x80038e80,))
                top=sp-4 if stack else sp+4
                self.assertEqual(regs[22],top)
                self.assertEqual(ct.c_int32(memory[top-4]).value,expected)
                output=(ct.c_int32*3)()
                self.assertEqual(self.lib.dispatch(y,x,stack,output),0)
                self.assertEqual(list(output),[expected,1,1])
                model=VM([inst],constants=[y,x])
                if stack:model.stack.push(y);model.stack.push(x)
                self.assertEqual(model.step(),'running')
                self.assertEqual(model.stack.pop(),expected)

    def test_native_extreme_inputs_and_invalid_addresses(self):
        for y,x in [(-2147483648,v) for v in (0,1,-1,-2147483648,2147483647)]+[(v,-2147483648) for v in (0,1,-1,2147483647)]:
            output=ct.c_int32(12345)
            try:
                expected=ct.c_int32(replay(self.image,0x8004f794,{4:y,5:x},dict(self.ram))[2]).value
            except (KeyError,ArithmeticError):
                self.assertEqual(self.lib.angle(y,x,ct.byref(output)),-1)
                self.assertEqual(output.value,12345)
            else:
                self.assertEqual(self.lib.angle(y,x,ct.byref(output)),0)
                self.assertEqual(output.value,expected)
        vm=VM([0x2de1fbe0]);vm.stack.push(100)
        with self.assertRaisesRegex(VMError,'atan address'):vm.step()
        self.assertEqual(vm.stack.height,0)
        output=(ct.c_int32*3)()
        self.assertEqual(self.lib.dispatch(0,100,2,output),-12)
        self.assertEqual(list(output)[1:],[0,0])

    def test_table_and_code_evidence_match_original(self):
        self.assertEqual((ROOT/'source/gool_atan_table.h').read_text(),table_source(self.image))
        evidence=json.loads((ROOT/'tests/gool_atan_native_evidence.json').read_text())
        self.assertEqual(evidence['source_sha256'],self.image.sha256)
        self.assertEqual(bytes.fromhex(evidence['table_raw']),self.image.read(0x8005f6b8,2050))
        source=(ROOT/'source/gool_atan_table.h').read_text().split('{',1)[1].split('}',1)[0]
        values=tuple(map(int,source.replace('\n','').replace(' ','').strip(',').split(',')))
        self.assertEqual(values,struct.unpack('<1025h',self.image.read(0x8005f6b8,2050)))
        for w in evidence['windows'].values():
            self.assertEqual(w['raw_hex'],self.image.read(w['start'],w['end']-w['start']).hex())
        self.assertEqual(self.image.word(0x8005d120+0x2d*4),0x80039828)


if __name__=='__main__':unittest.main()
