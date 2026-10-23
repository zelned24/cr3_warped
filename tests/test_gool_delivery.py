"""Real Instance/VM handlers and routes; final state/code invocation pending."""
from pathlib import Path
import struct
import sys
from types import SimpleNamespace
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from gool_instance import Instance,Placement
from gool_object import Object,Context
from gool_delivery import Delivery
from test_gool_send import send
from test_gool_receive import receive


def words(values):return struct.pack('<%dI'%len(values),*values)


class GoolDelivery(unittest.TestCase):
    def fixture(self,handler=None,default=255,raw=0,argc=1,motion=0,self_target=False,publish=True):
        objects=[Object(),Object()];instances=[]
        for index in range(2):
            header=bytearray(20);struct.pack_into('<I',header,12,64);struct.pack_into('<I',header,16,64)
            code=[0x2f000000]*8;code[0]=send(target=1,condition=14,argc=argc)
            target=index==(0 if self_target else 1)
            if target and handler:code[2:2+len(handler)]=handler
            on=2 if target and handler else 0x3fff
            states=struct.pack('<II4H',0,0,0,on,0x3fff,0)
            states+=struct.pack('<II4H',motion,0,0,0x3fff,0x3fff,0)
            table=[255]*64;table[(raw&0xffffffff)>>8]=default
            eid=100+index
            entry=SimpleNamespace(eid=eid,items=[header,words(code),words([eid]),
                          struct.pack('<65h',*table,0),states,b''])
            instances.append(Instance(Context(objects,index),entry,entry,Placement(34,0,(0,0,0),1,0)))
        sender=instances[0];target=instances[0 if self_target else 1]
        for instance in instances:instance.object.members[0x32]=0 # fixture provider for SET_STATE
        sender.object.members[1]=Context.pointer(0 if self_target else 1)
        sender.object.members[0x25]=raw;sender.vm.send_enabled=True;target.vm.send_enabled=True
        sender.vm.resume_frame(7);sender.vm.mode=4
        for n in range(argc):sender.vm.stack.push(70+n)
        if publish:self.assertEqual(sender.vm.step(),'send')
        return sender,target,instances

    def test_handler_executes_and_routes_without_faking_state_application(self):
        s,t,_=self.fixture([receive(0x3e,offset=1)])
        token=t.vm.stack.words[t.vm.frame_token_index];old_top=t.vm.stack.top
        job=Delivery(s,t,0xb0000000,7)
        self.assertEqual(job.step(0),'budget')
        self.assertEqual(job.step(1),'route')
        self.assertEqual(job.route.value,1);self.assertEqual(t.state,0) # execution boundary only
        self.assertEqual(t.object.members[7],Context.pointer(0))
        self.assertEqual(t.object.members[0x37],0)
        self.assertEqual(s.object.members[0x25],1)
        self.assertEqual(t.vm.stack.top,old_top);self.assertEqual(t.vm.stack.words[t.vm.frame_token_index],token)
        self.assertIsNone(t.vm.receiver);self.assertTrue(s.vm.send_pending)

    def test_no_match_falls_back_and_none_finishes_sender(self):
        s,t,_=self.fixture([0x31000000])
        job=Delivery(s,t,0xb0000000,7)
        self.assertEqual(job.step(10),'running');self.assertFalse(s.vm.send_pending)
        self.assertEqual(s.object.members[0x25],0);self.assertNotIn(0x37,t.object.members)
        self.assertEqual(s.vm.stack.height,0)

    def test_handler_a_or_b_without_state_suppresses_default(self):
        for op in (0x3d,0x3e):
            s,t,_=self.fixture([receive(op,action=2)],default=1)
            job=Delivery(s,t,0xb0000000,7)
            self.assertEqual(job.step(10),'running')
            self.assertEqual(s.object.members[0x25],int(op==0x3e))
            self.assertEqual(t.state,0);self.assertFalse(s.vm.send_pending)

    def test_receiver_arguments_do_not_replace_native_incoming_span(self):
        s,t,_=self.fixture([0x11a09e1f,receive(0x3e,offset=1,argc=1)])
        job=Delivery(s,t,0xb0000000,7)
        self.assertEqual(job.step(1),'budget')
        self.assertEqual(job.step(1),'route')
        self.assertEqual(job.receiver.arguments,(144,))
        self.assertEqual(job.arguments,[70])

    def test_handled_signal_does_not_read_invalid_unused_default(self):
        s,t,_=self.fixture([receive(0x3e,offset=1)],default=-1)
        job=Delivery(s,t,0xb0000000,7)
        self.assertEqual(job.step(1),'route');self.assertEqual(job.route.value,1)

    def test_unintegrated_handler_state_path_is_sticky_failure_not_success(self):
        s,t,_=self.fixture([0x35000001])
        job=Delivery(s,t,0xb0000000,7)
        with self.assertRaises(ValueError) as first:job.step(1)
        self.assertEqual(t.state,1) # native side effect is not rolled back or hidden
        self.assertIsNone(t.vm.receiver);self.assertTrue(s.vm.send_pending)
        with self.assertRaises(ValueError) as second:job.step(1)
        self.assertIs(first.exception,second.exception)

    def test_state_compatibility_and_exact_raw_hit_flags(self):
        for raw,expected in ((0x100,'running'),(0x1800,'route'),(0x1801,'running')):
            s,t,_=self.fixture(default=1,raw=raw,motion=2);t.object.members[0x1c]=2
            job=Delivery(s,t,0,7)
            self.assertEqual(job.step(1),expected)
            self.assertEqual(s.object.members[0x25],int(expected=='route'))
            if expected=='route':self.assertTrue(t.object.members[0x1a]&0x10000)
            else:self.assertNotIn(0x37,t.object.members)

    def test_player_mode_branch_and_missing_provider(self):
        s,t,_=self.fixture(default=1,motion=2)
        t.vm.context.player=1;t.object.members[0x32]=3
        self.assertEqual(Delivery(s,t,0,7).step(1),'running')
        self.assertEqual(s.object.members[0x25],0)
        s,t,_=self.fixture(default=1,motion=2);t.vm.context.player=1
        del t.object.members[0x32]
        with self.assertRaisesRegex(ValueError,'unbound player mode'):Delivery(s,t,0,7).step(1)

    def test_explicit_code_route_stays_pending_at_invocation_boundary(self):
        s,t,_=self.fixture(default=-32764) # unsigned 0x8004 stored by int16 table
        job=Delivery(s,t,0,7)
        self.assertEqual(job.step(1),'route');self.assertEqual((job.route.kind,job.route.value),('script',4))
        self.assertTrue(s.vm.send_pending);self.assertEqual(t.vm.pc,0)

    def test_budget_loop_can_be_aborted_without_success_or_borrowed_context(self):
        s,t,_=self.fixture([0x320003ff])
        job=Delivery(s,t,0xb0000000,7)
        self.assertEqual(job.step(20),'budget');self.assertIs(t.vm.receiver,job.receiver)
        job.abort();self.assertIsNone(t.vm.receiver);self.assertTrue(s.vm.send_pending)
        self.assertEqual(job.step(1),'aborted')

    def test_self_handler_restores_outer_pending_request_before_cleanup(self):
        s,t,_=self.fixture([receive(0x3e,action=2)],self_target=True)
        original=s.vm.send
        job=Delivery(s,t,0xb0000000,7)
        self.assertFalse(s.vm.send_pending)
        self.assertEqual(job.step(1),'running')
        self.assertEqual(s.vm.send,original);self.assertFalse(s.vm.send_pending)
        self.assertEqual(s.vm.stack.height,0)

    def test_dead_target_and_bad_span_or_tick_are_explicit(self):
        s,t,_=self.fixture();t.active=False
        job=Delivery(s,t,0,7);self.assertFalse(job.active);self.assertEqual(s.object.members[0x25],0)
        s,t,_=self.fixture([receive()])
        with self.assertRaises(ValueError):Delivery(s,t,0,7)
        self.assertIsNone(t.vm.receiver)
        with self.assertRaises(ValueError):Delivery(s,t,0xb0000000,8)

    def test_nested_send_is_exposed_for_an_owner_managed_child(self):
        s,t,_=self.fixture([send(raw=0xb7e,target=7,condition=14,argc=0),receive(0x3e,action=2)])
        parent=Delivery(s,t,0xb0000000,7)
        self.assertEqual(parent.step(4),'send')
        child=Delivery(t,s,0xb0000010,7)
        self.assertEqual(child.step(4),'running')
        self.assertEqual(parent.step(4),'running')
        self.assertFalse(s.vm.send_pending);self.assertFalse(t.vm.send_pending)
        self.assertIsNone(t.vm.receiver)


if __name__=='__main__':unittest.main()
