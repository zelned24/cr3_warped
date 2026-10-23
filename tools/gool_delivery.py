"""Budgeted ON_SIGNAL selection and explicit state/code application.

The owner supplies a span handle for incoming arguments and handles nested
SEND events. This is a resumable component, not the world delivery pump.
"""
import struct
from gool_vm import Receiver,word
from gool_signal import Route,lookup,resolve_script
from gool_send import finish


class Delivery:
    def __init__(self,sender,target,argument_pointer,tick,arguments=None):
        if not sender.active or sender.vm.error or sender.vm.done or not sender.vm.send_pending or tick!=sender.vm.tick:
            raise ValueError('no live sender request')
        self.sender=sender;self.target=target;self.request=sender.vm.send
        if arguments is not None and (not isinstance(arguments,list) or tuple(arguments)!=self.request.arguments):
            raise ValueError('incoming argument span does not match request')
        self.arguments=list(self.request.arguments) if arguments is None else arguments
        self.span_memory=None;self.span_pointer=None
        self.receiver=Receiver();self.bound=False;self.active=True;self.status='running';self.error=None
        self.phase='select';self.route=None
        self.generation=None;self.replaced=False;self.current_index=None
        try:
            if target is None or not target.active or target.vm.error or target.vm.done:
                self.ack(0);self.complete();return
            sc=sender.vm.context;tc=target.vm.context
            if (self.request.target!=tc.pointer(tc.self_index) or sc.objects is not tc.objects or
                tc.objects[tc.self_index] is not target.object):
                raise ValueError('target/registry mismatch')
            count=0 if len(target.base.items)==3 else struct.unpack_from('<I',target.base.items[0],16)[0]
            if ((self.request.raw_signal&0xffffffff)>>8)>=count:
                self.ack(0);self.complete();return
            target.vm.set_tick(tick,target.vm.respect_delay)
            self.ack(1);target.object.members[7]=sc.pointer(sc.self_index)
            pointer=target.object.members[0x23]
            if not pointer:return
            if not argument_pointer:raise ValueError('incoming argument span handle is unbound')
            vm=target.vm;owner,pc=vm.target(pointer)
            if vm.stack.top+5>len(vm.stack.words):raise ValueError('handler stack overflow')
            self.bind()
            vm.stack.push(self.request.raw_signal);vm.stack.push(word(argument_pointer))
            vm.invoke(owner,pc,2);vm.set_tick(tick,False)
            vm.receiver=self.receiver;vm.mode=8;self.phase='handler'
        except ValueError as error:
            self.restore();self.active=False;self.status='failed';self.error=error;raise

    @classmethod
    def mapped(cls,sender,target,memory,tick):
        if memory is None or sender.vm.memory is not memory or (target is not None and target.active and target.vm.memory is not memory):
            raise ValueError('delivery VMs must share the memory domain')
        if not sender.vm.send_pending:raise ValueError('no pending SEND to map')
        arguments=list(sender.vm.send.arguments);pointer=memory.borrow(arguments)
        try:job=cls(sender,target,pointer,tick,arguments)
        except ValueError:
            memory.release(pointer);raise
        if job.active:
            job.span_memory=memory;job.span_pointer=pointer
        else:memory.release(pointer)
        return job

    def release_span(self):
        if self.span_memory is not None:
            memory,pointer=self.span_memory,self.span_pointer
            self.span_memory=None;self.span_pointer=None
            memory.release(pointer)

    def restore(self):
        if self.bound:
            vm=self.target.vm
            vm.mode,vm.respect_delay,vm.receiver,vm.send_pending,vm.send=self.saved
            self.bound=False

    def bind(self):
        vm=self.target.vm
        self.saved=(vm.mode,vm.respect_delay,vm.receiver,vm.send_pending,vm.send)
        self.saved_depth=len(vm.stack.frames);self.generation=self.target.state_generation
        self.bound=True;vm.send_pending=False

    def ack(self,value):self.sender.object.members[0x25]=int(bool(value))

    def abort(self):
        self.restore();self.release_span();self.active=False;self.status='aborted'

    def complete(self):
        self.restore();self.status=finish(self.sender.vm);self.active=False
        if self.replaced and self.target.busy and self.target.vm.context.self_index==self.current_index:
            self.target.phase='main_pending' if self.target.vm.frame_yield else 'main'
        self.release_span()
        return self.status

    def select(self):
        self.restore();target=self.target
        if self.receiver.new_state==-1:
            self.route=lookup(target.base,self.request.raw_signal&0xffffffff)
            self.ack(self.route.kind not in ('none','outside_table'))
        else:
            state=self.receiver.new_state
            if state<0:raise ValueError('invalid receiver state')
            self.route=Route('none' if state==255 else 'state',
                             (self.request.raw_signal&0xffffffff)>>8,state,state)
            self.ack(self.receiver.was_b)
        if self.route.kind in ('none','outside_table'):return self.complete()
        raw=self.request.raw_signal
        if self.route.kind=='script':resolve_script(target.base,self.route)
        else:
            index=self.route.value
            if index*16+16>len(target.base.items[4]):raise ValueError('selected signal state out of bounds')
            motion=struct.unpack_from('<I',target.base.items[4],index*16)[0]
            flags=target.object.members[0x1c]&0xffffffff
            if raw in (0x1800,0x2600,0x1900,0x2500):flags&=~2
            context=target.vm.context
            if context.player is not None and context.self_index==context.player:
                if 0x32 not in target.object.members:raise ValueError('unbound player mode')
                if 2<=target.object.members[0x32]<=4:flags|=0x402
            if flags&motion:self.ack(0);return self.complete()
            if raw in (0x1800,0x1900):
                target.object.members[0x1a]=word(target.object.members[0x1a]|0x10000)
        target.object.members[0x37]=raw
        self.generation=target.state_generation
        self.active=False;self.status='route'
        return self.status

    def start_pre(self):
        target=self.target;vm=target.vm
        self.generation=target.state_generation;self.replaced=True
        if target.has_resume:
            self.saved_depth=len(vm.stack.frames);target.start_resume()
            self.phase='apply_resume';return 'budget'
        if vm.context.self_index!=self.current_index:return self.complete()
        pointer=target.object.members[0x22]
        if not pointer:return self.complete()
        self.saved_depth=len(vm.stack.frames)
        vm.invoke(*vm.target(pointer));vm.mode=3;vm.respect_delay=False
        self.phase='apply_pre';return 'budget'

    def apply(self,current_index,budget):
        """Owner supplies native current-object index, or None between updates.

        Incoming arguments are this job's mutable span, never RECEIVE outputs.
        Nested SEND is handed back to the owner without stepping its child.
        """
        if self.error:raise self.error
        if budget<0:raise ValueError('negative instruction budget')
        continuing=self.active and self.phase in ('apply_code','apply_pre','apply_resume')
        if not continuing and self.status!='route':raise ValueError('no selected route to apply')
        if continuing and current_index!=self.current_index:raise ValueError('application current object changed')
        if not budget:return 'budget'
        try:
            vm=self.target.vm
            if not continuing:
                if (self.target.state_generation!=self.generation or not self.target.active or vm.error or vm.done or
                    not self.sender.vm.send_pending):raise ValueError('stale selected route')
                if vm.receiver is not None or (self.target.busy and vm.context.self_index!=current_index):
                    raise ValueError('unsupported application into suspended foreign context')
                self.current_index=current_index
                self.bind();self.active=True
                if self.route.kind=='state':
                    self.target.enter_state(self.route.value,tuple(self.arguments))
                    result=self.start_pre()
                    if not self.active:return result
                elif self.route.kind=='script':
                    _,pc=resolve_script(self.target.base,self.route)
                    if vm.stack.top+len(self.arguments)+3>len(vm.stack.words):raise ValueError('signal script stack overflow')
                    for value in self.arguments:vm.stack.push(value)
                    vm.invoke(0,pc,len(self.arguments));vm.mode=3;vm.respect_delay=False
                    self.phase='apply_code'
                else:raise ValueError('invalid application route')
            while budget:
                # A child may replace this root, including a same-ID state.
                if self.target.state_generation!=self.generation:
                    raise ValueError('nested state replacement requires owner mode integration')
                if vm.send_pending:return 'send'
                budget-=1;result=vm.step()
                if result=='send':return result
                if result=='resume_return':
                    if self.phase!='apply_resume' or not self.target.resume_active or len(vm.stack.frames)!=self.saved_depth:
                        raise ValueError('unexpected RESUME application return')
                    self.target.resume_active=False;result=self.start_pre()
                    if not self.active:return result
                    continue
                if result=='new_state':
                    result=self.start_pre()
                    if not self.active:return result
                    continue
                if result in ('done','wait','signal_return','failed_signal'):
                    raise ValueError('unsupported signal application control path')
                if len(vm.stack.frames)==self.saved_depth:
                    if self.phase=='apply_resume':raise ValueError('RESUME exited without native return token')
                    return self.complete()
                if len(vm.stack.frames)<self.saved_depth:raise ValueError('signal application frame lost')
            return 'budget'
        except ValueError as error:
            self.restore();self.release_span();self.active=False;self.status='failed';self.error=error
            if self.target.vm.error:self.target.active=False
            raise

    def step(self,budget):
        if self.error:raise self.error
        if self.active and self.phase in ('apply_code','apply_pre','apply_resume'):return self.apply(self.current_index,budget)
        if not self.active:return self.status
        if budget<0:raise ValueError('negative instruction budget')
        if not budget:return 'budget'
        try:
            while self.phase=='handler' and budget:
                if self.target.state_generation!=self.generation:
                    raise ValueError('handler root replaced during nested delivery')
                budget-=1;result=self.target.vm.step()
                if result=='send':return result
                if result in ('new_state','done','wait','resume_return'):
                    raise ValueError('handler control path needs remaining native mode integration')
                if result in ('signal_return','failed_signal'):
                    if len(self.target.vm.stack.frames)!=self.saved_depth:
                        raise ValueError('handler did not restore its frame')
                    self.phase='select';break
            return self.select() if self.phase=='select' else 'budget'
        except ValueError as error:
            self.restore();self.release_span();self.active=False;self.status='failed';self.error=error
            if self.target.vm.error:self.target.active=False
            raise
