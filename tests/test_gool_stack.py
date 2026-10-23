"""Pure Python traces for the GOOL stack contract; no C compilation."""
from pathlib import Path
import random
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from gool_stack import Stack,StackError


class GoolStack(unittest.TestCase):
    def test_nested_call_parameters_and_return(self):
        s=Stack();s.push(10);s.push(20);s.enter(2,0)
        self.assertEqual((s.read(-1),s.read(-2)),(20,10))
        self.assertEqual((s.top,s.height),(5,0))
        s.push(77);s.push(100);s.push(200);s.enter(2,0x50000040)
        self.assertEqual((s.read(-1),s.read(-2)),(200,100))
        s.write(-2,101);s.push(33);s.write(3,44)
        self.assertEqual(s.pop(),44)
        self.assertEqual(s.leave(),0x50000040)
        self.assertEqual((s.top,s.height),(6,1))
        self.assertEqual(s.pop(),77)
        self.assertEqual(s.words[6],101) # callee parameter updated original slot
        self.assertIsNone(s.leave())
        self.assertEqual((s.top,len(s.frames)),(5,1))

    def test_binary_operand_order_with_stack_reads(self):
        s=Stack();s.enter(0,0);s.push(10);s.push(3)
        # Reference evaluates upper/right before lower/left.
        right=s.pop();left=s.pop()
        self.assertEqual(left-right,7)
        with self.assertRaises(StackError):s.pop()

    def test_failures_leave_state_unchanged(self):
        s=Stack(capacity=8,max_frames=2);s.enter(0,0)
        def unchanged(action):
            before=(s.words[:],s.frames[:],s.top)
            with self.assertRaises(StackError):action()
            self.assertEqual((s.words,s.frames,s.top),before)
        unchanged(lambda:s.enter(1,0))
        unchanged(lambda:s.pop())
        unchanged(lambda:s.write(-3,1))
        unchanged(lambda:s.write(100,1))
        s.enter(0,3)
        unchanged(lambda:s.enter(0,4))
        for i in range(2):s.push(i)
        unchanged(lambda:s.push(9))
        self.assertEqual(s.leave(),3)

    def test_frame_access_does_not_change_top(self):
        s=Stack();s.enter(0,0)
        # Explicit frame stores may initialize locals before a push reserves
        # that slot; the reference doesn't change stackIndex in setRelFrame.
        s.write(4,90)
        self.assertEqual(s.read(4),90)
        self.assertEqual(s.top,3)

    def test_reserve_keeps_previous_word(self):
        s=Stack(capacity=4);s.enter(0,0)
        s.push(77);s.pop();s.reserve()
        self.assertEqual(s.pop(),77)
        s.reserve()
        with self.assertRaises(StackError):s.reserve()

    def test_random_call_traces_restore_caller(self):
        rng=random.Random(104)
        for case in range(1000):
            s=Stack();s.enter(0,0)
            locals_=[rng.randrange(-100000,100000) for _ in range(rng.randrange(10))]
            for value in locals_:s.push(value)
            argc=rng.randrange(6)
            args=[rng.randrange(-100000,100000) for _ in range(argc)]
            for value in args:s.push(value)
            s.enter(argc,case)
            self.assertEqual([s.read(-i-1) for i in range(argc)],list(reversed(args)))
            for _ in range(rng.randrange(8)):s.push(rng.randrange(1000))
            self.assertEqual(s.leave(),case)
            self.assertEqual([s.pop() for _ in locals_],list(reversed(locals_)))
            self.assertEqual(s.height,0)

    def test_native_header_words_and_packed_object_offsets(self):
        s=Stack(origin=0x100);s.push(10);s.push(20);s.enter(2,0x50000000)
        self.assertEqual(s.words[2:5],[0xffff,0x50000000,0x100])
        self.assertEqual([s.read(i) for i in range(3)],[0xffff,0x50000000,0x100])
        s.push(77);s.push(100);s.push(200)
        s.enter(2,0x50000040,0x12345678)
        # Native addresses relative to object+0x40: caller FP=0x108,
        # argument base=0x118; actual stack words start at offset 0x100.
        self.assertEqual(s.words[8:11],[0x12345678,0x50000040,0x01080118])
        self.assertEqual(s.leave(),0x50000040)
        self.assertEqual(s.last_flags,0x5678)
        self.assertEqual((s.top,s.frames),(6,[2]))

    def test_corrupt_packed_links_fail_without_unwinding(self):
        for packed in (0,0x00040001,0x00040000,0x00040008,0x00080010,0x0004fffc):
            s=Stack();s.enter(0,0);s.enter(0,42)
            s.words[s.frames[-1]+2]=packed
            before=(s.words[:],s.frames[:],s.top)
            with self.assertRaises(StackError):s.leave()
            self.assertEqual((s.words,s.frames,s.top),before)

    def test_signed_packed_header_and_origin_limits(self):
        s=Stack(origin=0x8000);s.enter(0,0);s.enter(0,7)
        self.assertEqual(s.words[5],-2147450868) # 0x8000800c as signed word
        self.assertEqual(s.leave(),7)
        for origin in (0,1,65536,65536-256*4+4):
            with self.assertRaises(StackError):Stack(origin=origin)


if __name__=='__main__':unittest.main()
