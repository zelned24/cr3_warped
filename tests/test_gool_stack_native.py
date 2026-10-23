"""Replay selected original routines over fixture RAM, not a game emulator.

The tiny test interpreter decodes actual MIPS words, includes branch delay
slots and rejects other operations. It is an independent frame-layout oracle,
not an implementation of PS1 scheduling, GTE, memory timing or the whole VM.
"""
from pathlib import Path
import random
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import cb3
from psx_native import Image,ORIGINAL_SHA256
from gool_stack import Stack

MASK=0xffffffff


def replay(image,start,registers,memory,stops=(),hooks=None,max_steps=512):
    regs=[0]*32
    for reg,value in registers.items():regs[reg]=value&MASK
    sentinel=0x1000;regs[31]=sentinel
    pc=start;pending=None;hi=lo=0
    for _ in range(max_steps):
        if pc==sentinel or pc in stops:return regs
        if hooks and pc in hooks:
            hooks[pc](regs,memory)
            pc=regs[31];pending=None;continue
        inst=image.word(pc);op=inst>>26;rs=(inst>>21)&31;rt=(inst>>16)&31
        rd=(inst>>11)&31;shift=(inst>>6)&31;fn=inst&63
        imm=inst&65535;simm=imm-65536 if imm&32768 else imm
        jump=None;value=None;dest=rt
        if op==0:
            dest=rd
            if fn==0:value=regs[rt]<<shift
            elif fn==2:value=regs[rt]>>shift
            elif fn==3:value=(regs[rt] if regs[rt]<0x80000000 else regs[rt]-0x100000000)>>shift
            elif fn==4:value=regs[rt]<<(regs[rs]&31)
            elif fn==0x21:value=regs[rs]+regs[rt]
            elif fn==0x22:
                a=regs[rs] if regs[rs]<0x80000000 else regs[rs]-0x100000000
                b=regs[rt] if regs[rt]<0x80000000 else regs[rt]-0x100000000
                value=a-b
                if not -(1<<31)<=value<(1<<31):raise AssertionError('fixture SUB overflow')
            elif fn==0x23:value=regs[rs]-regs[rt]
            elif fn==0x24:value=regs[rs]&regs[rt]
            elif fn==0x25:value=regs[rs]|regs[rt]
            elif fn==0x26:value=regs[rs]^regs[rt]
            elif fn==0x27:value=~(regs[rs]|regs[rt])
            elif fn==0x2a:
                a=regs[rs] if regs[rs]<0x80000000 else regs[rs]-0x100000000
                b=regs[rt] if regs[rt]<0x80000000 else regs[rt]-0x100000000
                value=int(a<b)
            elif fn==0x2b:value=int(regs[rs]<regs[rt])
            elif fn in (0x18,0x19):
                a,b=regs[rs],regs[rt]
                if fn==0x18:
                    a=a if a<0x80000000 else a-0x100000000
                    b=b if b<0x80000000 else b-0x100000000
                product=(a*b)&0xffffffffffffffff
                hi,lo=product>>32,product&MASK
            elif fn in (0x1a,0x1b):
                a,b=regs[rs],regs[rt]
                if fn==0x1a:
                    a=a if a<0x80000000 else a-0x100000000
                    b=b if b<0x80000000 else b-0x100000000
                if not b:hi,lo=a&MASK,(-1 if fn==0x1b or a>=0 else 1)&MASK
                else:
                    quotient=abs(a)//abs(b)
                    if (a<0)!=(b<0):quotient=-quotient
                    hi,lo=(a-quotient*b)&MASK,quotient&MASK
            elif fn==0x10:value=hi
            elif fn==0x12:value=lo
            elif fn==8:jump=regs[rs]
            elif fn==9:jump=regs[rs];value=(pc+8)&MASK
            elif fn==0x0d:raise ArithmeticError(f'fixture BREAK {(inst>>6)&0xfffff} at {pc:x}')
            else:raise AssertionError(f'unsupported leaf SPECIAL {fn:x} at {pc:x}')
        elif op==8:
            source=regs[rs] if regs[rs]<0x80000000 else regs[rs]-0x100000000
            value=source+simm
            if not -(1<<31)<=value<(1<<31):raise AssertionError('fixture ADDI overflow')
        elif op==9:value=regs[rs]+simm
        elif op==12:value=regs[rs]&imm
        elif op==13:value=regs[rs]|imm
        elif op==14:value=regs[rs]^imm
        elif op==15:value=imm<<16
        elif op==10:value=int((regs[rs] if regs[rs]<0x80000000 else regs[rs]-0x100000000)<simm)
        elif op==11:value=int(regs[rs]<(simm&MASK))
        elif op in (32,33,35,36,37,40,41,43):
            address=(regs[rs]+simm)&MASK
            if op in (32,36):
                value=(memory[address&~3]>>((address&3)*8))&255
                if op==32 and value&128:value-=256
            elif op in (40,41):
                if op==41 and address&1:raise AssertionError('unaligned fixture halfword access')
                bits=8 if op==40 else 16;shift=(address&3)*8;mask=((1<<bits)-1)<<shift
                memory[address&~3]=(memory.get(address&~3,0)&~mask)|((regs[rt]<<shift)&mask)
            elif op in (33,37):
                if address&1:raise AssertionError('unaligned fixture halfword access')
                value=(memory[address&~3]>>((address&3)*8))&65535
                if op==33 and value&32768:value-=65536
            elif address&3:raise AssertionError('unaligned fixture RAM access')
            elif op==35:value=memory[address]
            else:memory[address]=regs[rt]
        elif op in (4,5):
            taken=(regs[rs]==regs[rt])==(op==4)
            if taken:jump=(pc+4+simm*4)&MASK
        elif op==6:
            signed=regs[rs] if regs[rs]<0x80000000 else regs[rs]-0x100000000
            if signed<=0:jump=(pc+4+simm*4)&MASK
        elif op==1 and rt in (0,1):
            negative=bool(regs[rs]&0x80000000)
            if negative==(rt==0):jump=(pc+4+simm*4)&MASK
        elif op in (2,3):
            if op==3:regs[31]=(pc+8)&MASK
            jump=((pc+4)&0xf0000000)|((inst&0x3ffffff)<<2)
        else:raise AssertionError(f'unsupported leaf opcode {op:x} at {pc:x}')
        if value is not None and dest:regs[dest]=value&MASK
        if pending is not None and jump is not None:raise AssertionError('branch in delay slot')
        pc=pc+4 if pending is None else pending
        pending=jump
    raise AssertionError('leaf routine did not return within budget')


