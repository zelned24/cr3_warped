"""Native SEND wrapper frontend. Delivery is an explicit engine handoff."""
from dataclasses import dataclass
from gool_operand import decode
from gool_stack import StackError


@dataclass(frozen=True)
class Request:
    target: int
    raw_signal: int
    arguments: tuple
    caller_mode: int


def begin(vm,inst):
    if not vm.send_enabled or vm.send_pending or inst>>24!=0x3c:
        raise ValueError('invalid/unbound SEND entry')
    target=vm.read(0xe00|((inst>>21)&7))
    raw=inst&0xfff;arg=decode(raw);address=True;cached=False;value=0
    if arg.kind=='boolean':
        if raw!=0xbe0:raise ValueError('unsupported boolean address in SEND (only BE0 is null)')
        address=False
    elif arg.kind=='variable' and arg.value==31:
        value=vm.stack.pop();cached=True
    elif arg.kind=='frame':vm.stack.read(arg.value) # bound the address, load later
    elif arg.kind=='indirect':
        address=vm.read(0xe00|arg.base)!=0
        if address and arg.value==31:raise ValueError('unbound raw indirect stack pointer for SEND')
    collision=vm.read(0xe1a)&0xffffffff
    collision&=~0x20000
    vm.write(0xe1a,collision if collision<0x80000000 else collision-0x100000000)
    condition=vm.read(0xe00|((inst>>12)&63))
    argc=(inst>>18)&7
    if argc>vm.stack.height:raise StackError('SEND arguments underflow')
    if not condition or not target or not address:
        vm.write(0xe25,0);vm.stack.top-=argc;vm.pc+=1;return 'running'
    args=tuple(vm.stack.words[vm.stack.top-argc:vm.stack.top])
    if not cached:value=vm.read(raw)
    vm.send=Request(target,value,args,vm.mode);vm.send_pending=True;vm.pc+=1
    return 'send'


def finish(vm):
    if not vm.send_pending or vm.error or vm.done:raise ValueError('no pending SEND')
    collision=vm.read(0xe1a)&0xffffffff
    if not collision&0x20000:
        if len(vm.send.arguments)>vm.stack.height:raise StackError('SEND cleanup underflow')
        old_top=vm.stack.top;vm.stack.top-=len(vm.send.arguments)
        if vm.frame_yield and vm.stack.top<=vm.frame_token_index<old_top:vm.frame_yield=False
        vm.send_pending=False;return 'running'
    if not vm.send.caller_mode&2:
        index=vm.stack.top-1;vm.stack.pop()
        if vm.frame_yield and vm.frame_token_index==index:vm.frame_yield=False
    vm.send_pending=False;return 'send_replaced'
