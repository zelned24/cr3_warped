"""Native indirect STACK read replay; no execution hooks or C compilation."""
from pathlib import Path
import unittest
from test_gool_stack_native import replay
from psx_native import Image,ORIGINAL_SHA256
from gool_object import Object,Context,ObjectVM
from gool_memory import Memory
import cb3

class NativeObjectStack(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image=Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes())
        if cls.image.sha256!=ORIGINAL_SHA256:raise AssertionError('wrong original')

    def test_384_null_link_reads_match_native_without_member_storage(self):
        obj=Object();vm=ObjectVM(Context([obj],0),[0x2f000000])
        for base in (1,2,3,4,6,7):
            obj.members[base]=0
            for member in range(64):
                raw=0xc00|(base<<6)|member
                address=0x80070000;top=0x80071000
                ram={address+0x40+base*4:0}
                regs=replay(self.image,0x80038c94,{4:raw,16:address,22:top},ram)
                self.assertEqual(regs[2],vm.read(raw))
                self.assertEqual(regs[22],top)
                self.assertEqual(ram,{address+0x40+base*4:0})

    def test_128_indirect_reads_return_address_without_pop(self):
        for base in (0,1,2,3,4,5,6,7):
            for count in range(16):
                a,b=Object(),Object();objects=[a,b]
                ctx=Context(objects,0,player=1)
                vm=ObjectVM(ctx,[0x2f000000])
                other=ObjectVM(Context(objects,1),[0x2f000000])
                target=a if base==0 else b
                if base not in (0,5):a.members[base]=ctx.pointer(1)
                for n in range(count):target.vm.stack.push(100+n)
                memory=Memory();vm.memory=memory;other.memory=memory; mapped=target.bind_stack(memory)
                top=target.vm.stack.top;raw=0xc00|(base<<6)|31
                obj=0x80070000;remote=obj if base==0 else 0x80071000
                native_sp=remote+0x840+4*top
                ram={obj+0x40+base*4:remote,remote+0xbc:native_sp}
                local_sp=obj+0x840+4*vm.stack.top
                regs=replay(self.image,0x80038c94,{4:raw,16:obj,22:local_sp},ram)
                self.assertEqual(regs[2],native_sp)
                self.assertEqual(regs[22],local_sp)
                self.assertEqual(vm.read(raw)-mapped,native_sp-(remote+0x840))
                self.assertEqual(target.vm.stack.top,top)
                self.assertEqual(ram[remote+0xbc],native_sp)
                target.unbind_stack()

if __name__=='__main__':unittest.main()