class NativeStack(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image=Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes())
        if cls.image.sha256!=ORIGINAL_SHA256:raise AssertionError('wrong original executable')

    def test_thousand_nested_frames_match_original_enter_and_return(self):
        rng=random.Random(94244);obj=0x80070000
        for case in range(1000):
            origin=rng.randrange(1,16129)*4
            s=Stack(origin=origin);memory={obj+0xc4:obj+0x40}
            argc=rng.randrange(8)
            for n in range(argc):s.push(n)
            root_pc=rng.randrange(0x4000)*4+0x50000000
            memory[obj+0xbc]=obj+0x40+origin+s.top*4
            memory[obj+0xc0]=root_pc
            replay(self.image,0x8001d588,{4:obj,5:argc,6:0xffff},memory)
            s.enter(argc,root_pc)
            for n in range(3):self.assertEqual(memory[obj+0x40+origin+(argc+n)*4],s.words[argc+n]&MASK)
            self.assertEqual(memory[obj+0xbc],obj+0x40+origin+s.top*4)
            for n in range(rng.randrange(5)):s.push(100+n)
            count=rng.randrange(8)
            for n in range(count):s.push(200+n)
            frame=s.top;return_pc=0x50000000+rng.randrange(0x4000)*4;flags=rng.randrange(1<<32)
            memory[obj+0xbc]=obj+0x40+origin+s.top*4;memory[obj+0xc0]=return_pc
            replay(self.image,0x8001d588,{4:obj,5:count,6:flags},memory)
            s.enter(count,return_pc,flags)
            for n in range(3):self.assertEqual(memory[obj+0x40+origin+(frame+n)*4],s.words[frame+n]&MASK)
            self.assertEqual(memory[obj+0xc4],obj+0x40+origin+frame*4)
            replay(self.image,0x8001d5e8,{4:obj,5:0},memory)
            self.assertEqual(s.leave(),return_pc)
            self.assertEqual(s.last_flags,flags&65535)
            self.assertEqual(memory[obj+0xc0],return_pc)
            self.assertEqual(memory[obj+0xbc],obj+0x40+origin+s.top*4)
            self.assertEqual(memory[obj+0xc4],obj+0x40+origin+s.frames[-1]*4)


if __name__=='__main__':unittest.main()
