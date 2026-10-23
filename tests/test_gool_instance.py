"""Entity lifecycle traces. These execute Python, never compile native C."""
from pathlib import Path
import struct
import sys
from types import SimpleNamespace
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from gool_instance import Instance,Placement
from gool_object import Object,Context
from gool_vm import VM,VMError
import cb3
import zdat


def store(dest,src):return 0x11000000|src<<12|dest


class GoolInstance(unittest.TestCase):
    def make(self,pre=4,main=0,alternate_code=None,args=(),tick=0):
        # Main copies handler's counter, waits three ticks, then repeats.
        # Handler increments counter by literal 1/256 in GOOL's encoding.
        code=[store(0xe25,0xe26),0x39c30000|0xa00,0x320003fd,0x2f000000,
              0x00a01e26,store(0xe26,0xe1f),0x31000000]
        header=bytearray(20);struct.pack_into('<I',header,12,512)
        def words(values):return struct.pack('<%dI'%len(values),*values)
        alternate=SimpleNamespace(eid=123,items=[header,words(alternate_code or code),words([123])])
        base=SimpleNamespace(eid=111,items=[header,words(code),words([123]),b'\0\0',
                         struct.pack('<II4H',7,9,0,0x3fff,pre,main),b''])
        obj=Object();ctx=Context([obj],0)
        instance=Instance(ctx,base,alternate,Placement(12,0,(100,-200,300),2,4,args,tick))
        obj.members[0x26]=0
        return instance

    def test_initial_state_parameters_and_known_providers(self):
        i=self.make(args=(77,-4),tick=11)
        self.assertEqual(i.vm.stack.read(-2),77)
        self.assertEqual(i.vm.stack.read(-1),-4)
        self.assertEqual([i.object.members[n] for n in (8,9,10)], [100,-200,300])
        self.assertEqual([i.object.members[n] for n in (14,15,16)], [4096]*3)
        self.assertEqual(i.object.members[0x1d],12<<16)
        self.assertEqual(i.object.members[0x2e],512)
        self.assertEqual(i.object.members[0x28],11)
        self.assertEqual(i.object.members[0x27],-1)
        self.assertNotIn(0x1b,i.object.members) # Flags provider still absent.

    def test_pre_run_executes_before_main_even_during_delay(self):
        i=self.make()
        self.assertEqual(i.update(0,100),'wait')
        self.assertEqual((i.object.members[0x25],i.object.members[0x26]),(16,16))
        self.assertEqual(i.update(1,100),'wait')
        self.assertEqual((i.object.members[0x25],i.object.members[0x26]),(16,32))
        self.assertEqual(i.update(2,100),'wait')
        self.assertEqual(i.update(3,100),'wait')
        self.assertEqual((i.object.members[0x25],i.object.members[0x26]),(64,64))
        self.assertEqual(i.vm.next_update,6)
        self.assertEqual(i.object.members[0x27],3)

    def test_budget_continues_same_tick_without_reentering_handler(self):
        i=self.make()
        self.assertEqual(i.update(5,1),'budget')
        with self.assertRaises(ValueError):i.update(6,1)
        for _ in range(20):
            result=i.update(5,1)
            if result!='budget':break
        self.assertEqual(result,'wait')
        self.assertEqual(i.object.members[0x26],16)
        self.assertEqual(len(i.vm.stack.frames),1)
        self.assertEqual(i.update(5,100),'skipped')
        with self.assertRaises(ValueError):i.update(4,100)

    def test_live_pre_run_can_be_disabled_or_replaced(self):
        i=self.make();i.update(0,100)
        i.object.members[0x22]=0
        self.assertEqual(i.update(1,100),'wait')
        self.assertEqual(i.object.members[0x26],16)
        i.object.members[0x22]=i.vm.pointer(1,4)
        self.assertEqual(i.update(2,100),'wait')
        self.assertEqual(i.object.members[0x26],32)
        i.object.members[0x22]=0x60000000
        before=(i.vm.tick,i.object.members[0x27],i.vm.stack.top)
        with self.assertRaises(ValueError):i.update(3,100)
        self.assertEqual(before,(i.vm.tick,i.object.members[0x27],i.vm.stack.top))

    def test_alternate_handler_returns_to_unexecuted_main_pc(self):
        i=self.make(pre=0x4004)
        i.update(0,100)
        self.assertEqual(i.vm.owner,0)
        self.assertEqual(i.object.members[0x25],16)
        # An alternate main remains alternate after the handler returns.
        j=self.make(pre=4,main=0x4000)
        j.update(0,100)
        self.assertEqual(j.vm.owner,1)
        self.assertEqual(j.object.members[0x25],16)

    def test_done_error_zero_budget_and_handler_loop(self):
        i=self.make(pre=0x3fff,main=6)
        self.assertEqual(i.update(0,0),'budget')
        self.assertEqual(i.object.members[0x27],-1)
        self.assertEqual(i.update(0,1),'done')
        self.assertEqual(i.update(5,10),'done')
        j=self.make(pre=0x4000,alternate_code=[0xff000000])
        with self.assertRaises(VMError):j.update(0,100)
        self.assertFalse(j.active)
        with self.assertRaises(VMError):j.update(1,100)
        k=self.make(pre=0x4000,alternate_code=[0x320003ff])
        self.assertEqual(k.update(0,100),'budget')
        self.assertEqual(k.phase,'pre')
        self.assertNotIn(0x25,k.object.members)

    def test_vm_invoke_preserves_current_pc_and_rejects_invalid_atomically(self):
        vm=VM([0x2f000000,0x31000000],alternate_code=[0x31000000])
        vm.invoke(1,0);vm.step()
        self.assertEqual((vm.owner,vm.pc),(0,0))
        before=(vm.owner,vm.pc,vm.stack.top,len(vm.stack.frames))
        for owner,pc,argc in ((2,0,0),(1,1,0),(1,0,1)):
            with self.assertRaises(ValueError):vm.invoke(owner,pc,argc)
            self.assertEqual(before,(vm.owner,vm.pc,vm.stack.top,len(vm.stack.frames)))

    def test_original_zdat_uses_scaled_lookup_and_real_initial_state(self):
        arc=cb3.load_level(11)
        entities=[e for zone in zdat.parse_zdat_archive(arc) for e in zone.objects
                  if e.file_name=='BoxsC' and e.object_id==28 and e.positions]
        self.assertTrue(entities)
        e=entities[0];objects=[None]*(e.lookup_index+1);objects[e.lookup_index]=Object()
        i=Instance.from_entity(arc,e,Context(objects,e.lookup_index),17)
        self.assertEqual(i.state,8)
        self.assertEqual(i.object.members[0x1e],e.lookup_index<<8)
        self.assertEqual(i.object.members[8],e.positions[0][0])
        self.assertEqual(i.object.members[0x2e],len(e.path)<<8)
        i.vm.globals=[0]*512;i.vm.globals[0]=11<<8
        with self.assertRaises(VMError):i.update(17,100)
        # Prefix reached the unbound PLAYER service, not a fabricated success.
        self.assertEqual(i.vm.pc,1997)
        self.assertEqual(i.object.members[0x27],17)

    def test_all_original_placements_initialize_or_explicitly_have_no_state(self):
        initialized=no_state=0
        for lid in cb3.all_level_ids():
            arc=cb3.load_level(lid)
            for zone in zdat.parse_zdat_archive(arc):
                for e in zone.objects:
                    table=zdat.parse_gool_table(arc.entry(e.file_name))
                    state=table.object_indices[e.object_id]
                    objects=[None]*(e.lookup_index+1);objects[e.lookup_index]=Object()
                    context=Context(objects,e.lookup_index)
                    if state==255:
                        with self.assertRaisesRegex(ValueError,'invalid initial state'):
                            Instance.from_entity(arc,e,context)
                        self.assertIsNone(objects[e.lookup_index].vm)
                        no_state+=1
                    else:
                        i=Instance.from_entity(arc,e,context)
                        self.assertEqual(i.state,state)
                        self.assertEqual(i.object.members[0x1e],e.lookup_index<<8)
                        self.assertEqual(tuple(i.object.members[8+n] for n in range(3)),tuple(e.positions[0]))
                        self.assertEqual(i.vm.stack.words[:len(e.parameters)],e.parameters)
                        initialized+=1
        self.assertEqual((initialized,no_state),(7682,79))


if __name__=='__main__':unittest.main()
