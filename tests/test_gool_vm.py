"""Pure Python execution traces for the initial dispatcher, not gameplay QA."""
from pathlib import Path
import random
import struct
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from gool_vm import VM,VMError,word
import cb3


def instruction(op,left=0xe1f,right=0xe1f):return op<<24|right<<12|left

def branch(op,offset,discard=0,condition=0):
    return op<<24|(condition&0x3f)<<14|(discard&15)<<10|(offset&0x3ff)

def call(pc,argc=0,alternate=False):return 0x3b000000|(argc<<20)|pc|(0x4000 if alternate else 0)

def set_geo(model,frame=0,duration=0,mirror=3):
    return 0x38000000|(mirror<<22)|(duration<<16)|(model<<5)|frame

def set_frame(operand,duration=0,mirror=3):
    return 0x39000000|(mirror<<22)|(duration<<16)|operand


class GoolVM(unittest.TestCase):
    def test_native_frame_headers_and_first_local_operand(self):
        vm=VM([instruction(0x11,0xe25,0xb00),instruction(0x11,0xb03,0xa07),
               instruction(0x11,0xe26,0xb03),0x31000000],stack_start=64)
        self.assertEqual(vm.read(0xb02),0x100)
        self.assertEqual(vm.run(10),'done')
        self.assertEqual(vm.members[0x25],0xffff)
        self.assertEqual(vm.members[0x26],112)

    def test_native_call_flags_clear_and_return_restores_low_half(self):
        vm=VM([call(2),0x31000000,0x31000000])
        vm.mode=0x12340009
        self.assertEqual(vm.step(),'running')
        self.assertEqual(vm.mode,0x12340000)
        self.assertEqual(vm.read(0xb00),0x12340009)
        vm.mode=0xabcd0000
        self.assertEqual(vm.step(),'running')
        self.assertEqual(vm.mode,0xabcd0009)
        self.assertEqual(vm.pc,1)

    def test_program_store_arithmetic_and_root_return(self):
        # Push 7, push 2, subtract, save result, complement, finish.
        code=[instruction(0x11,0xe1f,0xa07),instruction(0x11,0xe1f,0xa02),
              instruction(1),instruction(0x11,0xe25),instruction(0x17,0xe26,0xe25),0x31000000]
        vm=VM(code)
        self.assertEqual(vm.run(2),'budget')
        self.assertEqual(vm.run(10),'done')
        self.assertEqual(vm.members,{0x25:80,0x26:-81})
        self.assertEqual(vm.stack.height,0)

    def test_wrapping_division_modulo_and_shift(self):
        cases=[(0,0x7fffffff,1,-2147483648),(1,-2147483648,1,2147483647),
            (3,-7,3,-2),(13,-7,3,-1),(3,-2147483648,-1,-2147483648),
            (0x15,-2147483648,-1,-1073741824),(0x15,1,33,2),
            (0x15,-3,-32,-3),(0x21,4095,1,2)]
        for op,left,right,result in cases:
            with self.subTest(op=op,left=left,right=right):
                vm=VM([instruction(op,0,1)],constants=[left,right])
                vm.step();self.assertEqual(vm.stack.pop(),result)

    def test_unsupported_is_not_nop(self):
        for op in (0x10,0x4f,0x35,0x3a,0x49,0xff):
            vm=VM([instruction(op)]);vm.stack.push(12)
            with self.assertRaises(VMError) as first:vm.step()
            self.assertEqual(first.exception.kind,'unsupported')
            self.assertEqual(vm.pc,0);self.assertEqual(vm.stack.height,1)
            with self.assertRaises(VMError) as second:vm.step()
            self.assertIs(first.exception,second.exception)

    def test_missing_members_stop_and_zero_divisor_has_native_result(self):
        vm=VM([instruction(0,0xe25,0xa01)])
        with self.assertRaises(VMError):vm.step()
        self.assertEqual(vm.pc,0)
        vm=VM([instruction(3,0xa01,0x800)])
        self.assertEqual(vm.step(),'running')
        self.assertEqual(vm.stack.pop(),-1)
        self.assertEqual(vm.pc,1)

    def test_random_exact_integer_add_sub(self):
        rng=random.Random(349)
        for _ in range(1000):
            left=rng.randrange(-(1<<31),1<<31);right=rng.randrange(-(1<<31),1<<31)
            op=rng.randrange(2)
            vm=VM([instruction(op,0,1)],constants=[left,right]);vm.step()
            self.assertEqual(vm.stack.pop(),word(left+right if op==0 else left-right))

    def test_real_box_clock_initial_prefix(self):
        entry=cb3.load_level(11).entry('BoxsC')
        code=struct.unpack('<%dI'%(len(entry.items[1])//4),entry.items[1])
        constants=struct.unpack('<%dI'%(len(entry.items[2])//4),entry.items[2])
        # State 8 script offset taken directly from the original state record.
        pc=struct.unpack_from('<H',entry.items[4],8*16+14)[0]&0x3fff
        vm=VM(code,constants=constants,pc=pc)
        vm.step();vm.step()
        self.assertEqual(vm.members,{0x1b:16,0x36:32})
        with self.assertRaises(VMError) as failure:vm.step()
        self.assertEqual((failure.exception.pc,failure.exception.instruction>>24),(pc+2,0x20))

    def test_countdown_loop_and_resumable_budget(self):
        code=[instruction(0x11,0xe25,0xa03),instruction(1,0xe25,0xa01),
              instruction(0x11,0xe25),branch(0x33,-3,condition=0x25),0x31000000]
        vm=VM(code)
        self.assertEqual(vm.run(4),'budget')
        self.assertEqual((vm.pc,vm.members[0x25]),(1,32))
        self.assertEqual(vm.run(20),'done')
        self.assertEqual(vm.members[0x25],0)
        self.assertEqual(vm.stack.height,0)

    def test_conditional_discard_and_stack_condition(self):
        for op,value,taken in ((0x33,1,True),(0x33,0,False),(0x34,0,True),(0x34,-1,False)):
            with self.subTest(op=op,value=value):
                vm=VM([branch(op,1,discard=1,condition=31),0x2f000000,0x31000000])
                vm.stack.push(99);vm.stack.push(value)
                vm.step()
                self.assertEqual(vm.pc,2 if taken else 1)
                self.assertEqual(vm.stack.height,0 if taken else 1)
        # JMP must never consult its encoded condition field.
        vm=VM([branch(0x32,0,condition=0x25),0x31000000]);vm.step()
        self.assertEqual(vm.pc,1)

    def test_branch_errors_and_infinite_loop_budget(self):
        for offset in (-2,511):
            vm=VM([branch(0x32,offset)])
            with self.assertRaises(VMError):vm.step()
            self.assertEqual(vm.pc,0)
        vm=VM([branch(0x32,0,discard=1),0x31000000])
        with self.assertRaises(VMError):vm.step()
        self.assertEqual(vm.stack.height,0)
        # A non-taken branch must ignore an otherwise invalid destination and
        # discard count, while still evaluating the condition.
        vm=VM([branch(0x33,511,discard=15,condition=0x25),0x31000000],members={0x25:0})
        self.assertEqual(vm.run(2),'done')
        vm=VM([branch(0x32,-1)])
        self.assertEqual(vm.run(1000),'budget');self.assertEqual(vm.pc,0)

    def test_all_signed_branch_offsets(self):
        code=[0x2f000000]*1025
        for offset in range(-512,512):
            code[512]=branch(0x32,offset)
            vm=VM(code,pc=512);vm.step()
            self.assertEqual(vm.pc,513+offset)

    def test_call_parameters_and_nested_return(self):
        code=[instruction(0x11,0xe1f,0xa07),call(4,1),
              instruction(0x11,0xe25,0xe26),0x31000000,
              instruction(0,0xb7f,0xa01),instruction(0x11,0xe26),0x31000000]
        vm=VM(code)
        self.assertEqual(vm.run(20),'done')
        self.assertEqual(vm.members,{0x25:128,0x26:128})
        self.assertEqual((vm.stack.height,len(vm.stack.frames)),(0,1))

    def test_alternate_call_does_not_replace_constant_owners(self):
        base=[call(0,alternate=True),instruction(0x11,0xe25,0xe26),0x31000000]
        alternate=[instruction(0x11,0xe26,0),instruction(0x11,0xe27,0x400),0x31000000]
        vm=VM(base,constants=[11],alternate=[99],alternate_code=alternate)
        vm.step()
        self.assertEqual((vm.owner,vm.pc),(1,0))
        self.assertEqual(vm.stack.words[vm.stack.frames[-1]+1],0x50000004)
        self.assertEqual(vm.run(20),'done')
        self.assertEqual((vm.owner,vm.members),(0,{0x25:11,0x26:11,0x27:99}))

    def test_nested_calls_back_from_alternate(self):
        base=[call(0,alternate=True),0x31000000,0x2f000000,
              instruction(0x11,0xe28,0xa09),0x31000000]
        alternate=[call(3),instruction(0x11,0xe27,0xa02),0x31000000]
        vm=VM(base,alternate_code=alternate)
        vm.step();vm.step()
        self.assertEqual((vm.owner,vm.pc,len(vm.stack.frames)),(0,3,3))
        self.assertEqual(vm.stack.words[vm.stack.frames[-1]+1],0x50040004)
        self.assertEqual(vm.run(20),'done')
        self.assertEqual(vm.members,{0x28:144,0x27:32})

    def test_script_pointer_read_is_next_instruction(self):
        vm=VM([instruction(0x11,0xe25,0xe20),0x31000000])
        vm.step();self.assertEqual(vm.members[0x25],0x50000004)

    def test_call_and_return_failures(self):
        for code in ([call(9)],[call(0,alternate=True)],[call(1,argc=1),0x31000000]):
            vm=VM(code);before=(vm.stack.top,len(vm.stack.frames))
            with self.assertRaises(VMError):vm.step()
            self.assertEqual((vm.pc,vm.stack.top,len(vm.stack.frames)),(0,*before))
        vm=VM([call(2),0x31000000,0x31000000]);vm.step()
        vm.stack.words[vm.stack.frames[-1]+1]=0x60000000
        before=vm.stack.top
        with self.assertRaises(VMError):vm.step()
        self.assertEqual((vm.pc,vm.stack.top,len(vm.stack.frames)),(2,before,2))
        vm=VM([call(0)])
        with self.assertRaises(VMError):vm.run(100)
        self.assertEqual(len(vm.stack.frames),vm.stack.max_frames)

    def test_geometry_model_and_frame_encodings(self):
        for model in range(0,0x800,4):
            vm=VM([set_geo(model,frame=127)],members={0xe:4096})
            vm.step()
            self.assertEqual(vm.members[0x2a],0x40000000|(model<<2))
            self.assertEqual(vm.members[0x2b],127*256)
        for frame in range(128):
            vm=VM([set_geo(20,frame=frame)],members={0xe:4096});vm.step()
            self.assertEqual(vm.members[0x2b],frame*256)

    def test_mirror_modes_and_current_geometry(self):
        for scale in (-4096,4096,0,-2147483648):
            for mirror in range(4):
                vm=VM([set_frame(0xa07,mirror=mirror)],members={0xe:scale,0x2a:0x40000050})
                vm.step()
                expected=(word(abs(scale)),word(-abs(scale)),word(-scale),scale)[mirror]
                self.assertEqual(vm.members[0xe],expected)
                self.assertEqual(vm.members[0x2b],112)
                self.assertEqual(vm.members[0x2a],0x40000050)
        vm=VM([set_frame(0xe1f)],members={0xe:4096});vm.stack.push(768);vm.step()
        self.assertEqual((vm.stack.height,vm.members[0x2b]),(1,768)) # consumed frame, pushed wait token

    def test_frame_duration_waits_in_logic_ticks(self):
        for duration in range(2,64):
            vm=VM([set_geo(0,duration=duration),0x31000000],members={0xe:4096})
            vm.mode=4
            self.assertEqual(vm.run(10),'wait')
            self.assertEqual((vm.pc,vm.next_update),(1,duration))
            self.assertEqual(vm.resume_frame(duration-1),'wait')
            self.assertEqual(vm.step(),'wait');self.assertEqual(vm.pc,1)
            self.assertEqual(vm.resume_frame(duration),'running')
            self.assertEqual(vm.step(),'done')
        for duration in (0,1):
            vm=VM([set_geo(0,duration=duration),0x31000000],members={0xe:4096});vm.mode=4
            self.assertEqual(vm.run(2),'wait')
            self.assertEqual(vm.resume_frame(0),'running')
            self.assertEqual(vm.step(),'done')

    def test_handler_delay_bypass_and_clock_errors(self):
        vm=VM([set_geo(0,duration=6),0x31000000,set_frame(0x800),0x31000000],members={0xe:4096})
        vm.mode=4
        vm.step();self.assertEqual(vm.step(),'wait')
        token=vm.stack.words[vm.frame_token_index]
        vm.invoke(0,2);vm.mode=3;vm.set_tick(0,respect_delay=False)
        self.assertEqual(vm.run(2),'budget')
        self.assertEqual(vm.stack.words[vm.frame_token_index],token)
        vm.mode=4
        self.assertEqual(vm.resume_frame(5),'wait');self.assertEqual(vm.step(),'wait')
        with self.assertRaises(ValueError):vm.set_tick(4)
        self.assertEqual(vm.resume_frame(6),'running');self.assertEqual(vm.step(),'done')
        vm=VM([set_geo(0,duration=1)],members={0xe:4096});vm.set_tick((1<<64)-1)
        vm.step();self.assertEqual(vm.stack.pop(),-1) # uint32 native counter, no uint64 deadline overflow

    def test_original_warp_effect_frame_instruction(self):
        e=cb3.load_level(11).entry('WEfOC')
        pc=struct.unpack_from('<H',e.items[4],18*16+14)[0]&0x3fff
        code=struct.unpack('<%dI'%(len(e.items[1])//4),e.items[1])
        self.assertEqual(code[pc],0x38c60000)
        vm=VM(code,pc=pc,members={0xe:4096})
        vm.mode=4
        vm.step()
        self.assertEqual(vm.members,{0xe:4096,0x2a:0x40000000,0x2b:0})
        self.assertEqual((vm.pc,vm.next_update),(pc+1,6))
        self.assertEqual(vm.step(),'wait')

    def test_shared_globals_and_reserved_result_slot(self):
        globals_=[0]*8
        writer=VM([instruction(0x20,0x801,0xa09)],globals_=globals_)
        # key literal 0x801 = 256, so index 1. Preserve an already used slot.
        writer.stack.push(77);self.assertEqual(writer.stack.pop(),77)
        writer.step()
        self.assertEqual(globals_[1],144)
        self.assertEqual(writer.stack.pop(),77) # not zero or stored global value
        reader=VM([instruction(0x1f,0x801,0xe1f)],globals_=globals_)
        reader.stack.push(55);reader.step()
        self.assertEqual(reader.stack.pop(),144)
        self.assertEqual(reader.stack.height,0)
        self.assertIs(reader.globals,writer.globals)

    def test_global_bounds_unbound_and_overflow(self):
        for globals_,key in ((None,0x800),([],0x800),([0],0x801),([0],0)):
            vm=VM([instruction(0x20,key,0xa02)],constants=[-1],globals_=globals_)
            with self.assertRaises(VMError):vm.step()
            self.assertEqual(vm.pc,0)
            if globals_:self.assertEqual(globals_,[0])
        globals_=[0]
        vm=VM([instruction(0x20,0x800,0xa02)],globals_=globals_)
        for _ in range(len(vm.stack.words)-vm.stack.top):vm.stack.push(123)
        with self.assertRaises(VMError):vm.step()
        self.assertEqual(globals_,[0])

    def test_original_clock_prefix_with_explicit_globals(self):
        e=cb3.load_level(11).entry('BoxsC')
        code=struct.unpack('<%dI'%(len(e.items[1])//4),e.items[1])
        constants=struct.unpack('<%dI'%(len(e.items[2])//4),e.items[2])
        pc=struct.unpack_from('<H',e.items[4],8*16+14)[0]&0x3fff
        # Explicit experiment state, not a replacement for game initialization.
        globals_=[0]*512;globals_[0]=11<<8
        vm=VM(code,constants=constants,pc=pc,members={0x1e:126},globals_=globals_)
        for _ in range(30):self.assertEqual(vm.step(),'running')
        self.assertEqual(globals_[0xae],126)
        self.assertEqual(vm.members[0x25],0)
        self.assertEqual(vm.members[0x26],1)
        with self.assertRaises(VMError) as failure:vm.step()
        self.assertEqual((failure.exception.pc,failure.exception.instruction),(1995,0x01e1fe27))
        self.assertIn('unbound member',str(failure.exception))


if __name__=='__main__':unittest.main()
