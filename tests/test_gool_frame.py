"""Token protocol boundaries, separate from native-instruction replay."""
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from gool_vm import VM
from gool_stack import StackError
from test_gool_vm import set_geo


class GoolFrame(unittest.TestCase):
    def test_no_scalar_deadline_or_missing_scale_for_no_mirror(self):
        vm=VM([set_geo(0),0x31000000]);vm.mode=4;vm.next_update=999999
        self.assertEqual(vm.step(),'wait')
        self.assertEqual(vm.stack.words[vm.frame_token_index],0)
        self.assertEqual(vm.resume_frame(0),'running')
        self.assertEqual(vm.step(),'done')

    def test_engine_must_resume_and_cannot_consume_from_inside_handler(self):
        vm=VM([set_geo(0,duration=6),0x31000000,0x31000000]);vm.mode=4
        self.assertEqual(vm.step(),'wait')
        vm.set_tick(20)
        self.assertEqual(vm.step(),'wait') # readiness alone does not consume token
        vm.invoke(0,2);vm.mode=3;vm.set_tick(20,False)
        before=(vm.stack.top,vm.tick,vm.frame_token_index)
        with self.assertRaises(StackError):vm.resume_frame(20)
        self.assertEqual(before,(vm.stack.top,vm.tick,vm.frame_token_index))
        vm.step();vm.mode=4
        self.assertEqual(vm.resume_frame(20),'running')
        self.assertEqual(vm.step(),'done')

    def test_initial_zero_token_and_clock_validation(self):
        vm=VM([0x31000000]);vm.seed_frame()
        self.assertEqual(vm.step(),'wait')
        self.assertEqual(vm.resume_frame(7),'running')
        self.assertEqual(vm.stack.height,0)
        with self.assertRaises(ValueError):vm.resume_frame(6)


if __name__=='__main__':unittest.main()
