"""Original2157C ordering and bounded asynchronous retirement ownership."""
import _ctypes
import ctypes as C
from pathlib import Path
import random
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import cb3
from psx_native import Image,ORIGINAL_SHA256
from test_gool_stack_native import replay
from test_zone_transition import READ,WRITE,Services

RETAIN=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_uint32,C.POINTER(C.c_uint64))
RELEASE=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_uint64)
BEGIN=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_uint32)
STEP=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_uint32)
CANCEL=C.CFUNCTYPE(C.c_int,C.c_void_p)
class ExitServices(C.Structure):
    _fields_=[('memory',Services),('retain',RETAIN),('release',RELEASE),
              ('begin',BEGIN),('step',STEP),('cancel',CANCEL)]
class Cursor(C.Structure):
    _fields_=[('services',ExitServices),('entry_lease',C.c_uint64),('player_lease',C.c_uint64)]+[
        (n,C.c_uint32) for n in ('entry','player','player_zone','header','flags','phase')]+[
        (n,C.c_int) for n in ('active','status','retirement_owned')]

ENTRY,PLAYER,HEADER=0x81000000,0x81001000,0x81002000
FLAG=HEADER+0x2bc

class Fixture:
    def __init__(self,memory,retired_flags=0,delay=0,fail_access=None):
        self.data=memory.copy();self.events=[];self.leases={};self.serial=0
        self.retired_flags=retired_flags;self.delay=delay;self.begins=self.steps=self.cancels=0
        self.fail_access=fail_access;self.accesses=0;self.release_failure=self.cancel_failure=False
        self.begin_error=self.step_error=self.retain_error=0
        @READ
        def read(_,address,out):
            if self.fail() or address not in self.data:return -99
            self.events.append(('read',address));out[0]=self.data[address];return 0
        @WRITE
        def write(_,address,value):
            if self.fail() or address not in self.data:return -99
            self.events.append(('write',address,value));self.data[address]=value;return 0
        @RETAIN
        def retain(_,address,lease):
            if self.retain_error:return self.retain_error
            self.serial+=1;self.leases[self.serial]=address;lease[0]=self.serial;return 0
        @RELEASE
        def release(_,lease):
            if self.release_failure:return -98
            assert lease in self.leases
            del self.leases[lease];return 0
        @BEGIN
        def begin(_,entry):
            assert entry==ENTRY
            self.begins+=1
            if self.begin_error:return self.begin_error
            if self.delay:return 1
            self.retired();return 0
        @STEP
        def step(_,budget):
            assert budget==1
            self.steps+=1
            if self.step_error:return self.step_error
            self.delay-=1
            if self.delay:return 1
            self.retired();return 0
        @CANCEL
        def cancel(_):
            self.cancels+=1
            return -97 if self.cancel_failure else 0
        self.services=ExitServices(Services(None,read,write),retain,release,begin,step,cancel)
    def retired(self):
        self.events.append(('retire',ENTRY));self.events.append(('write',FLAG,self.retired_flags))
        self.data[FLAG]=self.retired_flags
    def fail(self):
        index=self.accesses;self.accesses+=1
        return index==self.fail_access

def original(image,memory,retired_flags):
    events=[];shared=set(memory)
    class Traced(dict):
        def __getitem__(self,address):
            if address in shared:events.append(('read',address))
            return super().__getitem__(address)
        def __setitem__(self,address,value):
            if address in shared:events.append(('write',address,value))
            super().__setitem__(address,value)
    data=Traced(memory)
    def retirement(regs,ram):
        assert regs[4]==ENTRY
        events.append(('retire',ENTRY));ram[FLAG]=retired_flags
    replay(image,0x8002157c,{4:ENTRY,29:0x810f0000},data,hooks={0x8001aa98:retirement})
    return {key:dict.__getitem__(data,key) for key in shared},events

def run(lib,fixture,budget=1):
    cursor=Cursor()
    assert lib.zone_exit_begin(C.byref(cursor),ENTRY,C.byref(fixture.services))==1
    assert lib.zone_exit_step(C.byref(cursor),0)==1
    for _ in range(50):
        status=lib.zone_exit_step(C.byref(cursor),budget)
        if status!=1:break
    else:raise AssertionError('unbounded exit')
    return cursor,status

