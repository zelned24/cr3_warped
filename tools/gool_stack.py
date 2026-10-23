"""Bounded GOOL call-stack model for traces before native builds are enabled.

Three native header words: flags, return PC, packed previous FP/argument base.
Frame offsets are raw signed words; offset 3 begins locals. origin defaults
to virtual member 1 for standalone traces; entities bind original stackStart.
Capacity is a port budget; this model does not prove original engine limits.
"""
class StackError(ValueError):pass


class Stack:
    def __init__(self,capacity=256,max_frames=32,origin=4):
        if capacity<3 or max_frames<1 or origin<4 or origin%4 or origin+capacity*4>65536:
            raise StackError('invalid capacity or object-relative origin')
        self.words=[0]*capacity
        self.frames=[]
        self.top=0
        self.max_frames=max_frames
        self.origin=origin
        self.last_flags=None

    @property
    def floor(self):return self.frames[-1]+3 if self.frames else 0

    @property
    def height(self):return self.top-self.floor

    def push(self,value):
        if self.top==len(self.words):raise StackError('overflow')
        if not -(1<<31)<=value<(1<<31):raise StackError('word out of range')
        self.words[self.top]=value
        self.top+=1

    def pop(self):
        if self.top==self.floor:raise StackError('underflow')
        self.top-=1
        return self.words[self.top]

    def reserve(self):
        if self.top==len(self.words):raise StackError('overflow')
        self.top+=1

    def enter(self,argc,return_pointer,flags=0xffff):
        if not 0<=argc<=self.height:raise StackError('arguments underflow')
        if len(self.frames)==self.max_frames or self.top+3>len(self.words):raise StackError('call overflow')
        if not -(1<<31)<=return_pointer<(1<<31):raise StackError('return word out of range')
        if not 0<=flags<=0xffffffff:raise StackError('call flags out of range')
        previous=self.origin+self.frames[-1]*4 if self.frames else 0
        arguments=self.origin+(self.top-argc)*4
        packed=(previous<<16)|arguments
        self.frames.append(self.top)
        self.push(flags if flags<0x80000000 else flags-0x100000000)
        self.push(return_pointer)
        self.push(packed if packed<0x80000000 else packed-0x100000000)

    def leave(self):
        if not self.frames:raise StackError('no frame')
        if len(self.frames)==1:return None
        frame=self.frames[-1]
        previous=self.frames[-2]
        packed=self.words[frame+2]&0xffffffff;arguments=packed&0xffff
        if (packed>>16!=self.origin+previous*4 or arguments%4 or
            not self.origin+(previous+3)*4<=arguments<=self.origin+frame*4):
            raise StackError('invalid packed call links')
        target=self.words[frame+1]
        self.top=(arguments-self.origin)//4
        self.last_flags=self.words[frame]&0xffff
        self.frames.pop()
        return target

    def slot(self,offset):
        if not self.frames:raise StackError('no frame')
        slot=self.frames[-1]+offset
        if not 0<=slot<len(self.words):raise StackError('frame bounds')
        return slot

    def read(self,offset):return self.words[self.slot(offset)]

    def write(self,offset,value):
        slot=self.slot(offset)
        if not -(1<<31)<=value<(1<<31):raise StackError('word out of range')
        self.words[slot]=value
