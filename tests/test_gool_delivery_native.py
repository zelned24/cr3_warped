"""Replay native delivery selection; dispatcher/state execution are hooks.

This checks service decisions/flags/arguments, not the two hooked bodies.
ON_SIGNAL execution is separately exercised by test_gool_delivery and the
receive replay. State/code application remains an explicit pending boundary.
"""
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from psx_native import Image,ORIGINAL_SHA256
from test_gool_stack_native import replay
import test_gool_delivery as fixtures
from test_gool_receive import receive
from gool_delivery import Delivery
import cb3


class NativeDelivery(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image=Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes())
        if cls.image.sha256!=ORIGINAL_SHA256:raise AssertionError('wrong original executable')

    def compare(self,kind,default,raw,flags,motion,player):
        handler=None;selected=-1;was_b=0
        if kind=='return':handler=[0x31000000]
        elif kind in ('a','b'):
            selected=1;was_b=int(kind=='b');handler=[receive(0x3e if was_b else 0x3d,offset=1)]
        elif kind in ('a_none','b_none'):
            selected=255;was_b=int(kind=='b_none');handler=[receive(0x3e if was_b else 0x3d,action=2)]
        helper=fixtures.GoolDelivery()
        s,t,_=helper.fixture(handler,default=default,raw=raw,argc=2,motion=motion)
        t.object.members[0x1c]=flags
        if player is not None:t.vm.context.player=1;t.object.members[0x32]=player
        job=Delivery(s,t,0xb0000000,7);result=job.step(8)
        sender=0x80070000;target=0x80080000;behavior=0x80090000
        header=0x80091000;indices=0x80092000;states=0x80093000
        code=0x80094000;args=0x80095000
        mem={target:1,target+0xc:behavior,behavior+0x10:header,header+0x10:64,
             behavior+0x1c:indices,behavior+0x20:states,behavior+0x14:code,
             target+0xcc:code+8 if handler else 0,target+0xb0:flags,
             target+0xa8:0x20020,target+0x108:0 if player is None else player,
             target+0x11c:0x12345678,0x80060a90:0 if player is None else target,
             target+0xbc:target+0x140,target+0xc4:target+0x40,target+0xc0:code,
             states+16:motion,sender+0xd4:raw,args:70,args+4:71}
        for n in range(32):mem[indices+n*4]=0x00ff00ff
        address=indices+(raw>>8)*2
        old=mem[address&~3];shift=(address&3)*8
        mem[address&~3]=(old&~(65535<<shift))|((default&65535)<<shift)
        replay(self.image,0x8001d588,{4:target,5:0,6:0xffff},mem)
        mem[mem[target+0xbc]]=0;mem[target+0xbc]+=4
        calls=[]
        def dispatch(regs,ram):
            if regs[5]==8:
                replay(self.image,0x8001d5e8,{4:target,5:0},ram)
                if selected>=0:ram[regs[6]]=selected;ram[regs[6]+4]=was_b
                regs[2]=0xffffffe6 if selected==-1 else 0xffffff01
            else:
                calls.append(('script',(ram[target+0xc0]-code)//4))
                replay(self.image,0x8001d5e8,{4:target,5:0},ram)
                regs[2]=0xffffff01
        def state_call(regs,ram):
            calls.append(('state',regs[5]))
            self.assertEqual(regs[6],2)
            self.assertEqual((ram[regs[7]],ram[regs[7]+4]),(70,71))
            regs[2]=0xffffff01
        sp=0x801ff000
        for address in range(sp-0x100,sp+0x20,4):mem.setdefault(address,0)
        mem[sp+0x10]=args
        replay(self.image,0x8001db38,{4:sender,5:target,6:raw,7:2,29:sp},mem,
               hooks={0x80038e28:dispatch,0x8001d2d4:state_call})
        self.assertEqual(mem[sender+0xd4],s.object.members[0x25])
        self.assertEqual(mem[target+0x5c],sender)
        self.assertEqual(t.object.members[7],0x60000000)
        self.assertEqual(mem[target+0xa8],t.object.members[0x1a])
        if calls:
            self.assertEqual(result,'route')
            self.assertEqual(calls,[(job.route.kind,job.route.value)])
            self.assertEqual(mem[target+0x11c],t.object.members[0x37])
        else:
            self.assertEqual(result,'running')
            self.assertEqual(mem[target+0x11c],0x12345678)
            self.assertNotIn(0x37,t.object.members)

    def test_192_original_service_selection_cases_with_declared_execution_hooks(self):
        for raw in (0,0x1800,0x1801):
            for kind,default in ((None,255),(None,1),('return',1),('a',255),
                                 ('b',255),('a_none',1),('b_none',1),(None,-32764)):
                for flags,motion in ((0,0),(2,2)):
                    for player in (None,3):self.compare(kind,default,raw,flags,motion,player)


if __name__=='__main__':unittest.main()