def main():
    image=Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes())
    assert image.sha256==ORIGINAL_SHA256
    rng=random.Random(2157)
    with tempfile.TemporaryDirectory(prefix='crash-zone-exit-') as folder:
        dll=Path(folder)/'exit.dll'
        subprocess.run([sys.executable,'-m','ziglang','cc','-shared','-O3','-Wall','-Wextra','-Werror',
                        str(ROOT/'source/zone_transition.c'),'-o',str(dll)],check=True)
        lib=C.CDLL(str(dll))
        lib.zone_exit_begin.argtypes=[C.POINTER(Cursor),C.c_uint32,C.POINTER(ExitServices)]
        lib.zone_exit_step.argtypes=[C.POINTER(Cursor),C.c_uint32]
        lib.zone_exit_close.argtypes=[C.POINTER(Cursor)]
        try:
            for case in range(512):
                memory={0x80060a90:PLAYER,PLAYER+0x18:ENTRY if case%2 else 0x81003000,
                        ENTRY+0x10:HEADER,0x80065d60:rng.getrandbits(32),FLAG:rng.getrandbits(32)}
                retired_flags=rng.getrandbits(32)
                expected,events=original(image,memory,retired_flags)
                for delay in (0,3):
                    f=Fixture(memory,retired_flags,delay);cursor,status=run(lib,f,1+case%9)
                    assert status==0 and f.data==expected and f.events==events,(case,delay,f.events,events)
                    assert lib.zone_exit_step(C.byref(cursor),100)==0
                    assert len(f.leases)==2
                    assert f.begins==int(bool(memory[FLAG]&1))
                    assert f.steps==(delay if memory[FLAG]&1 else 0)
                    assert lib.zone_exit_begin(C.byref(cursor),ENTRY,C.byref(f.services))==-70
                    assert lib.zone_exit_close(C.byref(cursor))==0 and not f.leases
                    assert lib.zone_exit_close(C.byref(cursor))==0
            memory={0x80060a90:PLAYER,PLAYER+0x18:ENTRY,ENTRY+0x10:HEADER,0x80065d60:77,FLAG:3}
            # Every native memory boundary can fail without replaying effects.
            for failure in range(8):
                f=Fixture(memory,99,2,fail_access=failure);cursor,status=run(lib,f,30)
                assert status==-99
                snapshot=(dict(f.data),list(f.events),f.begins,f.steps)
                assert lib.zone_exit_step(C.byref(cursor),50)==-99
                assert snapshot==(f.data,f.events,f.begins,f.steps)
                assert lib.zone_exit_close(C.byref(cursor))==0 and not f.leases
            # Provider errors after begin retain the mandatory cancellation owner.
            for location in ('begin_error','step_error'):
                f=Fixture(memory,99,3);setattr(f,location,-96);cursor,status=run(lib,f,30)
                assert status==-96 and cursor.retirement_owned and f.data[FLAG]==3
                f.cancel_failure=True
                assert lib.zone_exit_close(C.byref(cursor))==-97 and len(f.leases)==2
                f.cancel_failure=False;f.release_failure=True
                assert lib.zone_exit_close(C.byref(cursor))==-98 and not cursor.retirement_owned
                assert len(f.leases)==2
                f.release_failure=False
                assert lib.zone_exit_close(C.byref(cursor))==0 and not f.leases
            # Pending cancellation never fabricates completion or clears flags.
            f=Fixture(memory,99,10);cursor=Cursor()
            assert lib.zone_exit_begin(C.byref(cursor),ENTRY,C.byref(f.services))==1
            assert lib.zone_exit_step(C.byref(cursor),30)==1 and f.begins==1
            assert lib.zone_exit_close(C.byref(cursor))==0 and not f.leases and f.data[FLAG]==3
            f=Fixture(memory);f.retain_error=-95;cursor=Cursor()
            assert lib.zone_exit_begin(C.byref(cursor),ENTRY,C.byref(f.services))==-95
            assert not cursor.active and not f.leases and not f.events
        finally:_ctypes.FreeLibrary(lib._handle)
    print('PASS: 1024 original zone-exit comparisons; memory faults, pending/error cancellation and lease retries')

if __name__=='__main__':main()
