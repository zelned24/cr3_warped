"""Replay original multiply/RNG instructions with delay slots and no hooks.

The local MIPS fixture interpreter models integer MULT/DIVU/HI/LO, not PS1
hardware timing. Includes raw zero/negative/wrapping range semantics rather
than replacing them with Python random or a per-entity seeded generator.
"""
from pathlib import Path
import random
import unittest
from test_gool_stack_native import replay,MASK
from test_gool_vm import instruction
from gool_vm import VM,VMError,word
from psx_native import Image,ORIGINAL_SHA256
import cb3

class NativeArithmetic(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image=Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes())
        if cls.image.sha256!=ORIGINAL_SHA256:raise AssertionError('wrong original executable')

    def compare(self,op,left,right,seed,kind='constant'):
        low,high={'constant':(0,1),'member':(0xe25,0xe26),'stack':(0xe1f,0xe1f),
                  'frame':(0xb7e,0xb7f)}[kind]
        inst=instruction(op,low,high);state=list(seed)
        vm=VM([inst],constants=(left,right),members={0x25:left,0x26:right},
              args=(left,right),stack_start=64,random_state=state)
        if kind=='stack':vm.stack.push(left);vm.stack.push(right)
        obj=0x80070000;origin=obj+0x140;constants=0x80090000
        ram={constants:left&MASK,constants+4:right&MASK,obj+0xd4:left&MASK,
             obj+0xd8:right&MASK,0x80060a2c:seed[0],0x80060a30:seed[1]}
        for n,value in enumerate(vm.stack.words[:vm.stack.top]):ram[origin+n*4]=value&MASK
        regs=replay(self.image,self.image.word(0x8005d120+op*4),
                    {5:inst,16:obj,20:constants,22:origin+vm.stack.top*4,
                     23:origin+vm.stack.frames[-1]*4},ram,stops=(0x80038e80,))
        self.assertEqual(vm.step(),'running')
        self.assertEqual(regs[22],origin+vm.stack.top*4)
        self.assertEqual(ram[regs[22]-4],vm.stack.words[vm.stack.top-1]&MASK)
        self.assertEqual(state,[ram[0x80060a2c],ram[0x80060a30]])
        return vm

    def test_400_native_multiplications_wrap_low_word_without_shift(self):
        self.assertEqual(self.image.word(0x8005d120+2*4),0x80038f30)
        rng=random.Random(0x9424402)
        edges=[0,1,-1,256,4096,-2147483648,2147483647,65536]
        for kind in ('constant','member','stack','frame'):
            for n in range(100):
                a,b=(edges[n%8],edges[(n//8)%8]) if n<64 else (word(rng.getrandbits(32)),word(rng.getrandbits(32)))
                vm=self.compare(2,a,b,[0x12345678,0xabcdef01],kind)
                self.assertEqual(vm.stack.words[vm.stack.top-1],word(a*b))

    def test_480_native_random_steps_with_raw_ranges(self):
        self.assertEqual(self.image.word(0x8005d120+0x10*4),0x80039168)
        rng=random.Random(0x9424410)
        ranges=[(0,256),(-1024,2048),(7,7),(256,0),(-2147483648,2147483647),
                (2147483647,-2147483648),(0,1),(0,-1)]
        seeds=[(0,0),(1,0),(0,1),(MASK,MASK),(0x12345678,0xabcdef01)]
        for kind in ('constant','member','stack','frame'):
            for n in range(120):
                a,b=ranges[n%8]
                seed=seeds[n%5] if n<40 else (rng.getrandbits(32),rng.getrandbits(32))
                self.compare(0x10,a,b,seed,kind)

    def test_4096_basic_binary_results_match_native_including_division_edges(self):
        rng=random.Random(0x9424400)
        edges=[0,1,-1,256,-256,2147483647,-2147483648,65536]
        for op in range(16):
            for kind in ('constant','member','stack','frame'):
                for n in range(64):
                    a,b=(edges[n%8],edges[n//8]) if n<48 else (word(rng.getrandbits(32)),word(rng.getrandbits(32)))
                    self.compare(op,a,b,[0,0],kind)

    def test_shared_engine_state_interleaves_vms_and_survives_yields(self):
        seed=[0x12345678,0xabcdef01];expected=list(seed)
        code=[instruction(0x10,0,1)]*4
        first=VM(code,constants=(-1000,1000),random_state=seed)
        second=VM(code,constants=(-1000,1000),random_state=seed)
        oracle=VM(code*2,constants=(-1000,1000),random_state=expected)
        for vm in (first,second,first,second,second,first,first,second):
            self.assertEqual(vm.run(1),'budget');oracle.step()
            self.assertEqual(vm.stack.pop(),oracle.stack.pop())
            self.assertEqual(seed,expected)

    def test_missing_provider_rejects_before_stack_pop_and_invalid_seed(self):
        vm=VM([instruction(0x10)]);vm.stack.push(9)
        with self.assertRaises(VMError) as error:vm.step()
        self.assertEqual(error.exception.kind,'unsupported')
        self.assertEqual(vm.stack.top,4)
        for seed in ([],[1],[1,2,3],[-1,2],[1,1<<32],(1,2)):
            with self.assertRaises(ValueError):VM([0x2f000000],random_state=seed)
        state=[1,2];vm=VM([instruction(0x10,0,1)],constants=(0,10),random_state=state)
        state.append(3)
        with self.assertRaises(VMError):vm.step()
        self.assertEqual(state,[1,2,3])

if __name__=='__main__':unittest.main()
