"""State transitions using original table layout; Python only during pause."""
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
def change(op=0x35,state=1,argc=0,condition=0):
    return op<<24|condition<<14|argc<<10|state
def words(values):return struct.pack('<%dI'%len(values),*values)


class GoolState(unittest.TestCase):
    def make(self,program=None,motion=0,interact=4,pre=12,main=8,initial_pre=0x3fff):
        code=[0x2f000000]*20
        code[:4]=program or [store(0xe1f,0xa01),store(0xe1f,0xa02),change(argc=2),0xff000000]
        code[8:11]=[store(0xe25,0xb7e),store(0xe26,0xb7f),0x31000000]
        code[12:14]=[store(0xe36,0xa03),0x31000000]
        code[15]=0x31000000
        header=bytearray(20);struct.pack_into('<I',header,12,512)
        a=SimpleNamespace(eid=123,items=[header,words(code),words([77])])
        b=SimpleNamespace(eid=456,items=[header,words(code),words([99])])
        states=struct.pack('<II4H',0,0,0,0x3fff,initial_pre,0)
        states+=struct.pack('<II4H',motion,interact,1,15,pre,main)
        base=SimpleNamespace(eid=111,items=[header,words(code),words([123,456]),b'\0\0',states,b''])
        obj=Object();context=Context([obj],0)
        i=Instance(context,base,a,Placement(34,0,(1,2,3),1,0),{111:base,123:a,456:b}.get)
        i.object.members[0x32]=0 # explicit fixture provider, not a runtime default
        return i

    def test_transition_preserves_arguments_globals_and_runs_new_handler(self):
        i=self.make();i.vm.globals=[91];i.vm.next_update=0
        self.assertEqual(i.update(7,3),'budget')
        self.assertEqual(i.state,1)
        self.assertEqual(i.phase,'pre')
        self.assertEqual(i.vm.pc,12)
        self.assertEqual(i.object.members[0x28],7)
        self.assertEqual(i.object.members[0x1c],4)
        self.assertEqual(i.object.members[0x1a],0x20020)
        self.assertEqual(i.vm.next_update,0)
        self.assertEqual(i.vm.stack.words[3],i.vm.pointer(0,8))
        self.assertEqual(i.vm.alternate,[99])
        self.assertEqual(i.vm.globals,[91])
        self.assertEqual(i.update(7,100),'done')
        self.assertEqual((i.object.members[0x25],i.object.members[0x26]),(16,32))
        self.assertEqual(i.object.members[0x36],48)
        self.assertEqual(i.vm.stack.frames,[2])

    def test_condition_and_incompatible_flags_discard_arguments(self):
        for op,condition,current,motion in ((0x36,0,0,0),(0x37,1,0,0),(0x35,0,8,8)):
            with self.subTest(op=op):
                i=self.make(program=[store(0xe1f,0xa01),change(op,argc=1,condition=0x25),
                                     store(0xe26,0xa09),0x31000000],motion=motion)
                i.object.members[0x25]=condition;i.object.members[0x1c]=current
                self.assertEqual(i.update(0,100),'done')
                self.assertEqual(i.state,0)
                self.assertEqual(i.vm.stack.height,0)
                self.assertEqual(i.object.members[0x26],144)
                self.assertNotIn(0x36,i.object.members)
                self.assertEqual(i.vm.alternate,[77])

    def test_stack_condition_consumed_once_before_arguments(self):
        for op,cond in ((0x36,1),(0x37,0)):
            i=self.make(program=[store(0xe1f,0xa05),store(0xe1f,0xa06),
                                 store(0xe1f,0xa00|cond),change(op,argc=2,condition=31)])
            self.assertEqual(i.update(0,100),'done')
            self.assertEqual((i.object.members[0x25],i.object.members[0x26]),(80,96))

    def test_alternate_main_and_no_pre_run(self):
        i=self.make(pre=0x3fff,main=0x4008)
        self.assertEqual(i.update(0,100),'done')
        self.assertEqual(i.vm.owner,1)
        self.assertEqual(i.object.members[0x22],0)
        self.assertEqual(i.object.members[0x23],i.vm.pointer(0,15))
        self.assertNotIn(0x36,i.object.members)

    def test_delay_is_reset_and_unused_stack_words_are_retained(self):
        i=self.make()
        i.vm.stack.words[100]=123456
        i.vm.set_tick(5,False);i.vm.next_update=100
        i.vm.step();i.vm.step()
        self.assertEqual(i.vm.step(),'new_state')
        self.assertEqual(i.vm.next_update,0)
        self.assertEqual(i.vm.stack.words[100],123456)
        self.assertEqual(i.object.members[0x28],5)

    def test_invalid_resume_pointer_does_not_replace_state(self):
        i=self.make(program=[change()]);i.vm.resume_frame(0)
        i.object.members[0x24]=0x60000000
        before=(i.state,i.vm.pc,i.vm.stack.words[:],i.vm.stack.top)
        with self.assertRaises(VMError):i.update(0,10)
        self.assertEqual(before,(i.state,i.vm.pc,i.vm.stack.words,i.vm.stack.top))

    def test_invalid_target_owner_or_arguments_stop_without_state_replacement(self):
        for kind in ('target','owner','args','pc'):
            with self.subTest(kind=kind):
                i=self.make(program=[change(state=2 if kind=='target' else 1,argc=1 if kind=='args' else 0)])
                if kind=='owner':i.resolve_entry=lambda eid:None
                if kind=='pc':
                    states=bytearray(i.base.items[4]);struct.pack_into('<H',states,30,0x3ffe)
                    i.base.items[4]=states
                i.vm.resume_frame(0) # initial no-RESUME token is engine work
                before=(i.state,i.object.members[0x28],i.vm.stack.top,i.vm.alternate[:])
                with self.assertRaises(VMError):i.update(4,100)
                self.assertEqual(before,(i.state,i.object.members[0x28],i.vm.stack.top,i.vm.alternate))
                self.assertEqual(i.vm.pc,0)

    def test_nested_call_frames_are_abandoned_by_transition(self):
        i=self.make(program=[0x3b000004,0xff000000])
        i.vm.codes[0][4:7]=[store(0xe1f,0xa07),store(0xe1f,0xa08),change(argc=2)]
        self.assertEqual(i.update(0,100),'done')
        self.assertEqual((i.object.members[0x25],i.object.members[0x26]),(112,128))
        self.assertEqual(len(i.vm.stack.frames),1)

    def test_transition_from_pre_run_replaces_old_handler(self):
        i=self.make(program=[0xff000000],initial_pre=4)
        i.vm.codes[0][4:8]=[store(0xe1f,0xa01),store(0xe1f,0xa02),change(argc=2),0xff000000]
        self.assertEqual(i.update(2,100),'done')
        self.assertEqual(i.state,1)
        self.assertEqual(i.object.members[0x36],48)

    def test_decode_all_state_ids_argcounts_and_unbound_service(self):
        for state in range(1024):
            seen=[];vm=VM([change(state=state,argc=state%16)])
            vm.change_state=lambda *args:seen.append(args) or 'running'
            self.assertEqual(vm.step(),'running')
            self.assertEqual(seen,[(state,state%16,True)])
        vm=VM([change(0x36,argc=1,condition=31)])
        vm.stack.push(77)
        with self.assertRaises(VMError) as error:vm.step()
        self.assertEqual(error.exception.kind,'unsupported')
        self.assertEqual(vm.stack.height,1)

    def test_original_box_state_instruction_uses_real_target_tables(self):
        arc=cb3.load_level(11)
        e=next(e for z in zdat.parse_zdat_archive(arc) for e in z.objects
               if e.file_name=='BoxsC' and e.object_id==28)
        objects=[None]*(e.lookup_index+1);objects[e.lookup_index]=Object()
        i=Instance.from_entity(arc,e,Context(objects,e.lookup_index),17)
        i.object.members[0x32]=0 # directed fixture supplies otherwise unbound mode
        i.vm.resume_frame(17)
        i.vm.pc=389 # Original unconditional state 4 instruction, no edited bytecode.
        self.assertEqual(i.vm.code[389],0x356b0004)
        self.assertEqual(i.vm.step(),'new_state')
        self.assertEqual(i.state,4)
        self.assertEqual(i.vm.pc,1762)
        self.assertEqual(i.vm.stack.words[1],i.vm.pointer(0,1762))
        self.assertEqual(i.object.members[0x22],i.vm.pointer(0,268))
        self.assertEqual(i.object.members[0x30],641)
        self.assertEqual(i.object.members[0x1c],0)
        self.assertEqual(i.object.members[0x28],17)


if __name__=='__main__':unittest.main()
