"""Borrowed word spans in an explicit engine address domain (tag 7).

Not a host pointer map or PS1 RAM emulator. Addresses are byte-based, never
reused until the domain is destroyed. Engines must release before recycling
storage and keep the domain alive for every VM sharing these references.
"""


class Memory:
    def __init__(self):
        self.spans={};self.next_address=0x70000004

    def borrow(self,words,writable=True):
        if not isinstance(words,list) or any(not isinstance(v,int) or not -(1<<31)<=v<(1<<31) for v in words):
            raise ValueError('invalid borrowed word storage')
        end=self.next_address+len(words)*4+4
        if len(self.spans)>=32 or end>0x80000000:raise ValueError('memory domain capacity')
        pointer=self.next_address;self.next_address=end
        self.spans[pointer]=(words,len(words),bool(writable));return pointer

    def release(self,pointer):
        if pointer not in self.spans:raise ValueError('unbound memory span')
        del self.spans[pointer]

    def resolve(self,address):
        if not isinstance(address,int) or not 0<=address<=0xffffffff or address>>28!=7 or address&3:
            raise ValueError('invalid memory word address')
        for base,(words,count,writable) in self.spans.items():
            if base<=address<base+count*4:
                if len(words)!=count:raise ValueError('borrowed span resized')
                return words,(address-base)//4,writable
        raise ValueError('unbound memory word')

    def read(self,address):
        words,index,_=self.resolve(address);value=words[index]
        if not isinstance(value,int) or not -(1<<31)<=value<(1<<31):raise ValueError('invalid borrowed word')
        return value

    def write(self,address,value):
        words,index,writable=self.resolve(address)
        if not writable:raise ValueError('read-only memory span')
        if not isinstance(value,int) or not -(1<<31)<=value<(1<<31):raise ValueError('memory word out of bounds')
        words[index]=value
