"""Actual array read/write and fast operand instructions, no execution hooks.

Fixture RAM/branch-delay replay only; not PS1 memory timing or whole runtime.
"""
from pathlib import Path
import random
import unittest
import test_gool_memory as fixtures
from test_gool_stack_native import replay,MASK
from psx_native import Image,ORIGINAL_SHA256
from gool_vm import VM,word
from gool_memory import Memory
import cb3


class NativeMemory(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image=Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes())
        if cls.image.sha256!=ORIGINAL_SHA256:raise AssertionError('wrong original executable')

    def test_320_indexed_accesses_match_original_instructions_without_hooks(self):
        rng=random.Random(0x942442a)
        for op in (0x2a,0x4e):
            self.assertEqual(self.image.word(0x8005d120+4*op),0x80039724 if op==0x2a else 0x80039754)
            for kind in ('members','stack','frame','constants'):
                for case in range(40):
                    index=([0,1,255,256,511,512,0xffffff00,0x80000000,0x7fffffff]+[rng.getrandbits(32)])[case%10]
                    m=Memory();values=[word(rng.getrandbits(32))];p=m.borrow(values)
                    offset=((index>>8)<<2)&MASK;base=(p-offset)&MASK
                    high,low={'members':(0xe26,0xe25),'stack':(0xe1f,0xe1f),
                              'frame':(0xb7f,0xe25),'constants':(0,1)}[kind]
                    inst=fixtures.array(op,high,low)
                    vm=VM([inst],members={0x25:word(base),0x26:word(index)},
                          args=(word(index),),constants=(index,base),memory=m,stack_start=64)
                    value=word(rng.getrandbits(32))
                    if op==0x4e:vm.stack.push(value)
                    if kind=='stack':vm.stack.push(word(base));vm.stack.push(word(index))
                    obj=0x80070000;origin=obj+0x140;constants=0x80090000
                    ram={obj+0xd4:base,obj+0xd8:index,p:values[0]&MASK,
                         constants:index,constants+4:base}
                    for n,v in enumerate(vm.stack.words[:vm.stack.top]):ram[origin+4*n]=v&MASK
                    regs=replay(self.image,0x80039724 if op==0x2a else 0x80039754,
                                {5:inst,16:obj,20:constants,22:origin+4*vm.stack.top,
                                 23:origin+4*vm.stack.frames[-1]},ram,stops=(0x80038e80,))
                    self.assertEqual(vm.step(),'running')
                    self.assertEqual(regs[22],origin+4*vm.stack.top)
                    self.assertEqual(ram[p],values[0]&MASK)
                    if op==0x2a:self.assertEqual(ram[regs[22]-4],vm.stack.words[vm.stack.top-1]&MASK)


if __name__=='__main__':unittest.main()
