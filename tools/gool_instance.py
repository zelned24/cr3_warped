"""Per-entity GOOL initialization/update model; no world scheduler or physics.

Code owners 0/1 identify base/alternate; extra cached views retain old RESUME
code across owner changes. Native C uses level GOOL lookup ordinals instead;
these model IDs are not a native address oracle.
"""
from dataclasses import dataclass
import struct
from gool_object import ObjectVM
from gool_vm import word


@dataclass(frozen=True)
class Placement:
    class_index: int
    object_id: int
    position: tuple
    path_points: int
    geo_format: int
    parameters: tuple=()
    tick: int=0


class Instance:
    def __init__(self,context,base,alternate,placement,resolve_entry=None):
        p=placement
        if (not 0<=p.class_index<=65535 or not 0<=p.object_id<=65535 or
            not 0<=context.self_index<=0xffffff or not 0<=p.path_points<=0xffffff or
            not 0<=p.tick<(1<<64) or len(p.position)!=3 or len(p.parameters)>252):
            raise ValueError('placement out of bounds')
        if any(not -(1<<31)<=v<(1<<31) for v in (*p.position,*p.parameters)):
            raise ValueError('placement word out of bounds')
        if len(base.items)!=6 or len(alternate.items) not in (3,6):
            raise ValueError('missing GOOL tables')
        declared=struct.unpack_from('<I',base.items[0],16)[0]
        offset=(declared+p.object_id)*2
        if offset+2>len(base.items[3]):raise ValueError('object ID out of table')
        state=struct.unpack_from('<h',base.items[3],offset)[0]
        if state<0 or state==255 or state*16+16>len(base.items[4]):
            raise ValueError('invalid initial state')
        motion,interact,alt,signal,pre,main=struct.unpack_from('<II4H',base.items[4],state*16)
        if alt*4+4>len(base.items[2]) or struct.unpack_from('<I',base.items[2],alt*4)[0]!=alternate.eid:
            raise ValueError('wrong alternate behavior')
        def words(data):
            if len(data)%4:raise ValueError('unaligned GOOL words')
            return struct.unpack('<%dI'%(len(data)//4),data)
        codes={0:words(base.items[1]),1:words(alternate.items[1])}
        def address(raw,optional=False):
            if optional and raw==0x3fff:return None
            raw&=0x7fff
            owner=int(bool(raw&0x4000));pc=raw&0x3fff
            if pc>=len(codes[owner]):raise ValueError('state script out of bounds')
            return owner,pc
        main_address=address(main);pre_address=address(pre,True);signal_address=address(signal,True)
        _,obj=context.resolve(0)
        if obj.vm is not None:raise ValueError('object already initialized')
        if not codes[0]:raise ValueError('empty base code')
        vm=ObjectVM(context,codes[0],constants=words(base.items[2]),
                    alternate=words(alternate.items[2]),alternate_code=codes[1],args=p.parameters,
                    stack_start=struct.unpack_from('<I',base.items[0],12)[0])
        vm.owner,vm.pc=main_address;vm.code=vm.codes[vm.owner];vm.tick=p.tick
        vm.stack.words[vm.stack.frames[0]+1]=vm.pointer(*main_address)
        vm.seed_frame()
        obj.stack_start=struct.unpack_from('<I',base.items[0],12)[0]
        # Explicitly initialize only members whose providers are known here.
        obj.members.clear()
        for n in range(3):
            obj.members[8+n]=p.position[n];obj.members[11+n]=0;obj.members[14+n]=4096
        obj.members.update({0x1a:0x20020,0x1c:word(interact),
            0x1d:word(p.class_index<<16|p.object_id),0x1e:word(context.self_index<<8),
            0x22:vm.pointer(*pre_address) if pre_address else 0,
            0x23:vm.pointer(*signal_address) if signal_address else 0,
            0x24:0,
            0x27:-1,0x28:word(p.tick),0x2e:word(p.path_points<<8),
            0x30:word(motion),0x38:word(p.geo_format),0x3b:-2,0x3e:65535})
        self.vm=vm;self.object=obj;self.state=state;self.state_generation=1
        self.base=base
        self.alternate_eid=alternate.eid;self._resume_owners={}
        self.has_resume=False;self.resume_active=False;self.resume_address=None
        self.resolve_entry=resolve_entry or {base.eid:base,alternate.eid:alternate}.get
        vm.change_state=self.change_state
        self.active=True;self.busy=False;self.last_tick=None

    def change_state(self,index,argc,meets):
        vm=self.vm;items=self.base.items
        if not 0<=argc<=min(15,vm.stack.height):raise ValueError('state arguments underflow/budget')
        if not meets:
            vm.stack.top-=argc;return 'running'
        arguments=tuple(vm.stack.words[vm.stack.top-argc:vm.stack.top]);vm.stack.top-=argc
        if index<0 or index*16+16>len(items[4]):raise ValueError('state out of bounds')
        motion,interact,alt,signal,pre,main=struct.unpack_from('<II4H',items[4],index*16)
        if 0x32 not in self.object.members:raise ValueError('unbound state mode member 0x32')
        if 0x1c not in self.object.members:raise ValueError('unbound state interact member 0x1c')
        flags=self.object.members[0x1c]&0xffffffff
        if 2<=self.object.members[0x32]<=4:flags|=0x402
        if flags&motion:return 'running'
        return self.enter_state(index,arguments)

    def enter_state(self,index,args):
        vm=self.vm;items=self.base.items;argc=len(args)
        if not self.active or vm.error or vm.done or self.state_generation>=(1<<64)-1:
            raise ValueError('invalid engine state entry')
        if self.has_resume or self.resume_active:raise ValueError('nested state entry during RESUME unsupported')
        if index<0 or index==255 or index*16+16>len(items[4]):raise ValueError('state out of bounds')
        if any(not -(1<<31)<=value<(1<<31) for value in args):raise ValueError('state argument word out of bounds')
        motion,interact,alt,signal,pre,main=struct.unpack_from('<II4H',items[4],index*16)
        if 0x24 not in self.object.members:raise ValueError('unbound state RESUME')
        resume=self.object.members[0x24]
        resume_address=vm.target(resume) if resume else None
        if resume and argc>250:raise ValueError('state arguments leave no room for RESUME frame')
        if argc>252:raise ValueError('state arguments leave no room for native frame token')
        if alt*4+4>len(items[2]):raise ValueError('state alternate constant bounds')
        eid=struct.unpack_from('<I',items[2],alt*4)[0]
        entry=self.resolve_entry(eid)
        if entry is None:raise ValueError('missing state alternate')
        def words(data):
            if len(data)%4:raise ValueError('unaligned GOOL words')
            return list(struct.unpack('<%dI'%(len(data)//4),data))
        alternate_code=words(entry.items[1]);alternate_constants=words(entry.items[2])
        codes={0:vm.codes[0],1:alternate_code}
        def address(raw,optional=False):
            if optional and raw==0x3fff:return None
            owner=int(bool(raw&0x4000));pc=raw&0x3fff
            if pc>=len(codes[owner]):raise ValueError('state script out of bounds')
            return owner,pc
        main_address=address(main);pre_address=address(pre,True);signal_address=address(signal,True)
        alias=None
        if resume_address and resume_address[0]==1 and (self.alternate_eid!=eid or vm.codes[1]!=alternate_code):
            identity=(self.alternate_eid,tuple(vm.codes[1]))
            owner=self._resume_owners.get(identity)
            if owner is None:
                owner=next((n for n in range(2,1024) if n not in vm.codes),None)
                if owner is None:raise ValueError('RESUME code owner capacity')
                alias=(identity,owner,list(vm.codes[1]))
            resume_address=(owner,resume_address[1])
        collision=self.object.members[0x1a]
        # Keep unused words: state changes do not clear the original stack.
        vm.stack.words[:argc]=args;vm.stack.top=argc;vm.stack.frames.clear()
        vm.stack.enter(argc,vm.pointer(*main_address))
        vm.frame_yield=False
        if not resume:vm.seed_frame()
        self.has_resume=bool(resume);self.resume_address=resume_address
        if alias:
            identity,owner,old_code=alias
            self._resume_owners[identity]=owner;vm.codes[owner]=old_code
        self.alternate_eid=eid
        vm.codes[1]=alternate_code;vm.alternate=alternate_constants
        vm.owner,vm.pc=main_address;vm.code=vm.codes[vm.owner];vm.next_update=0
        self.state=index;self.state_generation+=1
        self.object.members.update({0x28:word(vm.tick),0x1c:word(interact),0x30:word(motion),
            0x1a:word(collision|0x20020),
            0x22:vm.pointer(*pre_address) if pre_address else 0,
            0x23:vm.pointer(*signal_address) if signal_address else 0,0x24:0})
        return 'new_state'

    def start_resume(self):
        if not self.active or not self.has_resume or self.resume_active or self.vm.error:
            raise ValueError('no pending RESUME entry')
        self.vm.invoke(*self.resume_address)
        self.has_resume=False;self.resume_active=True
        self.vm.mode=0x13;self.vm.respect_delay=False
        return 'running'

    def state_phase(self,tick):
        vm=self.vm
        if self.has_resume:
            self.phase='resume';self.handler_depth=len(vm.stack.frames);self.start_resume();return
        pointer=self.object.members[0x22]
        self.phase='pre' if pointer else 'main_pending'
        vm.mode=3 if pointer else 4;vm.set_tick(tick,not pointer)
        if pointer:
            self.handler_depth=len(vm.stack.frames);vm.invoke(*vm.target(pointer))

    @classmethod
    def from_entity(cls,archive,entity,context,tick=0):
        if context.self_index!=entity.lookup_index or not entity.positions:
            raise ValueError('placement lookup/path mismatch')
        base=archive.entry(entity.file_name)
        if base is None:raise ValueError('missing base behavior')
        import zdat
        table=zdat.parse_gool_table(base)
        if entity.object_id is None or not 0<=entity.object_id<len(table.object_indices):
            raise ValueError('invalid object ID')
        state=table.object_indices[entity.object_id]
        if state<0 or state==255 or state>=len(table.states):raise ValueError('invalid initial state')
        alternate=archive.entry(table.states[state].alt_name)
        if alternate is None:raise ValueError('missing alternate behavior')
        import nsf
        return cls(context,base,alternate,Placement(entity.file_index,entity.object_id,
                   tuple(entity.positions[0]),len(entity.path),
                   0 if entity.geometry_format is None else entity.geometry_format,
                   tuple(entity.parameters),tick),lambda eid:archive.entry(nsf.eid_str(eid)))

    def update(self,tick,budget):
        vm=self.vm
        if not self.active:
            if vm.error:raise vm.error
            return 'done'
        if budget<0:raise ValueError('negative budget')
        if not budget:return 'budget'
        if self.resume_active and (not self.busy or getattr(self,'phase',None)!='resume'):
            raise ValueError('RESUME owned by delivery or abandoned context')
        if self.busy:
            if tick!=self.update_tick:raise ValueError('unfinished update needs same tick')
        else:
            if self.has_resume and tick!=vm.tick:raise ValueError('unfinished RESUME entry needs same tick')
            if self.last_tick is not None:
                if tick<self.last_tick:raise ValueError('backwards tick')
                if tick==self.last_tick:
                    if self.has_resume:raise ValueError('pending RESUME requires its entry owner')
                    return 'skipped'
            pointer=0 if self.has_resume else self.object.members[0x22]
            target=vm.target(pointer) if pointer else None
            vm.begin_update(tick)
            self.update_tick=tick;self.busy=True;self.phase='pre' if target else 'main_pending'
            vm.mode=3 if target else 4
            if self.has_resume:self.state_phase(tick)
            elif target:
                self.handler_depth=len(vm.stack.frames)
                vm.invoke(*target);vm.set_tick(tick,False)
        try:
            for _ in range(budget):
                if self.phase=='main_pending':
                    vm.mode=4
                    if vm.resume_frame(tick)=='wait':
                        self.busy=False;self.last_tick=tick;return 'wait'
                    self.phase='main'
                result=vm.step()
                if result in ('signal_return','failed_signal','send','send_replaced'):return result
                if result=='done':self.active=False;self.busy=False;return result
                if result=='new_state':
                    self.state_phase(tick)
                    continue
                if result=='resume_return':
                    if self.phase!='resume' or not self.resume_active or len(vm.stack.frames)!=self.handler_depth:
                        raise ValueError('unexpected RESUME return frame')
                    self.resume_active=False;self.state_phase(tick)
                    continue
                if self.phase=='pre' and len(vm.stack.frames)==self.handler_depth:
                    self.phase='main_pending';vm.set_tick(tick,True)
                if result=='wait':
                    self.busy=False;self.last_tick=tick;return result
        except ValueError:
            self.active=False;raise
        return 'budget'
