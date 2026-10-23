"""Replay native state entry; resource lookup and pre dispatcher are hooks.

Executes original reset, script-owner resolution, argument copy, root frame,
token and pre selection. Does not execute the hooked resource loader or script.
"""
from pathlib import Path
import struct
import unittest
import test_gool_delivery as fixtures
from gool_delivery import Delivery
from test_gool_stack_native import replay,MASK
from psx_native import Image,ORIGINAL_SHA256
import cb3


class NativeStateEntry(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image=Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes())
        if cls.image.sha256!=ORIGINAL_SHA256:raise AssertionError('wrong original executable')

    def test_64_state_entries_match_native_reset_args_and_current_pre_selection(self):
        for argc in (0,1,3,7):
            for current in (False,True):
                for pre in (0x3fff,4):
                    for alt_main in (False,True):
                        for interact in (0,0x402):
                            with self.subTest(argc=argc,current=current,pre=pre,alt_main=alt_main,interact=interact):
                                self.compare(argc,current,pre,alt_main,interact)

    def compare(self,argc,current,pre,alt_main,interact):
        s,t,_=fixtures.GoolDelivery().fixture(default=1,argc=argc)
        motion=0x12340000;main=7|(0x4000 if alt_main else 0)
        states=bytearray(t.base.items[4])
        struct.pack_into('<II4H',states,16,motion,interact,0,0x4006,pre,main)
        t.base.items[4]=states
        t.vm.codes[0][4]=0x31000000;t.base.items[1]=fixtures.words(t.vm.codes[0])
        job=Delivery(s,t,0,7);self.assertEqual(job.step(1),'route')
        self.assertEqual(job.apply(1 if current else 0,1),'running')
        obj=0x80070000;base=0x80090000;alt=0x80091000;header=0x80092000
        state=0x80093000;code=0x80094000;altcode=0x80095000;constants=0x80096000
        args=0x80097000;sp=0x801ff000;globals_=0x80068e98
        memory={obj+0xc:base,obj+0x10:base,obj+0xd0:0,obj+0xa8:0x20020,
                base+0x10:header,base+0x14:code,base+0x18:constants,base+0x20:state,
                alt+0x14:altcode,header+0xc:64,globals_+0x50:obj if current else 0,
                globals_+0x54:7}
        for n in range(4):memory[state+16+n*4]=struct.unpack_from('<I',states,16+n*4)[0]
        for n,value in enumerate(job.arguments):memory[args+4*n]=value&MASK
        for address in range(sp-0x100,sp+0x20,4):memory.setdefault(address,0)
        called=[]
        def resource(regs,ram):
            self.assertEqual((regs[4],regs[5],regs[6]),(constants,1,0))
            regs[2]=alt
        def dispatch(regs,ram):
            self.assertEqual((regs[4],regs[5]),(obj,3))
            self.assertEqual(ram[obj+0xc0],code+pre*4)
            called.append('pre')
            replay(self.image,0x8001d5e8,{4:obj,5:0},ram)
            regs[2]=0xffffff01
        regs=replay(self.image,0x8001d2d4,{4:obj,5:1,6:argc,7:args,29:sp},memory,
                    hooks={0x800142b4:resource,0x80038e28:dispatch})
        self.assertEqual(regs[2],0xffffff01)
        self.assertEqual(called,['pre'] if current and pre!=0x3fff else [])
        for native,member in ((0xa8,0x1a),(0xb0,0x1c),(0x100,0x30),(0xe0,0x28)):
            self.assertEqual(memory[obj+native],t.object.members[member]&MASK)
        self.assertEqual(memory[obj+0x1c],t.state)
        self.assertEqual(memory[obj+0xcc],altcode+6*4)
        self.assertEqual(memory[obj+0xc8],0 if pre==0x3fff else code+pre*4)
        self.assertEqual(memory[obj+0xc0],(altcode if alt_main else code)+7*4)
        origin=obj+0x40+64*4
        self.assertEqual(memory[obj+0xbc],origin+t.vm.stack.top*4)
        self.assertEqual(memory[obj+0xc4],origin+t.vm.stack.frames[0]*4)
        for n,value in enumerate(job.arguments):self.assertEqual(memory[origin+n*4],value&MASK)
        frame=origin+argc*4
        self.assertEqual(memory[frame],t.vm.stack.words[argc]&MASK)
        self.assertEqual(memory[frame+8],t.vm.stack.words[argc+2]&MASK)
        self.assertEqual(memory[frame+12],0);self.assertTrue(t.vm.frame_yield)

    def test_255_is_sentinel_even_if_table_has_that_slot(self):
        _,t,_=fixtures.GoolDelivery().fixture()
        t.base.items[4]=t.base.items[4][:16]*256
        before=(t.state,t.vm.stack.top,t.state_generation)
        with self.assertRaises(ValueError):t.enter_state(255,())
        self.assertEqual(before,(t.state,t.vm.stack.top,t.state_generation))
        regs=replay(self.image,0x8001d2d4,{4:0x80070000,5:255,6:0,7:0,29:0x801ff000},
                    {0x800700d0:0})
        self.assertEqual(regs[2],(-14)&MASK)


if __name__=='__main__':unittest.main()
