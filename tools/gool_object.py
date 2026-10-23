"""Explicit GOOL object context; no implicit engine state or entity scheduler."""
from gool_vm import VM,word
from gool_operand import decode


class Object:
    def __init__(self,stack_start=512):
        self.members={}
        self.stack_start=stack_start
        self.vm=None
        self.stack_memory=None
        self.stack_pointer=None
        self.lighting={}

    def bind_stack(self,memory):
        if self.vm is None or self.stack_memory is not None or self.vm.memory is not memory:
            raise ValueError('invalid stack binding or memory domain')
        pointer=memory.borrow(self.vm.stack.words)
        self.stack_memory=memory;self.stack_pointer=pointer
        return pointer

    def unbind_stack(self):
        if self.stack_memory is None:raise ValueError('unbound stack mapping')
        memory,pointer=self.stack_memory,self.stack_pointer
        self.stack_memory=None;self.stack_pointer=None
        memory.release(pointer)

    def stack_address(self):
        if self.vm is None or self.stack_memory is None:raise ValueError('unbound raw stack pointer')
        span=self.stack_memory.spans.get(self.stack_pointer)
        if (span is None or span[0] is not self.vm.stack.words or
            span[1]!=len(self.vm.stack.words) or not 0<=self.vm.stack.top<=span[1]):
            raise ValueError('stale stack mapping')
        return self.stack_pointer+self.vm.stack.top*4


class Context:
    def __init__(self,objects,self_index,player=None):
        if not 0<=self_index<len(objects) or objects[self_index] is None:
            raise ValueError('missing self object')
        self.objects=objects;self.self_index=self_index;self.player=player

    @staticmethod
    def pointer(index):
        if not 0<=index<0x4000000:raise ValueError('object index bounds')
        return 0x60000000|(index<<2)

    def resolve(self,base):
        self_object=self.objects[self.self_index]
        if self_object is None:raise ValueError('missing self object')
        if base==0:index=self.self_index
        elif base==5:
            if self.player is None:raise ValueError('unbound player')
            index=self.player
        else:
            if not 0<=base<8:raise ValueError('link base bounds')
            if base not in self_object.members:raise ValueError('unbound link')
            pointer=self_object.members[base]&0xffffffff
            if pointer>>28!=6 or pointer&3:raise ValueError('null or invalid object link')
            index=(pointer&0xfffffff)>>2
        if not 0<=index<len(self.objects) or self.objects[index] is None:
            raise ValueError('missing linked object')
        return index,self.objects[index]

    def read(self,base,member):
        if not 0<=member<512:raise ValueError('member bounds')
        if base in (1,2,3,4,6,7):
            obj=self.objects[self.self_index]
            if obj is None:raise ValueError('missing self object')
            if base not in obj.members:raise ValueError('unbound link')
            if obj.members[base]==0:return 0
        index,obj=self.resolve(base)
        if member==0:return self.pointer(index)
        if member==5:
            if self.player is None or not 0<=self.player<len(self.objects) or self.objects[self.player] is None:
                raise ValueError('unbound player')
            return self.pointer(self.player)
        if member==31:
            reader=self.objects[self.self_index].vm
            if reader is None or reader.memory is not obj.stack_memory:
                raise ValueError('foreign stack memory domain')
            return obj.stack_address()
        if member==32:raise ValueError('unbound remote script pointer')
        if member>=obj.stack_start:raise ValueError('unbound stack member overlay')
        if member not in obj.members:raise ValueError('unbound member')
        return obj.members[member]

    def write(self,base,member,value):
        if not 0<=member<512:raise ValueError('member bounds')
        _,obj=self.resolve(base)
        if member==32:raise ValueError('generic script pointer write')
        if member==31:
            raise ValueError('unbound raw stack pointer assignment')
        if member in (0,5) or member>=obj.stack_start:raise ValueError('unbound member provider')
        obj.members[member]=word(value)

    def lighting_read(self,base,selector):
        if not 0<=selector<64:raise ValueError('halfword bounds')
        _,obj=self.resolve(base)
        if selector<16:
            if selector not in obj.lighting:raise ValueError('unbound lighting halfword')
            bits=obj.lighting[selector]&65535
        else:
            member=(selector-16)//2
            if member<8:raise ValueError('unbound structural halfword provider')
            bits=(self.read(base,member)>>((selector&1)*16))&65535
        return bits-65536 if bits&32768 else bits

    def lighting_write(self,base,selector,value):
        if not 0<=selector<64:raise ValueError('halfword bounds')
        _,obj=self.resolve(base)
        if selector<16:obj.lighting[selector]=value&65535;return
        member=(selector-16)//2
        if member<8:raise ValueError('unbound structural halfword provider')
        previous=self.read(base,member)
        shift=(selector&1)*16
        self.write(base,member,(previous&~(65535<<shift))|((value&65535)<<shift))


class ObjectVM(VM):
    def __init__(self,context,*args,**kwargs):
        _,obj=context.resolve(0)
        if obj.vm is not None:raise ValueError('object already bound to VM')
        self.context=context
        kwargs.setdefault('stack_start',obj.stack_start)
        super().__init__(*args,members=obj.members,**kwargs)
        obj.vm=self

    def read(self,raw):
        operand=decode(raw)
        if operand.kind=='indirect':return self.context.read(operand.base,operand.value)
        if operand.kind=='variable' and operand.value not in (31,32):
            return self.context.read(0,operand.value)
        return super().read(raw)

    def write(self,raw,value):
        operand=decode(raw)
        if operand.kind=='indirect':self.context.write(operand.base,operand.value,value);return
        if operand.kind=='variable' and operand.value not in (31,32):
            self.context.write(0,operand.value,value);return
        super().write(raw,value)

    def begin_update(self,tick):
        self.set_tick(tick)
        _,obj=self.context.resolve(0)
        obj.members[0x27]=word(tick)

    def lighting_read(self,base,selector):return self.context.lighting_read(base,selector)
    def lighting_write(self,base,selector,value):self.context.lighting_write(base,selector,value)
