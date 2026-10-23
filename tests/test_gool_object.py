"""Pure Python object context checks; C counterpart remains uncompiled."""
from pathlib import Path
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from gool_object import Object,Context,ObjectVM
from gool_vm import VMError
from gool_memory import Memory
import cb3


class GoolObject(unittest.TestCase):
    def setup_objects(self):
        a,b=Object(),Object()
        objects=[a,b,None]
        ctx=Context(objects,0,player=1)
        vm=ObjectVM(ctx,[0x2f000000]*64)
        return a,b,ctx,vm

    def test_known_zero_vs_unknown(self):
        a,_,ctx,_=self.setup_objects()
        with self.assertRaises(ValueError):ctx.read(0,0x27)
        a.members[0x27]=0
        self.assertEqual(ctx.read(0,0x27),0)

    def test_links_and_indirect_write(self):
        a,b,ctx,vm=self.setup_objects()
        a.members[1]=ctx.pointer(1);b.members[0x25]=75
        self.assertEqual(ctx.read(1,0x25),75)
        self.assertEqual(vm.read(0xc65),75) # PARENT.TEMP_A
        vm.write(0xc65,99);self.assertEqual(b.members[0x25],99)
        self.assertEqual(ctx.read(0,0),0x60000000)
        self.assertEqual(ctx.read(1,0),0x60000004)
        self.assertEqual(ctx.read(0,5),0x60000004)
        self.assertEqual(ctx.read(5,0x25),99)

    def test_out_of_bounds_removed_and_wrong_type_links(self):
        a,b,ctx,vm=self.setup_objects()
        for pointer in (0x50000004,0x60000005,0x60000008,0x600000fc):
            a.members[1]=pointer
            with self.assertRaises(ValueError):ctx.read(1,0x25)
        a.members[1]=ctx.pointer(1);ctx.objects[1]=None
        with self.assertRaises(ValueError):vm.read(0xc65)

    def test_null_link_reads_zero_without_resolving_member_or_stack(self):
        a,_,ctx,vm=self.setup_objects()
        for base in (1,2,3,4,6,7):
            with self.assertRaises(ValueError):ctx.read(base,0x25)
            a.members[base]=0
            for member in range(64):
                self.assertEqual(vm.read(0xc00|(base<<6)|member),0)
                with self.assertRaises(ValueError):vm.write(0xc00|(base<<6)|member,99)
        self.assertEqual(vm.stack.height,0)

    def test_remote_stack_and_script_pointer_rejection(self):
        a,b,ctx,vm=self.setup_objects()
        a.members[1]=ctx.pointer(1)
        other=ObjectVM(Context(ctx.objects,1),[0x2f000000])
        other.stack.push(55)
        before=other.stack.top
        with self.assertRaises(ValueError):vm.read(0xc5f)
        memory=Memory();vm.memory=memory;other.memory=memory;pointer=b.bind_stack(memory)
        self.assertEqual(vm.read(0xc5f),pointer+before*4)
        self.assertEqual(other.stack.top,before)
        self.assertEqual(memory.read(vm.read(0xc5f)-4),55)
        with self.assertRaises(ValueError):vm.write(0xc5f,66)
        self.assertEqual(other.stack.top,before)
        self.assertEqual(other.stack.pop(),55)
        b.unbind_stack()
        with self.assertRaises(ValueError):vm.read(0xc5f)
        with self.assertRaises(ValueError):ctx.read(1,32)
        self.assertEqual(vm.read(0xe20),0x50000004)

    def test_self_indirect_stack_is_address_but_direct_pops(self):
        a,_,ctx,vm=self.setup_objects();memory=Memory();vm.memory=memory;pointer=a.bind_stack(memory)
        vm.stack.push(77);height=vm.stack.top
        self.assertEqual(vm.read(0xc1f),pointer+height*4)
        self.assertEqual(vm.stack.top,height)
        self.assertEqual(vm.read(0xe1f),77)
        self.assertEqual(vm.stack.top,height-1)
        a.unbind_stack()

    def test_stack_mapping_lifetime_capacity_and_one_past(self):
        a,_,ctx,vm=self.setup_objects();memory=Memory();vm.memory=memory;pointer=a.bind_stack(memory)
        with self.assertRaises(ValueError):a.bind_stack(memory)
        vm.stack.top=len(vm.stack.words)
        self.assertEqual(ctx.read(0,31),pointer+len(vm.stack.words)*4)
        with self.assertRaises(ValueError):memory.read(ctx.read(0,31))
        memory.release(pointer)
        with self.assertRaises(ValueError):ctx.read(0,31)
        with self.assertRaises(ValueError):a.unbind_stack()
        for _ in range(32):memory.borrow([])
        with self.assertRaises(ValueError):a.bind_stack(memory)
        self.assertIsNone(a.stack_memory)

    def test_distinct_domains_cannot_alias_foreign_stack(self):
        a,b,ctx,vm=self.setup_objects();a.members[1]=ctx.pointer(1)
        other=ObjectVM(Context(ctx.objects,1),[0x2f000000])
        mine,theirs=Memory(),Memory();vm.memory=mine;other.memory=theirs
        with self.assertRaises(ValueError):b.bind_stack(mine)
        b.bind_stack(theirs)
        with self.assertRaises(ValueError):vm.read(0xc5f)
        b.unbind_stack()

    def test_timestamp_provider_and_frame_overlay(self):
        a,_,ctx,vm=self.setup_objects()
        vm.begin_update(0);self.assertEqual(vm.read(0xe27),0)
        vm.begin_update(17);self.assertEqual(vm.read(0xe27),17)
        with self.assertRaises(ValueError):vm.begin_update(16)
        self.assertEqual(a.members[0x27],17)
        a.stack_start=100;a.members[100]=999
        with self.assertRaises(ValueError):ctx.read(0,100)

    def test_real_clock_prefix_uses_update_timestamp(self):
        e=cb3.load_level(11).entry('BoxsC')
        code=struct.unpack('<%dI'%(len(e.items[1])//4),e.items[1])
        constants=struct.unpack('<%dI'%(len(e.items[2])//4),e.items[2])
        pc=struct.unpack_from('<H',e.items[4],8*16+14)[0]&0x3fff
        obj=Object(stack_start=struct.unpack_from('<I',e.items[0],12)[0])
        obj.members[0x1e]=126
        globals_=[0]*512;globals_[0]=11<<8
        vm=ObjectVM(Context([obj],0),code,constants=constants,pc=pc,globals_=globals_)
        vm.begin_update(17)
        for _ in range(31):vm.step()
        self.assertGreater(vm.pc,1995)
        self.assertEqual(obj.members[0x27],17)
        with self.assertRaises(VMError):vm.run(200)
        self.assertNotEqual(vm.error.pc,1995)


if __name__=='__main__':unittest.main()
