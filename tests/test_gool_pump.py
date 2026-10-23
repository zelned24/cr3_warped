"""Owner-level delivery composition. No C compilation or world-order claim."""
import unittest
import struct
from dataclasses import replace
import test_gool_delivery as fixtures
import test_gool_delivery_apply as apply_fixtures
from test_gool_receive import receive
from test_gool_send import send
from test_gool_state import store
from gool_memory import Memory
from gool_pump import Pump


class GoolPump(unittest.TestCase):
    def fixture(self,**kwargs):
        s,t,instances=fixtures.GoolDelivery().fixture(**kwargs)
        m=Memory()
        for i in instances:i.vm.memory=m
        return s,t,instances,m,Pump(instances,m)

    def drain(self,pump,budget=1):
        for _ in range(100):
            result=pump.step(budget)
            if result!='budget':return result
        self.fail('unexpected pump nontermination')

    def test_zero_budget_and_no_handler_complete_only_pending_wrapper(self):
        s,t,_,m,p=self.fixture()
        before=(s.vm.stack.top,t.vm.stack.top,s.vm.pc,t.vm.pc,dict(t.object.members))
        self.assertEqual(p.begin(s,7,0),'budget');self.assertEqual(p.step(0),'budget')
        self.assertFalse(m.spans)
        self.assertEqual(before,(s.vm.stack.top,t.vm.stack.top,s.vm.pc,t.vm.pc,dict(t.object.members)))
        self.assertEqual(p.step(1),'budget');self.assertEqual(len(m.spans),1)
        self.assertEqual(p.step(1),'running');self.assertFalse(p.active)
        self.assertEqual(s.vm.pc,1);self.assertEqual(s.vm.stack.height,0)
        self.assertFalse(s.vm.send_pending);self.assertFalse(m.spans)
        self.assertEqual(p.step(100),'running')

    def test_handler_selection_and_state_application_use_single_owner_budget(self):
        s,t,_,m,p=self.fixture(handler=[receive(0x3e,offset=1)],argc=2)
        p.begin(s,7,0)
        self.assertEqual(p.step(1),'budget');self.assertEqual(t.state,0)
        self.assertEqual(p.step(1),'budget');self.assertEqual(t.state,0)
        self.assertEqual(p.jobs[-1].status,'route');self.assertTrue(s.vm.send_pending)
        self.assertEqual(p.step(1),'running')
        self.assertEqual((t.state,t.state_generation),(1,2));self.assertEqual(t.vm.stack.words[:2],[70,71])
        self.assertTrue(t.vm.frame_yield);self.assertFalse(m.spans)

    def test_child_completes_before_parent_and_keeps_parent_span(self):
        handler=[send(raw=0xa10,target=7,condition=14,argc=0),receive(0x3e,action=2)]
        s,t,_,m,p=self.fixture(handler=handler)
        p.begin(s,7,0)
        self.assertEqual(p.step(2),'budget');self.assertIs(p.pending,t)
        pointer=p.jobs[0].span_pointer;self.assertEqual(m.read(pointer),70)
        self.assertEqual(p.step(0),'budget');self.assertEqual(len(m.spans),1)
        self.assertEqual(p.step(1),'budget');self.assertEqual(len(p.jobs),2);self.assertEqual(len(m.spans),2)
        self.assertEqual(p.step(1),'budget');self.assertEqual(len(p.jobs),1)
        self.assertEqual(m.read(pointer),70)
        self.assertEqual(p.step(1),'running');self.assertFalse(m.spans)
        self.assertIsNone(t.vm.receiver);self.assertFalse(s.vm.send_pending)

    def test_self_state_pre_runs_once_then_instance_owner_resumes_main(self):
        s,t,_,m,p=self.fixture(default=1,argc=2,self_target=True,publish=False)
        apply_fixtures.DeliveryApplication().destination(t)
        self.assertEqual(s.update(7,1),'send');p.begin(s,7,0)
        self.assertEqual(self.drain(p),'send_replaced')
        self.assertEqual(t.object.members[0x26],16);self.assertEqual(t.phase,'main')
        self.assertNotIn(0x36,t.object.members) # pump never runs new main
        self.assertEqual(s.update(7,10),'done')
        self.assertEqual(t.object.members[0x26],16);self.assertEqual(t.object.members[0x36],71)
        self.assertFalse(m.spans)

    def test_large_and_single_unit_budgets_have_same_observable_outcome(self):
        results=[]
        for budget in (1,100):
            s,t,instances,m,p=self.fixture(handler=[send(raw=0xa10,target=7,condition=14,argc=0),
                                                  receive(0x3e,action=2)])
            p.begin(s,7,0);result=self.drain(p,budget)
            results.append((result,[(i.state,i.vm.pc,i.vm.mode,dict(i.object.members),
                                    i.vm.stack.words[:i.vm.stack.top],i.vm.frame_yield) for i in instances]))
            self.assertFalse(m.spans)
        self.assertEqual(results[0],results[1])

    def test_explicit_script_is_resumable_and_abort_releases_context(self):
        s,t,_,m,p=self.fixture(default=-32764)
        t.vm.codes[0][4]=0x320003ff
        p.begin(s,7,0);self.assertEqual(p.step(20),'budget')
        self.assertTrue(p.active);self.assertEqual(t.vm.mode,3);self.assertTrue(m.spans)
        self.assertEqual(p.abort(),'aborted');self.assertEqual(p.step(1),'aborted')
        self.assertFalse(m.spans);self.assertTrue(s.vm.send_pending)
        self.assertEqual(t.vm.mode,0);self.assertTrue(t.vm.respect_delay)

    def test_empty_slot_and_missing_live_provider_are_distinct(self):
        for absent in (True,False):
            s,t,registry,m,p=self.fixture();registry[1]=None
            if absent:s.vm.context.objects[1]=None
            p.begin(s,7,0)
            if absent:
                self.assertEqual(p.step(1),'running');self.assertEqual(s.object.members[0x25],0)
                self.assertFalse(s.vm.send_pending)
            else:
                with self.assertRaisesRegex(ValueError,'lacks instance provider'):p.step(1)
                self.assertTrue(s.vm.send_pending)
            self.assertFalse(m.spans)

    def test_bad_pointer_domain_slot_and_timeline_fail_without_next_fetch(self):
        for kind in ('pointer','domain','slot','tick'):
            s,t,registry,m,p=self.fixture(handler=[0x320003ff])
            p.begin(s,7,0)
            if kind=='pointer':s.vm.send=replace(s.vm.send,target=0x60000005)
            else:
                self.assertEqual(p.step(1),'budget')
                if kind=='domain':t.vm.memory=Memory()
                if kind=='slot':registry[1]=None
                if kind=='tick':t.vm.set_tick(8)
            pc=t.vm.pc
            with self.assertRaises(ValueError) as first:p.step(1)
            with self.assertRaises(ValueError) as second:p.step(1)
            self.assertIs(first.exception,second.exception)
            self.assertEqual(t.vm.pc,pc);self.assertFalse(p.active);self.assertFalse(m.spans)
            self.assertTrue(s.vm.send_pending);self.assertIsNone(t.vm.receiver)

    def test_registry_alias_cannot_supply_another_slot_or_current_index(self):
        s,t,registry,m,p=self.fixture();registry[1]=s
        with self.assertRaises(ValueError):p.begin(s,7,1)
        self.assertFalse(m.spans)
        p.begin(s,7,0)
        with self.assertRaisesRegex(ValueError,'slot mismatch'):p.step(1)
        self.assertTrue(s.vm.send_pending)

    def test_nested_capacity_unwinds_in_reverse_without_claiming_delivery(self):
        s,t,_,m,p=self.fixture(handler=[send(raw=0xa10,target=7,condition=14,argc=0)])
        s.object.members[0x23]=s.vm.pointer(0,2)
        s.vm.codes[0][2]=send(raw=0xa10,target=7,condition=14,argc=0)
        p.begin(s,7,0)
        with self.assertRaisesRegex(ValueError,'nesting capacity'):p.step(1000)
        self.assertFalse(p.active);self.assertFalse(p.jobs);self.assertFalse(m.spans)
        self.assertIsNone(s.vm.receiver);self.assertIsNone(t.vm.receiver)
        self.assertTrue(s.vm.send_pending);self.assertFalse(t.vm.send_pending)

    def test_begin_rejects_second_root_without_disturbing_live_parent(self):
        s,t,_,m,p=self.fixture(handler=[0x320003ff])
        p.begin(s,7,0);p.step(1);pointer=p.jobs[0].span_pointer
        with self.assertRaises(ValueError):p.begin(s,7,0)
        self.assertTrue(p.active);self.assertEqual(m.read(pointer),70)
        p.abort();self.assertFalse(m.spans)

    def test_child_can_replace_original_sender_then_outer_wrapper_cleans_new_token(self):
        s,t,_,m,p=self.fixture(handler=[send(raw=0xa10,target=7,condition=14,argc=0),
                                      receive(0x3e,action=2)])
        s.vm.codes[0][2]=receive(0x3e,offset=1)
        s.object.members[0x23]=s.vm.pointer(0,2)
        p.begin(s,7,0)
        self.assertEqual(self.drain(p),'send_replaced')
        self.assertEqual((s.state,s.state_generation),(1,2))
        self.assertEqual(s.vm.stack.height,0);self.assertFalse(s.vm.frame_yield)
        self.assertFalse(s.vm.send_pending);self.assertFalse(m.spans)

    def test_unimplemented_outer_receiver_state_mode_fails_and_keeps_script_effects(self):
        s,t,_,m,p=self.fixture(handler=[store(0xe23,0xbe0),
                                      send(raw=0xa10,target=0,condition=14,argc=0)])
        table=bytearray(t.base.items[3]);struct.pack_into('<H',table,2,1);t.base.items[3]=table
        p.begin(s,7,0)
        with self.assertRaisesRegex(ValueError,'suspended foreign context'):p.step(30)
        self.assertEqual(t.state,0);self.assertEqual(t.object.members[0x23],0)
        self.assertTrue(s.vm.send_pending);self.assertFalse(m.spans)
        self.assertIsNone(t.vm.receiver)

    def test_span_exhaustion_preserves_storage_owned_by_other_engine_services(self):
        s,t,_,m,p=self.fixture(handler=[send(raw=0xa10,target=7,condition=14,argc=0)])
        borrowed=[m.borrow([n]) for n in range(31)]
        p.begin(s,7,0)
        with self.assertRaisesRegex(ValueError,'memory domain capacity'):p.step(20)
        self.assertEqual(len(m.spans),31)
        for n,pointer in enumerate(borrowed):self.assertEqual(m.read(pointer),n)
        self.assertTrue(s.vm.send_pending);self.assertIsNone(t.vm.receiver)

    def test_self_resume_and_pre_are_completed_before_owner_main_continuation(self):
        s,t,_,m,p=self.fixture(default=1,argc=2,self_target=True,publish=False)
        apply_fixtures.DeliveryApplication().destination(t)
        t.vm.codes[0][9:11]=[store(0xe35,0xa07),0x31000000]
        t.base.items[1]=fixtures.words(t.vm.codes[0]);t.object.members[0x24]=t.vm.pointer(0,9)
        self.assertEqual(s.update(7,1),'send');p.begin(s,7,0)
        self.assertEqual(self.drain(p),'send_replaced')
        self.assertEqual((t.object.members[0x35],t.object.members[0x26]),(112,16))
        self.assertFalse(t.resume_active);self.assertFalse(m.spans)
        self.assertEqual(s.update(7,10),'done');self.assertEqual(t.object.members[0x36],71)

    def test_resume_for_foreign_destination_completes_inside_pump(self):
        s,t,_,m,p=self.fixture(default=1)
        t.vm.codes[0][4:6]=[store(0xe35,0xa07),0x31000000]
        t.base.items[1]=fixtures.words(t.vm.codes[0]);t.object.members[0x24]=t.vm.pointer(0,4)
        p.begin(s,7,0);self.assertEqual(self.drain(p),'running')
        self.assertEqual(t.object.members[0x35],112);self.assertFalse(t.resume_active)
        self.assertTrue(t.vm.frame_yield);self.assertFalse(m.spans)


if __name__=='__main__':unittest.main()
