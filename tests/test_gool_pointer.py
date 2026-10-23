"""Code-pointer formation and use by actual VM calls/handlers. Python only."""
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from gool_vm import VM,VMError
from gool_object import Object,Context,ObjectVM
from test_gool_jalr import call
from test_gool_receive import receive
import test_gool_delivery as fixtures
from gool_delivery import Delivery


def pointer(destination,pc,alternate=False,ignored=0):
    return 0x18000000|destination<<18|pc|(0x4000 if alternate else 0)|ignored


class GoolPointer(unittest.TestCase):
    def test_script_forms_callback_then_indirect_call_and_return_without_host_pointer(self):
        vm=VM([pointer(31,4),0x11e1fe25,call(0xe25,0x800),0x2f000000,0x31000000])
        vm.mode=3
        for _ in range(4):self.assertEqual(vm.step(),'running')
        self.assertEqual(vm.members[0x25],vm.pointer(0,4));self.assertEqual(vm.pc,3)
        self.assertEqual(vm.mode,3);self.assertEqual(vm.stack.height,0);self.assertEqual(len(vm.stack.frames),1)

    def test_base_and_alternate_are_explicit_even_when_current_code_is_alternate(self):
        vm=VM([0x2f000000],alternate_code=[pointer(0x25,7),pointer(0x26,9,True)])
        vm.owner=1;vm.code=vm.codes[1]
        self.assertEqual(vm.step(),'running');self.assertEqual(vm.members[0x25],vm.pointer(0,7))
        self.assertEqual(vm.step(),'running');self.assertEqual(vm.members[0x26],vm.pointer(1,9))

    def test_null_sentinel_does_not_require_alternate_owner(self):
        for alternate in (False,True):
            vm=VM([pointer(31,0x3fff,alternate)])
            self.assertEqual(vm.step(),'running');self.assertEqual(vm.stack.pop(),0)

    def test_pointer_formation_does_not_fetch_outside_code_but_call_checks_bounds(self):
        vm=VM([pointer(0x25,0x3ffe),call(0xe25,0x800)])
        self.assertEqual(vm.step(),'running');self.assertEqual(vm.members[0x25],vm.pointer(0,0x3ffe))
        with self.assertRaises(VMError):vm.step()
        self.assertEqual(vm.pc,1);self.assertEqual(len(vm.stack.frames),1)

    def test_stack_destination_reserves_before_owner_failure_and_preserves_old_word(self):
        vm=VM([pointer(31,0,True)]);slot=vm.stack.top;vm.stack.words[slot]=765
        with self.assertRaisesRegex(VMError,'code owner'):vm.step()
        self.assertEqual(vm.stack.top,slot+1);self.assertEqual(vm.stack.words[slot],765)

    def test_stack_capacity_and_live_script_overlay_are_explicit_failures(self):
        vm=VM([pointer(31,0)]);vm.stack.top=len(vm.stack.words)
        with self.assertRaises(VMError):vm.step()
        self.assertEqual(vm.stack.top,len(vm.stack.words))
        vm=VM([pointer(32,0)])
        with self.assertRaisesRegex(VMError,'SCRIPT_PTR overlay'):vm.step()
        self.assertEqual(vm.pc,0)

    def test_protected_object_member_requires_its_provider(self):
        for destination in (0,5):
            obj=Object();vm=ObjectVM(Context([obj],0),[pointer(destination,0)])
            with self.assertRaises(VMError):vm.step()
            self.assertEqual(vm.pc,0)

    def test_script_forms_on_signal_pointer_consumed_by_delivery(self):
        s,t,_=fixtures.GoolDelivery().fixture()
        t.vm.codes[0][0]=pointer(0x23,4);t.vm.codes[0][4]=receive(0x3e,action=2)
        t.base.items[1]=fixtures.words(t.vm.codes[0])
        t.vm.resume_frame(7)
        self.assertEqual(t.vm.step(),'running')
        self.assertEqual(t.object.members[0x23],t.vm.pointer(0,4))
        job=Delivery(s,t,0x70000004,7)
        self.assertEqual(job.step(1),'running');self.assertFalse(s.vm.send_pending)
        self.assertIsNone(t.vm.receiver)


if __name__=='__main__':unittest.main()
