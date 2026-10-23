"""Replay the original receive service on fixture RAM; no game launch."""
from pathlib import Path
import random
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from gool_vm import VM,Receiver,word
from psx_native import Image,ORIGINAL_SHA256
from test_gool_stack_native import replay
from test_gool_receive import receive
import cb3


class NativeReceive(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image=Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes())
        if cls.image.sha256!=ORIGINAL_SHA256:raise AssertionError('wrong original binary')

    def compare(self,op,action,source_op,argc,value,stack_condition=False):
        inst=receive(op,source=31 if stack_condition else 0x25,src_op=source_op,
                     action=action,offset=1,argc=argc)
        code=[0x2f000000]*8;code[4]=inst
        vm=VM(code);vm.stack.push(0x1800);vm.stack.push(0);vm.invoke(0,4,2)
        vm.receiver=Receiver();vm.mode=8;vm.members[0x25]=value
        vals=[-300+n for n in range(argc)]
        for v in vals:vm.stack.push(v)
        if stack_condition:vm.stack.push(value)
        obj=0x80070000;out=0x80085000
        memory={obj+0xbc:obj+0x44,obj+0xc4:obj+0x40,obj+0xc0:0}
        replay(self.image,0x8001d588,{4:obj,5:0,6:0xffff},memory)
        for n,v in enumerate((0x1800,0)):memory[obj+0x44+(3+n)*4]=v
        memory[obj+0xbc]=obj+0x44+5*4;memory[obj+0xc0]=0x50000000
        replay(self.image,0x8001d588,{4:obj,5:2,6:0xffff},memory)
        for n,v in enumerate(vals+([value] if stack_condition else [])):
            memory[obj+0x44+(8+n)*4]=v&0xffffffff
        memory[obj+0xbc]=obj+0x44+vm.stack.top*4
        memory[obj+0xc0]=0x50000014 # dispatcher has already fetched handler PC 4
        memory[obj+0xd4]=value&0xffffffff
        for n in range(18):memory[out+n*4]=0
        memory[out]=0xffffffff
        regs=replay(self.image,0x800383dc,{4:obj,5:inst,6:8,7:out,29:0x801ff000},memory)
        result=vm.step()
        self.assertEqual(result,'running' if regs[2]==0 else 'signal_return')
        self.assertEqual(memory[0x1f800048],vm.mode)
        self.assertEqual(word(memory[out]),vm.receiver.new_state)
        self.assertEqual(bool(memory[out+4]),vm.receiver.was_b)
        self.assertEqual(memory[out+8],len(vm.receiver.arguments))
        self.assertEqual(tuple(word(memory[out+12+n*4]) for n in range(memory[out+8])),vm.receiver.arguments)
        self.assertEqual(memory[obj+0xbc],obj+0x44+vm.stack.top*4)
        self.assertEqual(memory[obj+0xc4],obj+0x44+vm.stack.frames[-1]*4)
        self.assertEqual(memory[obj+0xc0],vm.pointer(vm.owner,vm.pc))

    def test_receive_service_matches_original_for_384_cases(self):
        for op in (0x3d,0x3e):
            for action in range(4):
                for src_op in range(3):
                    for argc in range(16):
                        self.compare(op,action,src_op,argc,(-1,0,1)[argc%3])

    def test_stack_condition_before_argument_handling(self):
        for action in range(4):
            for value in (0,1):self.compare(0x3e,action,1,2,value,True)


if __name__=='__main__':unittest.main()
