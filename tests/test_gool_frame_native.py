"""Native SET_GEO wait-token and update-gate replay on fixture RAM."""
from pathlib import Path
import random
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from gool_vm import VM,word
from psx_native import Image,ORIGINAL_SHA256
from test_gool_stack_native import replay
from test_gool_vm import set_geo
import cb3


class NativeFrame(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image=Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes())
        if cls.image.sha256!=ORIGINAL_SHA256:raise AssertionError('wrong original executable')

    def compare_set(self,duration,tick,mode,mirror=3,scale=4096):
        inst=set_geo(12,frame=7,duration=duration,mirror=mirror)
        vm=VM([inst,0x31000000],members={0xe:scale});vm.mode=mode;vm.set_tick(tick)
        obj=0x80070000;behavior=0x80090000;top=obj+0x44+vm.stack.top*4
        memory={obj+0xc:behavior,behavior+0x24:0x800a0000,obj+0xbc:top,
                obj+0x78:scale&0xffffffff,0x80068eec:tick&0xffffffff}
        regs=replay(self.image,0x80037cd8,{4:obj,5:inst,6:mode},memory)
        self.assertEqual(vm.step(),'wait' if regs[2] else 'running')
        self.assertEqual(vm.stack.words[vm.stack.top-1],word(memory[top]))
        self.assertEqual(memory[obj+0xbc],top+4)
        self.assertEqual(word(memory[obj+0x78]),vm.members[0xe])
        self.assertEqual(memory[obj+0xec],vm.members[0x2b])
        self.assertEqual(vm.frame_yield,bool(mode&4))

    def test_768_native_token_and_yield_cases(self):
        for duration in range(64):
            for tick in (0,1,0x123456,0xffffff,0x1000000,0xffffffff):
                for mode in (0,4):self.compare_set(duration,tick,mode)

    def test_mirror_modes_match_original(self):
        for mirror in range(4):
            for scale in (-4096,0,4096,-2147483648):
                for mode in (3,4):self.compare_set(6,17,mode,mirror,scale)

    def test_thousand_native_update_gate_cases(self):
        rng=random.Random(0x1d870);obj=0x80070000;top=obj+0x1000
        for _ in range(1000):
            token=rng.randrange(1<<32);tick=rng.randrange(1<<32)
            vm=VM([0x31000000]);vm.stack.push(word(token))
            vm.frame_token_index=vm.stack.top-1;vm.frame_yield=True
            memory={obj+0xbc:top,top-4:token,0x80068eec:tick}
            replay(self.image,0x8001d870,{16:obj},memory,stops=(0x8001d8b0,0x8001d8d8))
            ready=memory[obj+0xbc]==top-4
            self.assertEqual(vm.resume_frame(tick),'running' if ready else 'wait')
            self.assertEqual(vm.frame_yield,not ready)
            self.assertEqual(vm.stack.height,0 if ready else 1)


if __name__=='__main__':unittest.main()
