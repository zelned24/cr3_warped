"""RESUME mode 0x13 composition; C remains uncompiled during user pause."""
import unittest
import test_gool_state as fixtures
import test_gool_delivery as delivery_fixtures
from test_gool_jalr import call
from gool_vm import VM,VMError,Receiver
from test_gool_vm import set_frame
from test_gool_pointer import pointer
from gool_delivery import Delivery
from gool_instance import Instance
from gool_object import Object,Context
import cb3
import zdat


class GoolResume(unittest.TestCase):
    def instance(self,pre=12,alternate=False):
        i=fixtures.GoolState().make(pre=pre)
        code=[0x2f000000]*8
        code[4:6]=[fixtures.store(0xe35,0xa07),0x31000000]
        if alternate:
            old=i.resolve_entry(123);old.items[1]=fixtures.words(code)
            i.vm.codes[1][:]=code
            # New alternate deliberately has a different instruction at PC 4.
            new=i.resolve_entry(456);modified=list(i.vm.codes[0]);modified[4]=0xff000000
            new.items[1]=fixtures.words(modified)
        else:i.vm.codes[0][4:6]=code[4:6]
        i.object.members[0x24]=i.vm.pointer(int(alternate),4)
        return i

    def test_vm_resume_return_pushes_exact_zero_and_keeps_root_parameters(self):
        vm=VM([0x2f000000,0x31000000],args=(77,));vm.invoke(0,1)
        vm.mode=0x13;vm.respect_delay=False
        self.assertEqual(vm.step(),'resume_return')
        self.assertEqual(vm.stack.read(-1),77);self.assertTrue(vm.frame_yield)
        self.assertEqual(vm.stack.words[vm.frame_token_index],0)
        self.assertEqual((vm.pc,len(vm.stack.frames)),(0,1))

    def test_nested_jalr_return_does_not_end_resume_or_seed_a_second_token(self):
        vm=VM([0x2f000000,call(0xe25,0x800),0x31000000,0x31000000],members={0x25:0x5000000c})
        vm.invoke(0,1);vm.mode=0x13;vm.respect_delay=False
        self.assertEqual(vm.step(),'running');self.assertEqual(vm.mode,0x12)
        self.assertEqual(vm.step(),'running');self.assertFalse(vm.frame_yield)
        self.assertEqual(vm.mode,0x13);self.assertEqual(vm.pc,2)
        self.assertEqual(vm.step(),'resume_return');self.assertEqual(vm.stack.height,1)

    def test_instance_runs_old_resume_then_new_pre_then_main_with_same_tick_budget(self):
        i=self.instance()
        self.assertEqual(i.update(7,3),'budget');self.assertEqual(i.phase,'resume')
        self.assertEqual(i.object.members[0x24],0);self.assertFalse(i.vm.frame_yield)
        self.assertEqual(i.update(7,1),'budget');self.assertEqual(i.object.members[0x35],112)
        with self.assertRaises(ValueError):i.update(8,1)
        self.assertEqual(i.update(7,1),'budget');self.assertEqual(i.phase,'pre')
        self.assertTrue(i.vm.frame_yield);self.assertFalse(i.resume_active)
        self.assertEqual(i.update(7,100),'done')
        self.assertEqual(i.object.members[0x36],48)
        self.assertEqual((i.object.members[0x25],i.object.members[0x26]),(16,32))

    def test_resume_retains_old_alternate_code_after_owner_changes(self):
        i=self.instance(pre=0x3fff,alternate=True)
        self.assertEqual(i.update(7,3),'budget');self.assertGreaterEqual(i.vm.owner,2)
        self.assertEqual(i.update(7,2),'budget');self.assertEqual(i.object.members[0x35],112)
        self.assertEqual(i.vm.alternate,[99]);self.assertEqual(i.vm.owner,0)
        self.assertEqual(i.update(7,20),'done')

    def test_foreign_signal_entry_executes_resume_now_and_defers_pre_until_update(self):
        s,t,_=delivery_fixtures.GoolDelivery().fixture(default=1)
        t.vm.codes[0][4:6]=[fixtures.store(0xe35,0xa07),0x31000000]
        t.base.items[1]=delivery_fixtures.words(t.vm.codes[0])
        t.object.members[0x24]=t.vm.pointer(0,4)
        job=Delivery(s,t,0,7);self.assertEqual(job.step(1),'route')
        self.assertEqual(job.apply(0,1),'budget');self.assertEqual(t.object.members[0x35],112)
        self.assertTrue(s.vm.send_pending)
        self.assertEqual(job.step(1),'running');self.assertFalse(s.vm.send_pending)
        self.assertTrue(t.vm.frame_yield);self.assertFalse(t.resume_active)
        self.assertEqual(t.object.members[0x24],0)

    def test_self_signal_resume_finishes_before_wrapper_pops_new_token(self):
        s,t,_=delivery_fixtures.GoolDelivery().fixture(default=1,self_target=True)
        t.vm.codes[0][4:6]=[fixtures.store(0xe35,0xa07),0x31000000]
        t.base.items[1]=delivery_fixtures.words(t.vm.codes[0]);t.object.members[0x24]=t.vm.pointer(0,4)
        job=Delivery(s,t,0,7);job.step(1)
        self.assertEqual(job.apply(0,1),'budget');self.assertEqual(job.step(1),'send_replaced')
        self.assertEqual(t.object.members[0x35],112);self.assertFalse(t.vm.frame_yield)
        self.assertEqual(t.vm.stack.height,0);self.assertEqual(t.vm.stack.words[0],70)

    def test_pending_resume_rejects_next_tick_and_nested_state_entry(self):
        i=self.instance(pre=0x3fff);i.vm.resume_frame(0)
        self.assertEqual(i.enter_state(1,(1,2)),'new_state')
        with self.assertRaisesRegex(ValueError,'same tick'):i.update(1,1)
        before=(i.state_generation,i.vm.stack.top)
        with self.assertRaisesRegex(ValueError,'nested state entry'):i.enter_state(0,())
        self.assertEqual(before,(i.state_generation,i.vm.stack.top))

    def test_state_change_from_resume_is_explicitly_unsupported(self):
        i=self.instance();i.vm.codes[0][4]=fixtures.change()
        self.assertEqual(i.update(7,3),'budget')
        with self.assertRaisesRegex(VMError,'during RESUME'):i.update(7,1)
        self.assertEqual(i.state_generation,2);self.assertEqual(i.state,1)

    def test_old_alternate_resume_uses_new_object_alternate_constants(self):
        i=self.instance(pre=0x3fff,alternate=True)
        i.vm.codes[1][4]=fixtures.store(0xe35,0x400)
        self.assertEqual(i.update(7,3),'budget')
        self.assertEqual(i.update(7,2),'budget');self.assertEqual(i.object.members[0x35],99)

    def test_old_code_owner_aliases_are_reused_across_state_cycles(self):
        i=self.instance(pre=0x3fff,alternate=True)
        b=i.resolve_entry(456);code=list(i.vm.codes[0]);code[4:6]=[fixtures.store(0xe35,0xa08),0x31000000]
        b.items[1]=fixtures.words(code)
        for state in (1,0)*6:
            i.object.members[0x24]=i.vm.pointer(1,4)
            self.assertEqual(i.enter_state(state,()),'new_state');i.start_resume()
            self.assertEqual(i.vm.step(),'running');self.assertEqual(i.vm.step(),'resume_return')
            i.resume_active=False
        self.assertEqual(len(i._resume_owners),2);self.assertEqual(set(i.vm.codes),{0,1,2,3})

    def test_cancelled_resume_cannot_be_run_as_an_ordinary_actor_update(self):
        s,t,_=delivery_fixtures.GoolDelivery().fixture(default=1)
        t.vm.codes[0][4]=0x320003ff;t.object.members[0x24]=t.vm.pointer(0,4)
        job=Delivery(s,t,0,7);job.step(1);self.assertEqual(job.apply(0,1),'budget')
        job.abort()
        with self.assertRaisesRegex(ValueError,'abandoned context'):t.update(7,1)
        self.assertTrue(s.vm.send_pending)

    def test_original_boxsc_resume_return_queues_pre_before_executing_its_body(self):
        arc=cb3.load_level(11)
        entity=next(e for z in zdat.parse_zdat_archive(arc) for e in z.objects
                    if e.file_name=='BoxsC' and e.object_id==28)
        objects=[None]*(entity.lookup_index+1);objects[entity.lookup_index]=Object()
        i=Instance.from_entity(arc,entity,Context(objects,entity.lookup_index),17)
        self.assertEqual(i.vm.codes[0][68],0x31894000)
        i.object.members[0x24]=i.vm.pointer(0,68)
        self.assertEqual(i.enter_state(4,(-123,456)),'new_state')
        self.assertEqual(i.update(17,1),'budget')
        self.assertEqual((i.phase,i.vm.pc),('pre',268))
        self.assertTrue(i.vm.frame_yield);self.assertEqual(i.vm.stack.words[i.vm.frame_token_index],0)
        self.assertFalse(i.resume_active);self.assertEqual(i.vm.stack.words[:2],[-123,456])

    def test_resume_animation_token_is_local_until_return_seeds_zero(self):
        i=self.instance(pre=0x3fff)
        i.vm.codes[0][4:6]=[set_frame(0xa01,duration=3),0x31000000]
        self.assertEqual(i.update(7,3),'budget')
        self.assertEqual(i.update(7,1),'budget');self.assertFalse(i.vm.frame_yield)
        self.assertEqual(i.object.members[0x2b],16)
        self.assertEqual(i.update(7,1),'budget');self.assertTrue(i.vm.frame_yield)
        self.assertEqual(i.vm.stack.words[i.vm.frame_token_index],0)

    def test_resume_can_install_callback_for_next_state_entry(self):
        i=self.instance(pre=0x3fff);i.vm.codes[0][4:6]=[pointer(0x24,6),0x31000000]
        self.assertEqual(i.update(7,3),'budget');self.assertEqual(i.object.members[0x24],0)
        self.assertEqual(i.update(7,2),'budget')
        self.assertEqual(i.object.members[0x24],i.vm.pointer(0,6))

    def test_engine_or_signal_return_does_not_restore_arbitrary_saved_frame_flags(self):
        for mode in (3,8,0x13):
            vm=VM([0x2f000000,0x31000000]);vm.invoke(0,1)
            vm.stack.words[vm.stack.frames[-1]]=0;vm.mode=mode;vm.respect_delay=False
            if mode==8:vm.receiver=Receiver()
            result=vm.step()
            self.assertEqual(result,{3:'running',8:'failed_signal',0x13:'resume_return'}[mode])
            self.assertEqual(vm.mode,mode)


if __name__=='__main__':unittest.main()
