"""Handlers dereference and mutate real incoming spans through VM opcodes."""
import unittest
import test_gool_delivery as fixtures
from test_gool_memory import array
from test_gool_state import store
from test_gool_receive import receive
from test_gool_send import send
from gool_delivery import Delivery
from gool_memory import Memory
from gool_vm import VMError


class DeliveryMemory(unittest.TestCase):
    def fixture(self,handler,**kwargs):
        s,t,instances=fixtures.GoolDelivery().fixture(handler,**kwargs)
        memory=Memory()
        for i in instances:i.vm.memory=memory
        return s,t,memory

    def test_handler_mutates_original_incoming_span_used_by_state_application(self):
        handler=[array(0x2a,0x800,0xb7f),store(0xe26,0xe1f),
                 store(0xe1f,0xa09),array(0x4e,0x800,0xb7f),receive(0x3e,offset=1)]
        s,t,m=self.fixture(handler)
        job=Delivery.mapped(s,t,m,7);pointer=job.span_pointer
        self.assertEqual(t.vm.stack.read(-1),pointer)
        self.assertEqual(job.step(2),'budget');self.assertEqual(t.object.members[0x26],70)
        self.assertEqual(job.step(3),'route');self.assertEqual(job.arguments,[144])
        self.assertEqual(job.receiver.arguments,());self.assertEqual(m.read(pointer),144)
        self.assertEqual(job.apply(0,1),'running')
        self.assertEqual(t.vm.stack.words[0],144);self.assertEqual(s.vm.send.arguments,(70,))
        self.assertIsNone(job.span_memory);self.assertFalse(m.spans)
        with self.assertRaises(ValueError):m.read(pointer)

    def test_self_handler_copies_modified_span_before_new_root_replaces_stack(self):
        s,t,m=self.fixture([store(0xe1f,0xa09),array(0x4e,0x800,0xb7f),
                            receive(0x3e,offset=1)],self_target=True)
        job=Delivery.mapped(s,t,m,7)
        self.assertEqual(job.step(3),'route');self.assertEqual(job.apply(0,1),'send_replaced')
        self.assertEqual((t.state,t.vm.stack.words[0]),(1,144));self.assertFalse(m.spans)
        self.assertFalse(s.vm.frame_yield)

    def test_abort_and_operand_failure_release_span_and_keep_sender_unfinished(self):
        for broken in (False,True):
            s,t,m=self.fixture([array(0x2a,0x801,0xb7f)] if broken else [0x320003ff])
            job=Delivery.mapped(s,t,m,7);pointer=job.span_pointer
            if broken:
                with self.assertRaises(VMError):job.step(1)
                self.assertFalse(t.active)
            else:self.assertEqual(job.step(4),'budget');job.abort()
            self.assertFalse(m.spans);self.assertTrue(s.vm.send_pending)
            with self.assertRaises(ValueError):m.read(pointer)
            self.assertIsNone(t.vm.receiver)

    def test_constructor_failure_none_and_empty_spans_do_not_leak(self):
        s,t,m=self.fixture([receive(0x3e,action=2)])
        with self.assertRaises(ValueError):Delivery.mapped(s,t,m,8)
        self.assertFalse(m.spans)
        self.assertEqual(Delivery.mapped(s,None,m,7).status,'running');self.assertFalse(m.spans)
        s,t,m=self.fixture([receive(0x3e,action=2)],argc=0)
        job=Delivery.mapped(s,t,m,7);pointer=job.span_pointer
        self.assertNotEqual(pointer,0)
        with self.assertRaises(ValueError):m.read(pointer)
        self.assertEqual(job.step(1),'running');self.assertFalse(m.spans)

    def test_mismatched_memory_domains_fail_before_sender_or_target_mutation(self):
        s,t,m=self.fixture([receive(0x3e,action=2)]);t.vm.memory=Memory()
        before=(s.vm.stack.top,t.vm.stack.top,t.vm.pc,s.object.members[0x25])
        with self.assertRaises(ValueError):Delivery.mapped(s,t,m,7)
        self.assertEqual(before,(s.vm.stack.top,t.vm.stack.top,t.vm.pc,s.object.members[0x25]))
        self.assertFalse(m.spans);self.assertTrue(s.vm.send_pending)

    def test_nested_child_owns_separate_span_and_preserves_parent_until_return(self):
        s,t,m=self.fixture([send(raw=0xa10,target=7,condition=14,argc=0),receive(0x3e,action=2)])
        parent=Delivery.mapped(s,t,m,7);pointer=parent.span_pointer
        self.assertEqual(parent.step(1),'send');self.assertEqual(m.read(pointer),70)
        child=Delivery.mapped(t,s,m,7);self.assertEqual(len(m.spans),2)
        self.assertNotEqual(parent.span_pointer,child.span_pointer)
        self.assertEqual(child.step(1),'running');self.assertEqual(len(m.spans),1)
        self.assertEqual(m.read(pointer),70)
        self.assertEqual(parent.step(1),'running');self.assertFalse(m.spans)


if __name__=='__main__':unittest.main()
