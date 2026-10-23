"""Native RECEIVE context/actions; transport/state application remain separate."""
from pathlib import Path
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from gool_vm import VM,VMError,Receiver,word
import cb3


def receive(op=0x3d,source=0x25,src_op=0,action=1,offset=7,argc=0):
    return op<<24|action<<22|src_op<<20|source<<14|argc<<10|(offset&1023)


class GoolReceive(unittest.TestCase):
    def handler(self,instruction,alternate=False,waiting=False):
        code=[0x2f000000]*8;code[4]=instruction;code[6]=0x31000000
        vm=VM(code,alternate_code=code)
        if waiting:
            vm.frame_token_index=vm.stack.top;vm.stack.push(word(99<<24));vm.frame_yield=True
        vm.stack.push(0x1800);vm.stack.push(0x12340000) # sender-data pointer, opaque here
        vm.invoke(int(alternate),4,2)
        vm.receiver=Receiver();vm.mode=8
        vm.set_tick(20,False);vm.next_update=99
        return vm

    def test_a_and_b_select_state_and_complete_handler(self):
        for op in (0x3d,0x3e):
            vm=self.handler(receive(op,offset=34),alternate=True,waiting=True)
            self.assertEqual(vm.step(),'signal_return')
            self.assertEqual((vm.owner,vm.pc),(0,0))
            self.assertEqual(vm.receiver,Receiver(op==0x3e,34))
            self.assertEqual((len(vm.stack.frames),vm.stack.height),(1,1))
            self.assertFalse(vm.done)
            self.assertEqual(vm.next_update,99)
            vm.mode=4;vm.receiver=None
            self.assertEqual(vm.resume_frame(20),'wait')
            self.assertEqual(vm.step(),'wait')

    def test_all_state_values_and_action_semantics(self):
        for state in range(1024):
            vm=self.handler(receive(action=1,offset=state))
            self.assertEqual(vm.step(),'signal_return');self.assertEqual(vm.receiver.new_state,state)
        for action in (0,3):
            vm=self.handler(receive(action=action))
            self.assertEqual(vm.step(),'running')
            self.assertEqual(vm.receiver.new_state,-1)
            self.assertEqual(vm.pc,5)
            self.assertEqual(vm.mode,40)
            self.assertEqual(vm.run(2),'signal_return')
            self.assertEqual(vm.receiver.new_state,255)
        vm=self.handler(receive(action=2))
        self.assertEqual(vm.step(),'signal_return');self.assertEqual(vm.receiver.new_state,255)

    def test_conditions_and_unknown_source_op(self):
        for src_op in range(3):
            for value in (-1,0,1):
                vm=self.handler(receive(src_op=src_op));vm.members[0x25]=value
                matches=True if src_op==0 else (value==0 if src_op==2 else value!=0)
                self.assertEqual(vm.step(),'signal_return' if matches else 'running')
                self.assertEqual(vm.receiver.new_state,7 if matches else -1)
        vm=self.handler(receive(src_op=3,source=31))
        with self.assertRaises(VMError):vm.step()
        self.assertEqual(vm.receiver,Receiver());self.assertEqual(vm.stack.height,0)

    def test_false_action_zero_branches_and_discards_argc(self):
        for offset in (-1,0,1,2):
            vm=self.handler(receive(src_op=1,action=0,offset=offset,argc=2))
            vm.members[0x25]=0;vm.stack.push(33);vm.stack.push(44)
            vm.step();self.assertEqual(vm.pc,5+offset)
            self.assertEqual(vm.stack.height,0);self.assertEqual(vm.receiver.new_state,-1)
        vm=self.handler(receive(src_op=1,action=0,offset=511));vm.members[0x25]=0
        with self.assertRaises(VMError):vm.step()
        self.assertEqual(vm.pc,4)

    def test_action_one_transfers_all_argument_counts_in_order(self):
        for argc in range(16):
            vm=self.handler(receive(argc=argc));values=tuple(-100+n for n in range(argc))
            for value in values:vm.stack.push(value)
            self.assertEqual(vm.step(),'signal_return')
            self.assertEqual(vm.receiver.arguments,values)
            self.assertEqual(vm.stack.height,0)
        vm=self.handler(receive(argc=1))
        with self.assertRaises(VMError):vm.step()
        self.assertEqual(vm.receiver,Receiver())

    def test_stack_condition_consumed_once_before_argument_transfer(self):
        vm=self.handler(receive(source=31,src_op=1,argc=2))
        for value in (11,22,77):vm.stack.push(value)
        self.assertEqual(vm.step(),'signal_return')
        self.assertEqual(vm.receiver.arguments,(11,22))
        vm=self.handler(receive(source=31,src_op=0,action=0))
        self.assertEqual(vm.step(),'running') # no STACK read for unconditional
        self.assertEqual(vm.receiver.new_state,-1)

    def test_engine_context_is_independent_of_script_parameter(self):
        vm=self.handler(receive(op=0x3e))
        vm.stack.words[vm.stack.frames[-1]-1]=0 # not a tagged receiver index
        self.assertEqual(vm.step(),'signal_return')
        self.assertEqual(vm.receiver,Receiver(True,7))
        vm=self.handler(receive(source=31,src_op=1));vm.mode=0
        with self.assertRaises(VMError):vm.step()
        self.assertEqual(vm.receiver,Receiver());self.assertEqual(vm.stack.height,0)
        vm=self.handler(receive());vm.receiver=None
        with self.assertRaises(VMError):vm.step()

    def test_normal_return_without_receive_requests_default_route(self):
        vm=self.handler(0x31000000)
        self.assertEqual(vm.step(),'failed_signal')
        self.assertEqual(vm.receiver.new_state,-1);self.assertEqual(vm.pc,0)
        # A nested JAL clears mode bit 8; its RETURN resumes the handler.
        vm=self.handler(0x3b000006)
        self.assertEqual(vm.step(),'running');self.assertEqual(vm.mode,0)
        self.assertEqual(vm.step(),'running');self.assertEqual(vm.pc,5);self.assertEqual(vm.mode,8)
        vm.code[5]=0x31000000
        self.assertEqual(vm.step(),'failed_signal')

    def test_false_nonbranch_does_not_discard_and_action_two_keeps_result_args(self):
        for action in (1,2,3):
            vm=self.handler(receive(src_op=1,action=action,argc=1));vm.members[0x25]=0
            vm.stack.push(91);vm.step();self.assertEqual(vm.stack.height,1)
        vm=self.handler(receive(action=2,argc=15));vm.receiver.arguments=(71,)
        self.assertEqual(vm.step(),'signal_return')
        self.assertEqual(vm.receiver.arguments,(71,))

    def test_original_box_receive_b_selects_state_34(self):
        e=cb3.load_level(11).entry('BoxsC')
        code=struct.unpack('<%dI'%(len(e.items[1])//4),e.items[1])
        vm=VM(code);vm.receiver=Receiver();vm.mode=8
        vm.stack.push(0x1800);vm.stack.push(0)
        vm.invoke(0,1184,2)
        self.assertEqual(code[1184],0x3e494022)
        self.assertEqual(vm.step(),'signal_return')
        self.assertEqual(vm.receiver,Receiver(True,34));self.assertEqual(vm.pc,0)


if __name__=='__main__':unittest.main()
