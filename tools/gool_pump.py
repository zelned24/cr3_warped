"""Bounded owner of delivery jobs; not the world's entity-update scheduler.

One work unit starts a job or advances a job by at most one VM instruction.
Selection/application and completion also cost units. The owner must keep the
registry, object slots, shared memory and timeline stable until terminal.
"""
from gool_delivery import Delivery


class Pump:
    MAX_DEPTH=16 # port resource budget, not an original engine limit

    def __init__(self,instances,memory):
        if not isinstance(instances,list) or not instances or memory is None:
            raise ValueError('missing instance registry or memory domain')
        self.instances=instances;self.count=len(instances);self.memory=memory
        self.jobs=[];self.pending=None;self.active=False;self.status='idle';self.error=None

    def check(self,instance):
        ctx=instance.vm.context;index=ctx.self_index
        if (len(self.instances)!=self.count or not 0<=index<self.count or
            self.instances[index] is not instance or ctx.objects is not self.objects or
            len(ctx.objects)!=self.count or ctx.objects[index] is not instance.object or
            instance.vm.memory is not self.memory):
            raise ValueError('instance registry/object/memory mismatch')

    def validate(self):
        self.check(self.root)
        if self.root.vm.tick!=self.tick:raise ValueError('pump timeline changed')
        if self.current_index is not None:
            if (not 0<=self.current_index<self.count or self.instances[self.current_index] is None or
                self.instances[self.current_index].vm.context.self_index!=self.current_index):
                raise ValueError('current object is not registered')
            self.check(self.instances[self.current_index])
        for job in self.jobs:
            for instance in (job.sender,job.target):
                if instance is None:continue
                self.check(instance)
                if instance.vm.tick!=self.tick:raise ValueError('pump timeline changed')

    def begin(self,sender,tick,current_index=None):
        if self.active or self.jobs:raise ValueError('pump already owns a delivery')
        if not 0<=tick<(1<<64):raise ValueError('pump tick bounds')
        if not sender.active or sender.vm.error or sender.vm.done or not sender.vm.send_pending:
            raise ValueError('no live sender request')
        self.objects=sender.vm.context.objects;self.root=sender;self.tick=tick;self.current_index=current_index
        self.validate()
        self.pending=sender;self.active=True;self.status='budget';self.error=None
        return 'budget'

    def resolve(self,pointer):
        raw=pointer&0xffffffff
        if raw>>28!=6 or raw&3:raise ValueError('invalid signal destination pointer')
        index=(raw&0xfffffff)>>2
        if index>=self.count:raise ValueError('signal destination outside registry')
        # An explicitly empty object slot is not an unimplemented live object.
        if self.objects[index] is None:return None
        instance=self.instances[index]
        if instance is None:raise ValueError('live object lacks instance provider')
        if instance.vm.context.self_index!=index:raise ValueError('destination registry slot mismatch')
        self.check(instance);return instance

    def unwind(self):
        for job in reversed(self.jobs):job.abort()
        self.jobs.clear();self.pending=None;self.active=False

    def abort(self):
        if not self.active:return self.status
        self.unwind();self.status='aborted';return self.status

    def step(self,budget):
        if self.error:raise self.error
        if not self.active:return self.status
        if budget<0:raise ValueError('negative pump work budget')
        if not budget:return 'budget'
        try:
            for _ in range(budget):
                self.validate()
                if self.pending is not None:
                    if len(self.jobs)>=self.MAX_DEPTH:raise ValueError('signal nesting capacity')
                    sender=self.pending;self.check(sender)
                    if sender.vm.tick!=self.tick:raise ValueError('pump timeline changed')
                    target=self.resolve(sender.vm.send.target)
                    job=Delivery.mapped(sender,target,self.memory,self.tick)
                    self.jobs.append(job);self.pending=None
                    result='budget' if job.active else job.status
                else:
                    job=self.jobs[-1]
                    result=job.apply(self.current_index,1) if job.status=='route' and not job.active else job.step(1)
                if result=='send':
                    self.pending=job.target
                elif result in ('budget','route'):continue
                elif not job.active:
                    if result not in ('running','send_replaced'):raise ValueError('unexpected delivery terminal status')
                    self.jobs.pop()
                    if not self.jobs:
                        self.active=False;self.status=result;return result
                else:raise ValueError('unexpected live delivery status')
            return 'budget'
        except ValueError as error:
            self.unwind();self.error=error;self.status='failed';raise
