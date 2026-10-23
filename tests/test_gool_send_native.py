"""Replay native SEND wrapper; actual delivery call is a declared fixture hook.

This proves frontend operand/cleanup behavior, not the hooked delivery engine.
"""
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from gool_vm import VM,word
from gool_send import finish
from psx_native import Image,ORIGINAL_SHA256
from test_gool_stack_native import replay
from test_gool_send import send
import cb3


class NativeSend(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image=Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes())
        if cls.image.sha256!=ORIGINAL_SHA256:raise AssertionError('wrong original executable')

    def compare(self,raw=0xe25,argc=2,mode=4,condition=1,target=True,replaced=False,target_src=0,cond_src=8):
        inst=send(raw,target=target_src,condition=cond_src,argc=argc)
        vm=VM([inst]+[0x2f000000]*7,stack_start=64,
              members={target_src:0x60000000 if target else 0,8:condition,0x1a:0x20020,0x25:0},constants=[0])
        vm.send_enabled=True;vm.mode=mode
        vals=[10+n for n in range(argc)]
        for v in vals:vm.stack.push(v)
        if cond_src==31:vm.stack.push(condition)
        if raw==0xe1f:vm.stack.push(0x1800)
        obj=0x80070000;origin=0x100;owner=0x80090000;const=0x80091000
        memory={obj+0xbc:obj+0x40+origin,obj+0xc4:obj+0x40,obj+0xc0:0,
                obj+0xc:owner,obj+0x10:owner,owner+0x18:const,const:0}
        replay(self.image,0x8001d588,{4:obj,5:0,6:0xffff},memory)
        for n in range(3,vm.stack.top):memory[obj+0x40+origin+n*4]=vm.stack.words[n]&0xffffffff
        memory[obj+0xbc]=obj+0x40+origin+vm.stack.top*4;memory[obj+0xc0]=0x50000004
        memory[obj+0x40+target_src*4]=obj if target else 0
        memory[obj+0x60]=condition;memory[obj+0xa8]=0x20020;memory[obj+0xd4]=0
        captured=[]
        def delivery(regs,ram):
            address=ram[regs[29]+0x10]
            captured.append((regs[4],regs[5],word(regs[6]),
                             tuple(word(ram[address+n*4]) for n in range(regs[7]))))
            ram[obj+0xd4]=1
            if replaced:
                ram[obj+0xa8]|=0x20000
                top=obj+0x40+origin+4*4
                ram[top-4]=0;ram[obj+0xbc]=top;ram[obj+0xc0]=0x50000018
            regs[2]=0xffffff01
        regs=replay(self.image,0x80037edc,{4:obj,5:inst,6:mode,28:0x80060000,29:0x801ff000},
                    memory,hooks={0x8001db38:delivery})
        result=vm.step()
        if captured:
            self.assertEqual(result,'send')
            self.assertEqual(len(captured),1)
            self.assertEqual(captured[0],(obj,obj,vm.send.raw_signal,vm.send.arguments))
            vm.members[0x25]=1
            if replaced:
                vm.members[0x1a]|=0x20000
                vm.stack.frames=[0];vm.stack.top=3;vm.seed_frame();vm.pc=6
            self.assertEqual(finish(vm),'send_replaced' if replaced else 'running')
        else:self.assertEqual(result,'running')
        self.assertEqual(word(memory[obj+0xd4]),vm.members[0x25])
        self.assertEqual(word(memory[obj+0xa8]),vm.members[0x1a])
        self.assertEqual(memory[obj+0xbc],obj+0x40+origin+vm.stack.top*4)
        self.assertEqual(memory[obj+0xc0],vm.pointer(0,vm.pc))
        self.assertEqual(regs[2],0xffffff01 if replaced and mode&2 and captured else 0)

    def test_wrapper_matches_native_for_192_cases(self):
        for raw in (0xe25,0xe1a,0xe1f,0,0xb03,0xbe0):
            for argc in (1,2,3,7):
                for mode in (0,3,4,8):
                    for condition in (0,1):self.compare(raw,argc,mode,condition)

    def test_stack_order_and_all_target_selectors(self):
        for target in range(8):
            for condition in (0,1):self.compare(raw=0xe1f,cond_src=31,target_src=target,condition=condition)

    def test_null_target_and_replaced_state_cleanup(self):
        for raw in (0xe25,0xe1f,0xbe0):self.compare(raw,target=False)
        for mode in (0,3,4,8):self.compare(mode=mode,replaced=True)


if __name__=='__main__':unittest.main()
