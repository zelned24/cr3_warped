"""Original 0x18 and destination helper over fixture RAM, no hooks.

Addresses are symbolic code-owner coordinates; no callee fetch or gameplay.
"""
from pathlib import Path
import unittest
from psx_native import Image,ORIGINAL_SHA256
from test_gool_stack_native import replay,MASK
from test_gool_pointer import pointer
from gool_vm import VM
import cb3


class NativePointer(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image=Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes())
        if cls.image.sha256!=ORIGINAL_SHA256:raise AssertionError('wrong original executable')

    def test_280_native_destination_owner_offset_and_ignored_bit_cases(self):
        self.assertEqual(self.image.word(0x8005d120+0x18*4),0x800392b8)
        for destination in (8,14,31,35,36,37,63):
            for alternate in (False,True):
                for offset in (0,1,7,0x3ffe,0x3fff):
                    for ignored in (0,0x8000,0x10000,0x38000):
                        instruction=pointer(destination,offset,alternate,ignored)
                        vm=VM([instruction],alternate_code=[0x2f000000],stack_start=64)
                        obj=0x80070000;base=0x80090000;alt=0x80091000
                        origin=obj+0x40+vm.stack.origin;top=origin+vm.stack.top*4
                        memory={obj+0xc:base,obj+0x14:alt,base+0x14:vm.pointer(0,0),alt+0x14:vm.pointer(1,0)}
                        regs=replay(self.image,0x800392b8,{5:instruction,16:obj,22:top},memory,
                                    stops=(0x80038e80,))
                        self.assertEqual(vm.step(),'running')
                        address=top if destination==31 else obj+0x40+destination*4
                        value=vm.stack.words[vm.stack.top-1] if destination==31 else vm.members[destination]
                        self.assertEqual(memory[address],value&MASK)
                        self.assertEqual(regs[22],origin+vm.stack.top*4)

    def test_null_alternate_path_never_reads_absent_owner_fields(self):
        instruction=pointer(31,0x3fff,True)
        top=0x80071000
        memory={}
        regs=replay(self.image,0x800392b8,{5:instruction,16:0x80070000,22:top},memory,
                    stops=(0x80038e80,))
        self.assertEqual(memory[top],0);self.assertEqual(regs[22],top+4)


if __name__=='__main__':unittest.main()
