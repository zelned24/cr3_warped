"""Deferred SEND frontend contract, not target delivery/handler scheduling."""
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from gool_vm import VM,VMError
from gool_send import finish


def send(raw=0xe25,target=0,condition=8,argc=0):
    return 0x3c000000|target<<21|argc<<18|condition<<12|raw


def sender(inst):
    vm=VM([inst]+[0x2f000000]*7,members={0:0x60000000,8:1,0x1a:0x20020,0x25:0x1800})
    vm.send_enabled=True;vm.mode=4
    return vm


class GoolSend(unittest.TestCase):
    def test_request_keeps_arguments_until_service_completes(self):
        vm=sender(send(argc=2));vm.stack.push(11);vm.stack.push(22)
        self.assertEqual(vm.step(),'send')
        self.assertEqual((vm.send.target,vm.send.raw_signal,vm.send.arguments),(0x60000000,0x1800,(11,22)))
        self.assertEqual(vm.members[0x1a],0x20);self.assertEqual(vm.stack.height,2)
        before=(vm.stack.top,vm.pc,vm.send)
        for _ in range(3):self.assertEqual(vm.step(),'send')
        self.assertEqual(before,(vm.stack.top,vm.pc,vm.send))
        vm.members[0x25]=1 # supplied by actual delivery, not the frontend
        self.assertEqual(finish(vm),'running')
        self.assertEqual(vm.stack.height,0);self.assertEqual(vm.pc,1)

    def test_null_target_false_condition_and_false_pointer_still_clean(self):
        for kind in ('target','condition','pointer'):
            vm=sender(send(raw=0xbe0 if kind=='pointer' else 0xe25,argc=1))
            if kind=='target':vm.members[0]=0
            if kind=='condition':vm.members[8]=0
            vm.stack.push(91)
            self.assertEqual(vm.step(),'running')
            self.assertEqual(vm.members[0x25],0);self.assertEqual(vm.members[0x1a],0x20)
            self.assertEqual(vm.stack.height,0);self.assertFalse(vm.send_pending)

    def test_zero_value_is_not_a_null_pointer_and_collision_load_is_deferred(self):
        vm=sender(send());vm.members[0x25]=0
        self.assertEqual(vm.step(),'send');self.assertEqual(vm.send.raw_signal,0)
        vm=sender(send(raw=0xe1a))
        self.assertEqual(vm.step(),'send');self.assertEqual(vm.send.raw_signal,0x20)
        vm=sender(send(condition=0x1a));vm.members[0x1a]=0x20000
        self.assertEqual(vm.step(),'running');self.assertEqual(vm.members[0x25],0)

    def test_signal_stack_then_condition_stack_then_argument_copy(self):
        vm=sender(send(raw=0xe1f,condition=31,argc=2))
        for value in (10,20,1,0x1800):vm.stack.push(value)
        self.assertEqual(vm.step(),'send')
        self.assertEqual((vm.send.arguments,vm.send.raw_signal),((10,20),0x1800))
        self.assertEqual(finish(vm),'running');self.assertEqual(vm.stack.height,0)

    def test_state_replaced_cleanup_uses_captured_mode_not_old_argc(self):
        for mode in (3,4,8):
            vm=sender(send(argc=2));vm.mode=mode
            vm.stack.push(100);vm.stack.push(200);vm.step()
            # Engine changed state: a new root and native zero wait token.
            vm.stack.frames=[0];vm.stack.top=3;vm.seed_frame();vm.pc=6
            vm.members[0x1a]|=0x20000
            self.assertEqual(finish(vm),'send_replaced')
            self.assertEqual(vm.stack.height,1 if mode&2 else 0)
            self.assertEqual(vm.frame_yield,bool(mode&2));self.assertEqual(vm.pc,6)

    def test_unbound_or_unknown_services_do_not_become_success(self):
        vm=sender(send(raw=0xe1f));vm.send_enabled=False;vm.stack.push(88)
        with self.assertRaises(VMError) as error:vm.step()
        self.assertEqual(error.exception.kind,'unsupported');self.assertEqual(vm.stack.height,1)
        vm=sender(send(raw=0xb90))
        with self.assertRaises(VMError):vm.step()
        vm=sender(send(raw=0xc5f));vm.members[1]=0x60000004
        with self.assertRaises(VMError):vm.step() # raw remote SP pointer not provided
        with self.assertRaises(ValueError):finish(sender(send()))

    def test_normal_cleanup_invalidates_a_wait_token_removed_from_new_stack(self):
        vm=sender(send(argc=2));vm.stack.push(100);vm.stack.push(200);vm.step()
        # Engine callback/pre has replaced the stack then cleared IN_NEW_STATE.
        # Native cleanup still removes argc; metadata must not retain that slot.
        vm.stack.frames=[0];vm.stack.top=3;vm.stack.push(99);vm.seed_frame()
        self.assertEqual(finish(vm),'running')
        self.assertFalse(vm.frame_yield);self.assertEqual(vm.stack.height,0)


if __name__=='__main__':unittest.main()
