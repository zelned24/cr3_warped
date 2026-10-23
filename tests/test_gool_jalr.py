"""Indirect calls and actual RETURN composition. Python only during pause."""
from pathlib import Path
import sys
import unittest
import struct
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from gool_vm import VM,VMError
from gool_object import Object,Context,ObjectVM
import cb3


def call(target=0xe25,count=0xe26):return 0x47000000|count<<12|target


class GoolJalr(unittest.TestCase):
    def test_call_reads_arguments_and_return_restores_pc_owner_and_flags(self):
        code=[call(),0x2f000000,0x2f000000,0x2f000000,
              0x11b7ee36,0x11b7fe26,0x31000000]
        vm=VM(code,members={0x25:0x50000010,0x26:512});vm.mode=0x1234000b
        vm.stack.push(77);vm.stack.push(-22)
        self.assertEqual(vm.step(),'running');self.assertEqual(vm.pc,4)
        self.assertEqual(vm.mode,0x12340002)
        for _ in range(3):self.assertEqual(vm.step(),'running')
        self.assertEqual((vm.members[0x36],vm.members[0x26]),(77,-22))
        self.assertEqual((vm.pc,vm.owner,vm.mode),(1,0,0x1234000b))
        self.assertEqual(vm.stack.height,0);self.assertEqual(len(vm.stack.frames),1)

    def test_index_and_target_stack_operands_are_consumed_in_native_order(self):
        vm=VM([call(0xe1f,0xe1f),0x2f000000,0x31000000]);vm.mode=3
        vm.stack.push(99);vm.stack.push(vm.pointer(0,2));vm.stack.push(256)
        self.assertEqual(vm.step(),'running');self.assertEqual(vm.stack.read(-1),99)
        self.assertEqual(vm.step(),'running');self.assertEqual((vm.pc,vm.mode),(1,3))
        self.assertEqual(vm.stack.height,0)

    def test_alternate_code_and_indirect_member_target(self):
        objects=[Object(),Object()];ctx=Context(objects,0)
        vm=ObjectVM(ctx,[call(0xc65,0x800),0x2f000000],alternate_code=[0x31000000])
        objects[0].members[1]=ctx.pointer(1);objects[1].members[0x25]=vm.pointer(1,0)
        self.assertEqual(vm.step(),'running');self.assertEqual(vm.owner,1)
        self.assertEqual(vm.step(),'running');self.assertEqual((vm.owner,vm.pc),(0,1))

    def test_null_word_uses_raw_count_and_preserves_reserved_words(self):
        for count in (0,1,2,-1,-2,0x40000000,-0x40000000,-2147483648):
            vm=VM([call()],members={0x25:0,0x26:count});vm.mode=0xb
            for n in range(3):vm.stack.push(10+n)
            before=vm.stack.top;vm.stack.words[before]=765;vm.stack.words[before+1]=766
            expected=((before*4-((count&0xffffffff)<<2))&0xffffffff)//4
            self.assertEqual(vm.step(),'running');self.assertEqual(vm.stack.top,expected)
            self.assertEqual(vm.mode,0xb);self.assertEqual(vm.pc,1)
            self.assertEqual(vm.stack.words[before:before+2],[765,766])

    def test_fast_boolean_false_aliases_are_null_but_true_address_is_rejected(self):
        for raw in range(0xb80,0xc00):
            vm=VM([call(raw,0xe1f)]);vm.stack.push(0)
            if raw&16:
                with self.assertRaisesRegex(VMError,'boolean address 1'):vm.step()
                self.assertEqual(vm.pc,0)
            else:self.assertEqual(vm.step(),'running');self.assertEqual(vm.pc,1)
            self.assertEqual(vm.stack.height,0)

    def test_absent_link_is_null_and_raw_remote_sp_is_explicitly_unbound(self):
        vm=VM([call(0xc65,0x800)],members={1:0})
        self.assertEqual(vm.step(),'running');self.assertEqual(vm.pc,1)
        vm=VM([call(0xc5f,0x800)],members={1:0x60000004})
        with self.assertRaisesRegex(VMError,'raw indirect SP'):vm.step()
        self.assertEqual(vm.pc,0)

    def test_bad_target_counts_and_stack_limits_do_not_jump_to_unbound_code(self):
        for pointer,count in ((0,256),(0,-256),(0x60000000,0),(0x500000fc,0),
                              (0x50000004,-1),(0x50000004,256)):
            vm=VM([call(),0x31000000],members={0x25:pointer,0x26:count})
            with self.assertRaises(VMError):vm.step()
            self.assertEqual(vm.pc,0);self.assertEqual(len(vm.stack.frames),1)


    def test_original_boxsc_call_and_return_with_explicit_callback_provider(self):
        # Directed entry and explicit callback; not proof of campaign reach.
        e=cb3.load_level(11).entry('BoxsC')
        code=struct.unpack('<%dI'%(len(e.items[1])//4),e.items[1])
        self.assertEqual(code[496],0x47800e43);self.assertEqual(code[68],0x31894000)
        vm=VM(code,pc=496,stack_start=struct.unpack_from('<I',e.items[0],12)[0])
        vm.members[0x43]=vm.pointer(0,68);vm.mode=3
        self.assertEqual(vm.step(),'running');self.assertEqual(vm.pc,68)
        self.assertEqual(vm.step(),'running');self.assertEqual((vm.pc,vm.mode),(497,3))
        self.assertEqual(len(vm.stack.frames),1);self.assertEqual(vm.stack.height,0)


if __name__=='__main__':unittest.main()
