"""Check actual GOOL halfword dispatch against original MIPS LH/SH paths."""
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
from gool_object import Object,Context,ObjectVM
from gool_vm import VMError

WRAPPER=r'''
#include "gool_vm.h"
#include <string.h>
typedef struct {uint8_t *bytes;unsigned base;} Prefix;
static int read_half(void *context,const gool_operand *arg,int32_t *out){
 Prefix *p=context;
 if(arg->kind!=GOOL_ARG_HALFWORD||arg->base!=p->base||arg->value<0||arg->value>=64)return GOOL_ARG_UNBOUND;
 unsigned offset=(unsigned)arg->value*2;unsigned v=p->bytes[offset]|(unsigned)p->bytes[offset+1]<<8;
 *out=v&32768?(int)v-65536:(int)v;return 0;
}
static int write_half(void *context,const gool_operand *arg,int32_t value){
 Prefix *p=context;
 if(arg->kind!=GOOL_ARG_HALFWORD||arg->base!=p->base||arg->value<0||arg->value>=64)return GOOL_ARG_UNBOUND;
 unsigned offset=(unsigned)arg->value*2;p->bytes[offset]=(uint8_t)value;p->bytes[offset+1]=(uint8_t)((uint32_t)value>>8);return 0;
}
static void pack(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
__declspec(dllexport) int run(uint32_t instruction,int32_t value,unsigned stack,uint8_t *prefix,int32_t *out){
 uint8_t code[4],constant[4];pack(code,instruction);pack(constant,(uint32_t)value);
 Prefix p={prefix,(instruction>>12)&7};
 gool_data data={.code=code,.code_words=1,.constants=constant,.constant_words=1,.stack_start=64};
 gool_operand_context ctx={.base=&data,.context=&p,.read=read_half,.write=write_half};gool_vm vm;
 int status=gool_vm_init(&vm,&data,0,&ctx,NULL,0);if(status)return status;
 if(stack)gool_stack_push(&vm.stack,value);
 status=gool_vm_step(&vm);out[1]=(int32_t)gool_stack_height(&vm.stack);out[2]=(int32_t)vm.pc;
 if(!status && instruction>>24==0x23)status=gool_stack_pop(&vm.stack,&out[0]);
 return status;
}
'''


class NativeLighting(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image=Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes())
        assert cls.image.sha256==ORIGINAL_SHA256
        cls.tmp=tempfile.TemporaryDirectory(prefix='crash-lighting-')
        folder=Path(cls.tmp.name);(folder/'wrapper.c').write_text(WRAPPER)
        lib=folder/'lighting.dll'
        subprocess.run([sys.executable,'-m','ziglang','cc','-shared','-O3','-Wall','-Wextra','-Werror',
            '-I'+str(ROOT/'source'),str(folder/'wrapper.c'),
            *all_sources(),
            '-o',str(lib)],check=True)
        cls.lib=ct.CDLL(str(lib))
        cls.lib.run.argtypes=[ct.c_uint32,ct.c_int32,ct.c_uint,ct.POINTER(ct.c_uint8),ct.POINTER(ct.c_int32)]

    @classmethod
    def tearDownClass(cls):
        if os.name=='nt':
            import _ctypes
            _ctypes.FreeLibrary(cls.lib._handle)
        cls.tmp.cleanup()

    def test_8192_original_dispatch_cases(self):
        rng=random.Random(0x942442324)
        obj,remote,constants,top=0x80070000,0x80071000,0x80090000,0x800b0000
        for opcode in (0x23,0x24):
            for base in range(8):
                for selector in range(64):
                    for stack in (0,1):
                        for ignored in (0,0xe00000):
                            target=obj if base==0 else remote
                            raw=bytearray(rng.randbytes(128))
                            if target==obj:struct.pack_into('<I',raw,32+base*4,target)
                            value=ct.c_int32(rng.getrandbits(32)).value
                            inst=opcode<<24|base<<12|selector<<15|(0xe1f if stack else 0)|ignored
                            memory={target+32+i:struct.unpack_from('<I',raw,i)[0] for i in range(0,128,4)}
                            memory[obj+64+base*4]=target
                            memory[constants]=value&MASK;memory[top-4]=value&MASK
                            regs=replay(self.image,0x80039684 if opcode==0x23 else 0x800396b0,
                                {5:inst,16:obj,20:constants,22:top},memory,stops=(0x80038e80,))
                            expected=b''.join(struct.pack('<I',memory[target+32+i]) for i in range(0,128,4))
                            buffer=(ct.c_uint8*128).from_buffer_copy(raw);out=(ct.c_int32*3)()
                            self.assertEqual(self.lib.run(inst,value,stack,buffer,out),0)
                            self.assertEqual(bytes(buffer),expected)
                            self.assertEqual(regs[22],top+(4 if opcode==0x23 else -4 if stack else 0))
                            self.assertEqual(list(out)[1:],[stack+1 if opcode==0x23 else 0,1])
                            if opcode==0x23:self.assertEqual(out[0],ct.c_int32(memory[top]).value)

    def test_object_alias_and_unknown_provider(self):
        a,b=Object(),Object();ctx=Context([a,b],0,player=1)
        vm=ObjectVM(ctx,[0x2f000000]);a.members[1]=ctx.pointer(1)
        for selector in range(16):
            with self.assertRaises(ValueError):ctx.lighting_read(1,selector)
            ctx.lighting_write(1,selector,0x12348000+selector)
            self.assertEqual(ctx.lighting_read(1,selector),-32768+selector)
        b.members[8]=0x12345678
        ctx.lighting_write(1,32,0xabcd)
        self.assertEqual(b.members[8],0x1234abcd)
        self.assertEqual(ctx.lighting_read(1,33),0x1234)
        ctx.lighting_write(1,33,0xfedc)
        self.assertEqual(b.members[8],-19092531)
        self.assertEqual(ctx.lighting_read(1,32),-21555)
        vm.code[0]=0x24001e1f;a.members[1]=0;vm.stack.push(42)
        with self.assertRaises(VMError):vm.step()
        self.assertEqual(vm.stack.height,0)

    def test_original_handler_windows(self):
        evidence=json.loads((ROOT/'tests/gool_lighting_evidence.json').read_text())
        self.assertEqual(evidence['source_sha256'],self.image.sha256)
        self.assertEqual(evidence['raw_hex'],self.image.read(0x80039684,0x58).hex())
        self.assertEqual(self.image.word(0x8005d120+0x23*4),0x80039684)
        self.assertEqual(self.image.word(0x8005d120+0x24*4),0x800396b0)


if __name__=='__main__':unittest.main()
