"""Word-span lifetime/bounds plus VM array instructions. Pure Python only."""
from pathlib import Path
import sys
import unittest
import struct
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from gool_memory import Memory
from gool_vm import VM,VMError
import cb3


def array(op,index,base):return op<<24|index<<12|base


class GoolMemory(unittest.TestCase):
    def test_borrow_aliases_original_storage_and_release_never_reuses_handle(self):
        m=Memory();words=[11,22,33];p=m.borrow(words)
        self.assertEqual(m.read(p+4),22);m.write(p+8,-7);self.assertEqual(words,[11,22,-7])
        words[0]=-123;self.assertEqual(m.read(p),-123)
        m.release(p);q=m.borrow([99]);self.assertNotEqual(p,q)
        with self.assertRaises(ValueError):m.read(p)
        with self.assertRaises(ValueError):m.release(p)

    def test_alignment_exact_extent_null_and_tags(self):
        m=Memory();p=m.borrow([1,2])
        for address in (0,0x60000000,p-4,p+1,p+2,p+8,p+12,0x80000000):
            with self.subTest(address=address):
                with self.assertRaises(ValueError):m.read(address)
                with self.assertRaises(ValueError):m.write(address,7)
        with self.assertRaises(ValueError):m.release(p+4)
        self.assertEqual(m.read(p+4),2)

    def test_readonly_empty_capacity_exhaustion_and_resized_storage(self):
        m=Memory();p=m.borrow([1],False)
        with self.assertRaises(ValueError):m.write(p,8)
        self.assertEqual(m.read(p),1)
        q=m.borrow([]);self.assertNotEqual(q,0)
        with self.assertRaises(ValueError):m.read(q)
        for _ in range(30):m.borrow([])
        with self.assertRaises(ValueError):m.borrow([1])
        m.release(q);m.borrow([])
        m=Memory();m.next_address=0x7ffffffc
        with self.assertRaises(ValueError):m.borrow([1])
        q=m.borrow([]);self.assertEqual(q,0x7ffffffc)
        with self.assertRaises(ValueError):m.borrow([])
        m=Memory();values=[1];q=m.borrow(values);values.append(2)
        with self.assertRaises(ValueError):m.read(q)

    def test_array_vm_reads_then_writes_same_borrowed_span(self):
        m=Memory();values=[101,-77,303];p=m.borrow(values)
        vm=VM([array(0x2a,0x801,0xb7f),array(0x4e,0x802,0xb7f)],args=(p,),memory=m)
        self.assertEqual(vm.step(),'running');self.assertEqual(vm.stack.words[vm.stack.top-1],-77)
        self.assertEqual(vm.step(),'running');self.assertEqual(values,[101,-77,-77])
        self.assertEqual(vm.stack.height,0)

    def test_index_then_base_then_value_stack_order(self):
        for op in (0x2a,0x4e):
            m=Memory();words=[5,6];p=m.borrow(words)
            vm=VM([array(op,0xe1f,0xe1f)],memory=m)
            if op==0x4e:vm.stack.push(-123)
            vm.stack.push(p);vm.stack.push(256)
            self.assertEqual(vm.step(),'running')
            if op==0x2a:self.assertEqual(vm.stack.pop(),6)
            else:self.assertEqual(words[1],-123)
            self.assertEqual(vm.stack.height,0)

    def test_missing_domain_stale_pointer_and_readonly_faults_are_explicit(self):
        vm=VM([array(0x2a,0xe1f,0xe1f)])
        vm.stack.push(123);vm.stack.push(0);before=vm.stack.top
        with self.assertRaises(VMError):vm.step()
        self.assertEqual(vm.stack.top,before)
        for kind in ('stale','readonly','unaligned','bounds'):
            m=Memory();words=[77];p=m.borrow(words,kind!='readonly')
            if kind=='stale':m.release(p)
            base=p+1 if kind=='unaligned' else p
            vm=VM([array(0x4e,0x801 if kind=='bounds' else 0x800,0xb7f)],args=(base,),memory=m)
            vm.stack.push(99)
            with self.assertRaises(VMError):vm.step()
            self.assertEqual(words,[77]);self.assertEqual(vm.stack.height,0)
            self.assertEqual(vm.pc,0)

    def test_original_partc_prefix_reads_three_incoming_coordinates(self):
        # Directed entry at a real code slice, not proof that a game route has
        # reached it. No instruction in this six-word prefix is substituted.
        e=cb3.load_level(11).entry('PartC')
        code=struct.unpack('<%dI'%(len(e.items[1])//4),e.items[1])
        self.assertEqual(code[3137:3143],(0x2a800b7f,0x11e1fe08,0x2a801b7f,
                                       0x11e1fe09,0x2a802b7f,0x11e1fe0a))
        m=Memory();position=[12594944,-818944,103679744];pointer=m.borrow(position)
        vm=VM(code,pc=3137,args=(pointer,),memory=m,
              stack_start=struct.unpack_from('<I',e.items[0],12)[0])
        for _ in range(6):self.assertEqual(vm.step(),'running')
        self.assertEqual([vm.members[n] for n in (8,9,10)],position)
        self.assertEqual(vm.pc,3143);self.assertEqual(vm.stack.height,0)


if __name__=='__main__':unittest.main()
