"""Route application with real Instance/VM, not a state-service mock.

Synthetic scripts test composition. They do not certify whole-game reachability
or the uncompiled C counterpart. Native service replay is a separate suite.
"""
import struct
import unittest
import test_gool_delivery as fixtures
from test_gool_delivery import words
from gool_delivery import Delivery
from test_gool_state import store,change
from test_gool_send import send


class DeliveryApplication(unittest.TestCase):
    def fixture(self,**kwargs):return fixtures.GoolDelivery().fixture(**kwargs)

    def destination(self,t,pre=True):
        # New state's pre increments a member, main reads its last argument.
        code=list(t.vm.codes[0])+[0x2f000000]*4
        code[4:9]=[0x00e26a01,store(0xe26,0xe1f),0x31000000,
                   store(0xe36,0xb7f),0x31000000]
        t.base.items[1]=words(code);t.vm.codes[0][:]=code
        states=bytearray(t.base.items[4]);struct.pack_into('<HH',states,28,4 if pre else 0x3fff,7)
        t.base.items[4]=states;t.object.members[0x26]=0

    def test_other_object_receives_incoming_span_then_pre_on_its_update(self):
        s,t,_=self.fixture(default=1,argc=2);self.destination(t)
        job=Delivery(s,t,0,7);self.assertEqual(job.step(1),'route')
        job.arguments[1]=-123 # incoming engine span, not receiver outputs
        before=t.vm.stack.top
        self.assertEqual(job.apply(0,0),'budget');self.assertEqual(t.vm.stack.top,before)
        self.assertEqual(job.apply(0,1),'running')
        self.assertEqual((t.state,t.state_generation),(1,2));self.assertFalse(s.vm.send_pending)
        self.assertEqual(t.vm.stack.words[:2],[70,-123]);self.assertEqual(t.object.members[0x26],0)
        self.assertTrue(t.vm.frame_yield)
        self.assertEqual(t.update(8,20),'done')
        self.assertEqual(t.object.members[0x26],16);self.assertEqual(t.object.members[0x36],-123)

    def test_self_main_replacement_runs_pre_once_and_resumes_main(self):
        s,t,_=self.fixture(default=1,argc=2,self_target=True,publish=False)
        self.destination(t)
        self.assertEqual(s.update(7,1),'send');self.assertTrue(s.busy)
        job=Delivery(s,t,0,7);self.assertEqual(job.step(1),'route')
        self.assertEqual(job.apply(0,1),'budget');self.assertEqual(t.object.members[0x26],0)
        self.assertEqual(job.step(1),'budget');self.assertEqual(t.object.members[0x26],16)
        self.assertEqual(job.step(1),'send_replaced')
        self.assertEqual(t.phase,'main');self.assertFalse(t.vm.frame_yield)
        self.assertEqual(s.update(7,5),'done')
        self.assertEqual(t.object.members[0x26],16);self.assertEqual(t.object.members[0x36],71)

    def test_self_pre_mode_keeps_new_root_token(self):
        s,t,_=self.fixture(default=1,self_target=True);self.destination(t,False)
        # Frontend captures actual mode; prepare a fresh pre-mode request.
        s.vm.send_pending=False;s.vm.pc=0;s.vm.mode=3
        self.assertEqual(s.vm.step(),'send')
        job=Delivery(s,t,0,7);self.assertEqual(job.step(1),'route')
        self.assertEqual(job.apply(0,1),'send_replaced');self.assertTrue(t.vm.frame_yield)
        self.assertEqual(t.vm.stack.height,1)

    def test_hit_compatibility_is_not_rechecked_by_engine_entry(self):
        s,t,_=self.fixture(default=1,raw=0x1800,motion=2)
        self.destination(t,False);t.object.members[0x1c]=2
        job=Delivery(s,t,0,7);self.assertEqual(job.step(1),'route')
        self.assertEqual(job.apply(0,1),'running');self.assertEqual(t.state,1)
        self.assertTrue(t.object.members[0x1a]&0x10000)

    def test_nonplayer_signal_state_entry_does_not_inherit_opcode_mode_filter(self):
        s,t,_=self.fixture(default=1,motion=2);t.object.members[0x32]=3
        self.assertIsNone(t.vm.context.player)
        self.assertEqual(t.change_state(1,0,True),'running');self.assertEqual(t.state,0)
        job=Delivery(s,t,0,7);self.assertEqual(job.step(1),'route')
        self.assertEqual(job.apply(0,1),'running');self.assertEqual(t.state,1)

    def test_same_state_entry_is_a_new_generation_and_preserves_external_args(self):
        s,t,_=self.fixture(default=0);self.destination(t,False)
        job=Delivery(s,t,0,7);job.step(1)
        self.assertEqual(job.apply(None,1),'running')
        self.assertEqual((t.state,t.state_generation),(0,2));self.assertEqual(t.vm.stack.words[0],70)

    def test_explicit_script_budget_restores_main_pc_and_wait_token(self):
        s,t,_=self.fixture(default=-32764,argc=2)
        t.vm.codes[0][4:7]=[store(0xe36,0xb7f),store(0xe26,0xb7e),0x31000000]
        t.base.items[1]=words(t.vm.codes[0])
        top=t.vm.stack.top;token=t.vm.frame_token_index
        job=Delivery(s,t,0,7);job.step(1)
        self.assertEqual(job.apply(0,1),'budget');self.assertTrue(s.vm.send_pending)
        self.assertEqual(job.step(1),'budget');self.assertEqual(job.step(1),'running')
        self.assertEqual((t.object.members[0x36],t.object.members[0x26]),(71,70))
        self.assertEqual((t.vm.pc,t.vm.stack.top,t.vm.frame_token_index),(0,top,token))
        self.assertTrue(t.vm.frame_yield);self.assertTrue(t.vm.respect_delay)

    def test_explicit_script_state_transition_does_not_execute_new_main_as_handler(self):
        s,t,_=self.fixture(default=-32764);self.destination(t,False)
        t.vm.codes[0][4]=change(state=1)
        job=Delivery(s,t,0,7);job.step(1)
        self.assertEqual(job.apply(0,10),'running');self.assertEqual(t.state,1)
        self.assertEqual(t.vm.pc,7);self.assertNotIn(0x36,t.object.members)

    def test_application_error_is_sticky_and_does_not_acknowledge_execution(self):
        s,t,_=self.fixture(default=1);t.object.members[0x24]=1
        job=Delivery(s,t,0,7);job.step(1)
        with self.assertRaisesRegex(ValueError,'script pointer') as first:job.apply(0,1)
        self.assertEqual(t.state,0);self.assertTrue(s.vm.send_pending)
        with self.assertRaises(ValueError) as second:job.step(1)
        self.assertIs(first.exception,second.exception);self.assertFalse(job.bound)

    def test_stale_route_and_foreign_suspended_context_are_rejected(self):
        for stale in (True,False):
            s,t,_=self.fixture(default=1);job=Delivery(s,t,0,7);job.step(1)
            if stale:t.enter_state(0,())
            else:t.busy=True
            with self.assertRaises(ValueError):job.apply(0,1)
            self.assertTrue(s.vm.send_pending)

    def test_nested_send_from_explicit_code_is_owned_separately(self):
        s,t,_=self.fixture(default=-32764)
        t.vm.codes[0][4:6]=[send(raw=0xa10,target=7,condition=14,argc=0),0x31000000]
        job=Delivery(s,t,0,7);job.step(1)
        self.assertEqual(job.apply(0,10),'send')
        child=Delivery(t,s,0,7);self.assertEqual(child.step(1),'running')
        self.assertEqual(job.step(10),'running')
        self.assertFalse(s.vm.send_pending);self.assertFalse(t.vm.send_pending)

    def test_suspended_application_rejects_changed_current_or_new_root_before_fetch(self):
        s,t,_=self.fixture(default=-32764)
        t.vm.codes[0][4:6]=[store(0xe36,0xa01),0x31000000]
        job=Delivery(s,t,0,7);job.step(1)
        self.assertEqual(job.apply(0,1),'budget')
        with self.assertRaisesRegex(ValueError,'current object changed'):job.apply(1,1)
        self.assertTrue(job.active)
        t.enter_state(0,())
        before=(t.vm.pc,t.vm.stack.top)
        with self.assertRaisesRegex(ValueError,'nested state replacement'):job.step(10)
        self.assertEqual(before,(t.vm.pc,t.vm.stack.top));self.assertFalse(job.bound)
        self.assertTrue(s.vm.send_pending)

    def test_abort_application_restores_borrowed_context_without_fake_completion(self):
        s,t,_=self.fixture(default=-32764)
        t.vm.codes[0][4]=0x320003ff
        job=Delivery(s,t,0,7);job.step(1)
        self.assertEqual(job.apply(0,20),'budget');self.assertEqual(t.vm.mode,3)
        job.abort();self.assertEqual(job.step(1),'aborted')
        self.assertTrue(t.vm.respect_delay);self.assertEqual(t.vm.mode,0)
        self.assertTrue(s.vm.send_pending)


if __name__=='__main__':unittest.main()
