"""Native SET_STATE compatibility/argument wrapper, engine entry is a hook.

State entry/reset/pre execution have separate tests; no claim for that hooked
body here. Conditional false stubs run original instructions without hooks.
"""
from pathlib import Path
import unittest
import test_gool_state as fixtures
from test_gool_stack_native import replay,MASK
from psx_native import Image,ORIGINAL_SHA256
from gool_vm import VMError
import cb3


class NativeStateWrapper(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image=Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes())
        if cls.image.sha256!=ORIGINAL_SHA256:raise AssertionError('wrong original executable')

    def test_192_original_wrapper_compatibility_and_argument_cases(self):
        for mode in (0,1,2,3,4,5,-1,-2147483648):
            for interact in (0,2,0x400):
                for motion in (0,2,0x400,0x402):
                    for argc in (0,15):
                        self.compare(mode,interact,motion,argc)

    def compare(self,mode,interact,motion,argc):
        i=fixtures.GoolState().make(motion=motion,pre=0x3fff)
        i.vm.resume_frame(7);i.vm.mode=3
        i.object.members[0x32]=mode;i.object.members[0x1c]=interact
        args=[(n+1)*(-17 if n&1 else 19) for n in range(argc)]
        for value in args:i.vm.stack.push(value)
        obj=0x80070000;behavior=0x80090000;states=0x80091000;sp=0x801ff000
        origin=obj+0x40+i.vm.stack.origin;top=origin+i.vm.stack.top*4
        memory={obj+0xbc:top,obj+0xc:behavior,behavior+0x20:states,
                obj+0x108:mode&MASK,obj+0xb0:interact,states+16:motion}
        for n,value in enumerate(i.vm.stack.words[:i.vm.stack.top]):memory[origin+4*n]=value&MASK
        calls=[]
        def engine_entry(regs,ram):
            self.assertEqual((regs[4],regs[5],regs[6]),(obj,1,argc))
            calls.append([ram[regs[7]+4*n] for n in range(argc)])
            regs[2]=0xffffff01
        regs=replay(self.image,0x80037bec,{4:obj,5:fixtures.change(argc=argc),6:3,29:sp},memory,
                    hooks={0x8001d2d4:engine_entry})
        result=i.change_state(1,argc,True)
        self.assertEqual(bool(calls),result=='new_state')
        if calls:
            self.assertEqual(calls,[[v&MASK for v in args]])
            self.assertEqual(i.vm.stack.words[:argc],args)
            self.assertEqual(regs[2],0xffffff01)
        else:
            self.assertEqual(i.state,0);self.assertEqual(i.vm.stack.height,0)
            self.assertEqual(memory[obj+0xbc],origin+i.vm.stack.top*4)
            self.assertEqual(regs[2],0)

    def test_32_false_conditional_stubs_skip_absent_state_tables_and_mode(self):
        for op,start,value in ((0x36,0x80039b28,0),(0x37,0x80039b50,1)):
            for argc in range(16):
                inst=fixtures.change(op,state=1023,argc=argc,condition=0x25)
                i=fixtures.GoolState().make(program=[inst])
                i.vm.resume_frame(7);i.object.members[0x25]=value
                del i.object.members[0x32];del i.object.members[0x1c]
                i.base.items[4]=b''
                for n in range(argc):i.vm.stack.push(n+10)
                obj=0x80070000;origin=obj+0x40+i.vm.stack.origin
                top=origin+i.vm.stack.top*4
                memory={obj+0xd4:value}
                regs=replay(self.image,start,{5:inst,16:obj,22:top},memory,stops=(0x80038e80,))
                self.assertEqual(i.vm.step(),'running');self.assertEqual(i.vm.pc,1)
                self.assertEqual(i.vm.stack.height,0)
                self.assertEqual(regs[22],origin+i.vm.stack.top*4)

    def test_missing_mode_provider_is_fault_after_native_argument_discard(self):
        for member in (0x32,0x1c):
            i=fixtures.GoolState().make(program=[fixtures.change(argc=1)])
            i.vm.resume_frame(7);i.vm.stack.push(123);del i.object.members[member]
            with self.assertRaisesRegex(VMError,hex(member)):i.vm.step()
            self.assertEqual(i.state,0);self.assertEqual(i.state_generation,1)
            self.assertEqual(i.vm.stack.height,0);self.assertEqual(i.vm.pc,0)


if __name__=='__main__':unittest.main()
