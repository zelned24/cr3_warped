"""Replay original JALR/frame/address instructions, without execution hooks.

Stops before the callee fetch. The separate composition suite tests RETURN.
This is fixture RAM, not original-game reachability or C execution.
"""
from pathlib import Path
import unittest
from test_gool_stack_native import replay,MASK
from test_gool_jalr import call
from psx_native import Image,ORIGINAL_SHA256
from gool_operand import decode
from gool_vm import VM
import cb3


class NativeJalr(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image=Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes())
        if cls.image.sha256!=ORIGINAL_SHA256:raise AssertionError('wrong original executable')

    def compare(self,kind,mode,argc,owner=0,null=False,count=None):
        pointer=0 if null else 0x50000000|(owner<<18)|(4 if owner==0 else 0)*4
        count=argc*256+127 if count is None else count
        raw={'member':0xe25,'stack':0xe1f,'frame':0xb7f,'constant':0,'literal':0x800}[kind]
        inst=call(raw)
        vm=VM([inst]+[0x2f000000]*5,alternate_code=[0x31000000],
              args=(pointer,),members={0x25:pointer,0x26:count},constants=(pointer,),stack_start=64)
        vm.mode=mode
        for n in range(argc):vm.stack.push(19-n*41)
        if kind=='stack':vm.stack.push(pointer)
        obj=0x80070000;origin=obj+0x40+vm.stack.origin;constants=0x80090000
        ram={obj+0xd4:pointer,obj+0xd8:count&MASK,constants:pointer}
        for n,value in enumerate(vm.stack.words[:vm.stack.top]):ram[origin+4*n]=value&MASK
        regs=replay(self.image,0x80039978,{5:inst,16:obj,17:mode,20:constants,
                    21:vm.pointer(0,1),22:origin+vm.stack.top*4,
                    23:origin+vm.stack.frames[-1]*4,30:0x1f800000},ram,stops=(0x80038e80,))
        self.assertEqual(vm.step(),'running')
        self.assertEqual(regs[21],vm.pointer(vm.owner,vm.pc));self.assertEqual(regs[17],vm.mode)
        self.assertEqual(regs[22],origin+vm.stack.top*4)
        self.assertEqual(regs[23],origin+vm.stack.frames[-1]*4)
        if not null:
            frame=vm.stack.frames[-1]
            for n in range(3):self.assertEqual(ram[origin+(frame+n)*4],vm.stack.words[frame+n]&MASK)

    def test_120_call_frames_targets_and_modes_match_original(self):
        self.assertEqual(self.image.word(0x8005d120+0x47*4),0x80039978)
        for kind in ('member','stack','frame','constant'):
            for mode in (0,3,4,8,0x10003):
                for argc in (0,1,3):
                    for owner in (0,1):self.compare(kind,mode,argc,owner)

    def test_40_null_cleanup_cases_match_original_raw_shift_wrap(self):
        for kind in ('member','stack','frame','constant','literal'):
            for count in (0,1,2,-1,-2,0x40000000,-0x40000000,-2147483648):
                self.compare(kind,0x1000b,3,null=True,count=count)

    def test_all_128_fast_boolean_addresses_match_decoder_without_reading_address_one(self):
        for raw in range(0xb80,0xc00):
            regs=replay(self.image,0x80038b40,{4:raw,6:24,16:0x80070000,30:0x1f800000},{})
            self.assertEqual(regs[2],decode(raw).value)


if __name__=='__main__':unittest.main()
