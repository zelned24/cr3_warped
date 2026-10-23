"""Executable Python model of the initial native GOOL dispatcher contract.

Deliberately partial. Unsupported instructions stop before operand evaluation.
Members must be supplied explicitly; unknown members are never fabricated zero.
Standalone stack_start=1 is a virtual test coordinate; entity instances pass
their actual GOOL header value. Native receive sourceOp 3 and engine scheduling
remain unsupported; signal result selection does not implement SEND transport.
"""
from gool_operand import decode,simple
from gool_stack import Stack,StackError
from dataclasses import dataclass

BINARY=frozenset(range(17))|{0x15,0x1f,0x20,0x21}
STORES=frozenset((0x11,0x12,0x17,0x19,0x27,0x2b,0x2c))


@dataclass
class Receiver:
    was_b: bool=False
    new_state: int=-1
    arguments: tuple=()


def word(value):
    value&=0xffffffff
    return value if value<0x80000000 else value-0x100000000


class VMError(ValueError):
    def __init__(self,kind,pc,instruction,detail=None):
        super().__init__(f'{kind} at {pc:#x}: {instruction:08X}; {detail}')
        self.kind,self.pc,self.instruction,self.detail=kind,pc,instruction,detail


class VM:
    def __init__(self,code,constants=(),alternate=(),members=None,pc=0,args=(),alternate_code=None,globals_=None,stack_start=1,memory=None,random_state=None,frame_factor=None):
        if not 0<=pc<len(code):raise ValueError('initial PC out of bounds')
        self.code=list(code);self.constants=list(constants);self.alternate=list(alternate)
        self.members={} if members is None else members
        self.pc=pc;self.error=None;self.done=False
        self.codes={0:self.code}
        if alternate_code is not None:self.codes[1]=list(alternate_code)
        self.owner=0
        self.mode=0
        self.tick=0;self.next_update=0;self.respect_delay=True
        self.frame_yield=False;self.frame_token_index=None
        self.change_state=None
        self.receiver=None
        self.send_enabled=False;self.send_pending=False;self.send=None
        self.globals=globals_
        self.memory=memory
        if random_state is not None and (not isinstance(random_state,list) or len(random_state)!=2 or
            any(not isinstance(n,int) or not 0<=n<=0xffffffff for n in random_state)):
            raise ValueError('invalid shared random state')
        self.random_state=random_state
        self.frame_factor=frame_factor
        self.stack=Stack(origin=stack_start*4)
        for value in args:self.stack.push(value)
        self.stack.enter(len(args),0)

    def read(self,raw):
        arg=decode(raw)
        if arg.kind in ('literal','boolean'):return arg.value
        if arg.kind=='constant':
            table=self.alternate if arg.base else self.constants
            if arg.value>=len(table):raise ValueError('constant bounds')
            return word(table[arg.value])
        if arg.kind=='frame':return self.stack.read(arg.value)
        if arg.kind=='variable':
            if arg.value==31:return self.stack.pop()
            if arg.value==32:return self.pointer(self.owner,self.pc+1)
            if arg.value not in self.members:raise ValueError('unbound member')
            return self.members[arg.value]
        raise ValueError('unbound object link')

    def write(self,raw,value):
        arg=decode(raw)
        if arg.kind=='frame':self.stack.write(arg.value,value);return
        if arg.kind=='variable':
            if arg.value==32:raise ValueError('generic script pointer store')
            if arg.value==31:self.stack.push(value)
            else:self.members[arg.value]=value
            return
        raise ValueError('read-only or unbound destination')

    def pointer(self,owner,pc):
        if not 0<=owner<1024 or not 0<=pc<0x4000:raise ValueError('script pointer bounds')
        return 0x50000000|(owner<<18)|(pc<<2)

    def target(self,pointer):
        raw=pointer&0xffffffff
        if raw>>28!=5 or raw&3:raise ValueError('invalid script pointer')
        owner=(raw&0x0fffffff)>>18;pc=(raw>>2)&0xffff
        if owner not in self.codes or pc>=0x4000 or pc>=len(self.codes[owner]):raise ValueError('return target bounds')
        return owner,pc

    def return_from_call(self,restore_flags=True):
        if len(self.stack.frames)==1:self.done=True;return 'done'
        if not self.stack.frames:raise StackError('no return frame')
        owner,pc=self.target(self.stack.words[self.stack.frames[-1]+1])
        self.stack.leave()
        if restore_flags and self.stack.last_flags!=0xffff:
            self.mode=(self.mode&0xffff0000)|self.stack.last_flags
        self.owner=owner;self.code=self.codes[owner];self.pc=pc
        return 'running'

    def lighting_read(self,base,selector):raise ValueError('unbound halfword provider')
    def lighting_write(self,base,selector,value):raise ValueError('unbound halfword provider')

    def step(self):
        if self.error:raise self.error
        if self.done:return 'done'
        if self.send_pending:return 'send'
        if self.respect_delay and self.frame_yield:return 'wait'
        inst=self.code[self.pc] if self.pc<len(self.code) else 0
        op=inst>>24
        if self.pc>=len(self.code):kind='pc_bounds'
        elif op not in BINARY|STORES|{0x18,0x22,0x23,0x24,0x25,0x2a,0x2d,0x4e,0x2f,0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x47,0x3b,0x3c,0x3d,0x3e}:kind='unsupported'
        elif op==0x3c and not self.send_enabled:kind='unsupported'
        elif op==0x10 and self.random_state is None:kind='unsupported'
        elif op in (0x35,0x36,0x37) and self.change_state is None:kind='unsupported'
        else:kind=None
        if kind:
            self.error=VMError(kind,self.pc,inst);raise self.error
        try:
            if op in (0x23,0x24):
                value=self.read(inst&0xfff) if op==0x24 else None
                base=(inst>>12)&7;selector=(inst>>15)&63
                if op==0x23:self.stack.push(self.lighting_read(base,selector))
                else:self.lighting_write(base,selector,value)
            elif op==0x3c:
                from gool_send import begin
                return begin(self,inst)
            elif op in (0x3d,0x3e):
                if not self.mode&8:raise ValueError('RECEIVE outside native signal mode')
                if self.receiver is None:raise ValueError('unbound engine receive result')
                receiver=self.receiver
                src_op=(inst>>20)&3;action=(inst>>22)&3;argc=(inst>>10)&15
                if src_op==3:raise ValueError('unsupported native sourceOp 3')
                matches=True
                if src_op:
                    value=self.read(0xe00|((inst>>14)&63))
                    matches=value==0 if src_op==2 else value!=0
                if not matches:
                    if action==0:
                        offset=inst&1023
                        if offset&512:offset-=1024
                        pc=self.pc+1+offset
                        if not 0<=pc<len(self.code):raise ValueError('receive branch bounds')
                        if argc>self.stack.height:raise StackError('receive discard underflow')
                        self.stack.top-=argc
                        self.pc=pc;return 'running'
                else:
                    if action==1 and argc>self.stack.height:raise StackError('receive arguments underflow')
                    self.mode|=0x20
                    receiver.was_b=op==0x3e
                    if action==1:
                        receiver.arguments=tuple(self.stack.words[self.stack.top-argc:self.stack.top])
                        self.stack.top-=argc
                    if action in (1,2):
                        result=self.return_from_call(False)
                        if result=='done':return result
                        receiver.new_state=inst&1023 if action==1 else 255
                        return 'signal_return'
            elif op in (0x35,0x36,0x37):
                meets=True if op==0x35 else (self.read(0xe00|((inst>>14)&0x3f))!=0)==(op==0x36)
                result=self.change_state(inst&0x3ff,(inst>>10)&15,meets)
                if result=='new_state':return result
                if result!='running':raise ValueError('invalid state service result')
            elif op==0x2d:
                right=self.read((inst>>12)&0xfff)
                arg=decode(inst&0xfff)
                if arg.kind=='boolean':raise ValueError('unbound atan address')
                if arg.kind=='indirect':
                    if arg.value==31:raise ValueError('unsupported atan indirect stack address')
                    if not self.read(0xe00|arg.base):raise ValueError('null atan address')
                left=self.read(inst&0xfff)
                from gool_atan import atan2
                self.stack.push(atan2(left,right))
            elif op in (0x22,0x25):
                high=(inst>>12)&0xfff
                rate=256
                if high==0xbf0:rate=self.stack.pop();right=self.stack.pop()
                else:right=self.read(high)
                left=self.read(inst&0xfff)
                if op==0x22:
                    rate=word(-rate) if rate<0 else rate
                    delta=word(right-left)
                    if delta<0:result=right if word(delta+rate)>=0 else word(left-rate)
                    else:result=right if word(delta-rate)<=0 else word(left+rate)
                else:
                    factor=self.frame_factor
                    if factor is None or len(factor)!=1:raise ValueError('unbound frame factor')
                    step=word(factor[0]*rate)>>10
                    delta=(right-left)&0xfff
                    if delta&0x800:delta-=4096
                    result=right if word(abs(delta)-step)<=0 else (left+(-step if delta<0 else step))&0xfff
                self.stack.push(word(result))
            elif op in BINARY:
                right=self.read((inst>>12)&0xfff);left=self.read(inst&0xfff)
                if op in (0x1f,0x20):
                    index=(left&0xffffffff)>>8
                    if self.globals is None:raise ValueError('unbound globals')
                    if index>=len(self.globals):raise ValueError('global bounds')
                    if op==0x1f:self.stack.push(word(self.globals[index]))
                    else:
                        self.stack.reserve()
                        self.globals[index]=right
                    self.pc+=1;return 'running'
                if op in (3,13):
                    quotient=(1 if left<0 else -1) if not right else abs(left)//abs(right)
                    if right and (left<0)!=(right<0):quotient=-quotient
                if op==0:result=left+right
                elif op==1:result=left-right
                elif op==2:result=left*right
                elif op==3:result=quotient
                elif op==4:result=int(left==right)
                elif op==5:result=left if right else 0
                elif op in (6,8):result=left|right
                elif op==7:result=left&right
                elif op==9:result=int(left<right)
                elif op==10:result=int(left<=right)
                elif op==11:result=int(left>right)
                elif op==12:result=int(left>=right)
                elif op==13:result=left-quotient*right
                elif op==14:result=left^right
                elif op==15:result=int(left&right==right)
                elif op==0x10:
                    state=self.random_state
                    if len(state)!=2 or any(not isinstance(n,int) or not 0<=n<=0xffffffff for n in state):
                        raise ValueError('invalid shared random state')
                    old_a,old_b=state
                    next_a=((old_a>>8)|(old_b<<24))&0xffffffff
                    total=(old_a+(old_b>>8)+(next_a>>8))&0xffffffff
                    next_b=((old_b>>8)|(total<<24))&0xffffffff
                    span=(right-left)&0xffffffff
                    state[:]=[next_a,next_b]
                    result=(left&0xffffffff)+(next_b%span if span else next_b)
                elif op==0x15:result=left<<(right&31) if right>=0 else left>>((-right)&31)
                elif op==0x21:
                    result=(right-left)&0xfff
                    if result&0x800:result-=4096
                self.stack.push(word(result))
            elif op in (0x2a,0x4e):
                if self.memory is None:raise ValueError('unbound memory domain')
                index=self.read((inst>>12)&0xfff)&0xffffffff
                base=self.read(inst&0xfff)&0xffffffff
                address=(base+((index>>8)<<2))&0xffffffff
                if op==0x2a:self.stack.push(self.memory.read(address))
                else:self.memory.write(address,self.stack.pop())
            elif op==0x18:
                destination=(inst>>18)&63;pc=inst&0x3fff
                if destination==32:raise ValueError('unbound live SCRIPT_PTR overlay for pointer formation')
                slot=self.stack.top
                if destination==31:self.stack.reserve()
                pointer=0
                if pc!=0x3fff:
                    owner=int(bool(inst&0x4000))
                    if owner not in self.codes:raise ValueError('unbound pointer code owner')
                    pointer=self.pointer(owner,pc)
                if destination==31:self.stack.words[slot]=pointer
                else:self.write(0xe00|destination,pointer)
            elif op in STORES:
                value=self.read((inst>>12)&0xfff)
                if op==0x11:result=value
                elif op==0x12:result=int(value==0)
                elif op==0x17:result=~value
                elif op==0x19:result=abs(value)
                elif op==0x27:result=0x40000000|((value&0xffffffff)>>6)<<2
                elif op in (0x2b,0x2c):
                    from gool_trig import sine
                    result=sine(value+(1024 if op==0x2c else 0))
                self.write(simple(inst&0xfff),word(result))
            elif op in (0x38,0x39):
                duration=(inst>>16)&63;mirror=(inst>>22)&3
                frame=(inst&127)*256 if op==0x38 else self.read(inst&0xfff)
                if op==0x38:self.write(0xe2a,0x40000000|(((inst>>5)&0x7fc)<<2))
                self.write(0xe2b,frame)
                token_index=self.stack.top
                self.stack.push(word((self.tick&0xffffffff)|(duration<<24)))
                if mirror!=3:
                    scale=self.read(0xe0e)
                    if mirror==0:self.write(0xe0e,word(abs(scale)))
                    elif mirror==1:self.write(0xe0e,word(-abs(scale)))
                    elif mirror==2:self.write(0xe0e,word(-scale))
                self.next_update=min((1<<64)-1,self.tick+duration) # diagnostic only
                if self.mode&4:
                    self.frame_yield=True;self.frame_token_index=token_index;self.pc+=1
                    return 'wait'
            elif op in (0x32,0x33,0x34):
                taken=True if op==0x32 else (self.read(0xe00|((inst>>14)&0x3f))!=0)==(op==0x33)
                if taken:
                    offset=inst&0x3ff
                    if offset&0x200:offset-=1024
                    target=self.pc+1+offset
                    if not 0<=target<len(self.code):raise ValueError('branch bounds')
                    discard=(inst>>10)&0xf
                    if discard>self.stack.height:raise StackError('branch discard underflow')
                    for _ in range(discard):self.stack.pop()
                    self.pc=target
                    return 'running'
            elif op==0x47:
                count=self.read((inst>>12)&0xfff)
                operand=decode(inst&0xfff);address=True
                if operand.kind=='boolean':
                    if operand.value:raise ValueError('unsupported JALR boolean address 1')
                    address=False
                elif operand.kind=='indirect':
                    address=self.read(0xe00|operand.base)!=0
                    if address and operand.value==31:raise ValueError('unbound raw indirect SP for JALR')
                pointer=self.read(inst&0xfff) if address else 0
                if not pointer:
                    size=(self.stack.top*4-((count&0xffffffff)<<2))&0xffffffff
                    if not self.stack.floor*4<=size<=len(self.stack.words)*4:raise StackError('null JALR raw cleanup bounds')
                    self.stack.top=size//4
                else:
                    if count<0:raise StackError('negative JALR argument count')
                    owner,pc=self.target(pointer)
                    self.stack.enter(count>>8,self.pointer(self.owner,self.pc+1),self.mode)
                    self.mode&=~9;self.owner=owner;self.code=self.codes[owner];self.pc=pc
                    return 'running'
            elif op==0x3b:
                address=inst&0x7fff;owner=1 if address&0x4000 else 0
                pc=address&0x3fff;argc=(inst>>20)&15
                if owner not in self.codes or pc>=len(self.codes[owner]):raise ValueError('call target bounds')
                pointer=self.pointer(self.owner,self.pc+1)
                self.stack.enter(argc,pointer,self.mode)
                self.mode&=~9
                self.owner=owner;self.code=self.codes[owner];self.pc=pc
                return 'running'
            elif op==0x31:
                mode=self.mode
                if mode&8 and self.receiver is None:raise ValueError('unbound engine receive result')
                result=self.return_from_call(not mode&9)
                if mode&0x11==0x11 and not mode&8 and result!='done':
                    self.seed_frame();return 'resume_return'
                if mode&8:
                    self.receiver.new_state=255 if mode&32 else -1
                    return 'signal_return' if mode&32 else 'failed_signal'
                return result
        except (ValueError,ArithmeticError,StackError) as error:
            self.error=VMError('execution',self.pc,inst,str(error));raise self.error from error
        self.pc+=1
        return 'running'

    def invoke(self,owner,pc,argc=0):
        """Engine handler entry, returning to current PC rather than PC+1."""
        if self.error or self.done or owner not in self.codes or not 0<=owner<1024:
            raise ValueError('invalid handler owner or stopped VM')
        if not 0<=pc<min(len(self.codes[owner]),0x4000) or self.pc>=len(self.code):
            raise ValueError('handler or resume PC out of bounds')
        pointer=self.pointer(self.owner,self.pc)
        self.stack.enter(argc,pointer)
        self.owner=owner;self.code=self.codes[owner];self.pc=pc

    def run(self,budget):
        for _ in range(budget):
            result=self.step()
            if result!='running':return result
        return 'done' if self.done else 'budget'

    def set_tick(self,tick,respect_delay=True):
        if not self.tick<=tick<(1<<64):raise ValueError('tick out of bounds or backwards')
        self.tick=tick;self.respect_delay=respect_delay

    def resume_frame(self,tick):
        if not self.tick<=tick<(1<<64):raise ValueError('tick out of bounds or backwards')
        if self.frame_yield:
            if self.stack.height==0 or self.frame_token_index!=self.stack.top-1:
                raise StackError('pending frame token is not top of expression stack')
            token=self.stack.words[self.frame_token_index]&0xffffffff
            duration=token>>24;age=((tick&0xffffffff)-(token&0xffffff))&0xffffffff
            self.set_tick(tick,True)
            if duration>=2 and age<duration:return 'wait'
            self.stack.pop();self.frame_yield=False
        else:self.set_tick(tick,True)
        return 'running'

    def seed_frame(self):
        if self.error or self.done:raise ValueError('stopped VM')
        index=self.stack.top;self.stack.push(0)
        self.frame_token_index=index;self.frame_yield=True
