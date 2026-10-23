"""Shared RNG across engine entry/return, not just standalone arithmetic."""
import unittest
from test_gool_vm import instruction
import test_gool_state as state_fixtures
from test_gool_state import store,change
import test_gool_pump as pump_fixtures
from test_gool_receive import receive
from gool_vm import VM

RNG=instruction(0x10,0x800,0xa10)

class SharedRandomComposition(unittest.TestCase):
    def test_state_pre_and_main_use_same_shared_words(self):
        state=[0x12345678,0xabcdef01];reference=list(state)
        oracle=VM([RNG]*3,random_state=reference)
        expected=[]
        for _ in range(3):oracle.step();expected.append(oracle.stack.pop())
        i=state_fixtures.GoolState().make(program=[RNG,store(0xe25,0xe1f),change(),0xff000000])
        i.vm.random_state=state
        i.vm.codes[0][8:11]=[RNG,store(0xe26,0xe1f),0x31000000]
        i.vm.codes[0][12:15]=[RNG,store(0xe36,0xe1f),0x31000000]
        self.assertEqual(i.update(0,100),'done')
        self.assertEqual([i.object.members[n] for n in (0x25,0x36,0x26)],expected)
        self.assertIs(i.vm.random_state,state);self.assertEqual(state,reference)

    def test_signal_handler_mutates_engine_rng_without_copy_restore(self):
        state=[0x12345678,0xabcdef01];reference=list(state)
        oracle=VM([RNG],random_state=reference);oracle.step();expected=oracle.stack.pop()
        s,t,registry,m,p=pump_fixtures.GoolPump().fixture(handler=[RNG,store(0xe26,0xe1f),receive(0x3e,action=2)])
        for i in registry:i.vm.random_state=state
        p.begin(s,7,0)
        self.assertEqual(pump_fixtures.GoolPump().drain(p),'running')
        self.assertEqual(t.object.members[0x26],expected)
        self.assertEqual(state,reference)
        self.assertIs(s.vm.random_state,t.vm.random_state)
        self.assertFalse(m.spans)

    def test_abort_does_not_roll_back_completed_random_side_effects(self):
        state=[1,2];reference=list(state)
        oracle=VM([RNG],random_state=reference);oracle.step()
        s,t,registry,m,p=pump_fixtures.GoolPump().fixture(handler=[RNG,0x320003ff])
        for i in registry:i.vm.random_state=state
        p.begin(s,7,0)
        self.assertEqual(p.step(2),'budget');self.assertEqual(state,reference)
        self.assertEqual(p.abort(),'aborted');self.assertEqual(state,reference)
        self.assertFalse(m.spans)
        self.assertIsNone(t.vm.receiver)

if __name__=='__main__':unittest.main()
