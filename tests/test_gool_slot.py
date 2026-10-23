"""Original slot selection/reclamation versus resumable production owner."""
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
from test_gool_admission import Result,READ,WRITE

SEARCH=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_uint32,C.c_uint32,C.c_uint32,C.POINTER(C.c_uint32))
RETIRE=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_uint32,C.c_uint32)
REPARENT=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_uint32,C.c_uint32)
class Services(C.Structure):
    _fields_=[('context',C.c_void_p),('read',READ),('write',WRITE),('search',SEARCH),
              ('retire',RETIRE),('reparent',REPARENT),('protected_address',C.c_uint32),('max_reclaims',C.c_uint32)]
class Cursor(C.Structure):
    _fields_=[('services',Services),('admission',Result)]+[(n,C.c_uint32) for n in
        ('slot','parent','candidate','root_index','root','reclaims','phase')]+[
        ('active',C.c_int),('status',C.c_int),('native_result',C.c_int32)]

def main():
    image=Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes());assert image.sha256==ORIGINAL_SHA256
    protected=0x80060a90;free=0x80068ee0;reserved=0x81200000;slot=0x81201000;victim=0x81202000
    rng=random.Random(16884)
    with tempfile.TemporaryDirectory(prefix='crash-slot-') as folder:
        dll=Path(folder)/'slot.dll'
        subprocess.run([sys.executable,'-m','ziglang','cc','-shared','-O3','-Wall','-Wextra',
                        '-Werror',str(ROOT/'source/gool_slot.c'),'-o',str(dll)],check=True)
        lib=C.CDLL(str(dll));lib.gool_slot_begin.argtypes=[C.POINTER(Cursor),C.POINTER(Result),C.POINTER(Services)]
        lib.gool_slot_step.argtypes=[C.POINTER(Cursor),C.c_uint32]
        for case in range(384):
            is_player=case%7==0;initial_free=case%3==0;no_candidate=case%11==0
            root_type=2 if case%2 else 3;head_offset=4 if root_type==2 else 0x4c
            needed=1+case%3;found_root=0x80068ea0+rng.randrange(8)*8
            memory={0x80068e98:reserved,protected:0,free:root_type,free+4:0,free+0x4c:0,
                    0x8006087c:case%2}
            if initial_free:memory[free+head_offset]=slot
            expected=memory.copy();events=[];native_retires=[0]
            def native_search(regs,ram):
                events.append(('search',regs[4],regs[5],regs[6]))
                assert regs[5]==0x80019cc0 and regs[6] in (0x20000,0x420000)
                ram[0x8006087c]^=1
                regs[2]=victim if not no_candidate and regs[4]==found_root else 0
            def native_retire(regs,ram):
                events.append(('retire',regs[4],regs[5]));assert regs[4]==victim and regs[5]==0
                native_retires[0]+=1
                if native_retires[0]==needed:ram[free+head_offset]=slot
                regs[2]=0xffffff01 # native return ignored by this creation stage
            def native_reparent(regs,ram):
                events.append(('reparent',regs[4],regs[5]));regs[2]=2
            registers=replay(image,0x8001a084,{20:int(is_player),28:0x80060878},expected,
                             stops=(0x8001a154,0x8001a400),hooks={0x80019844:native_search,
                             0x8001aa78:native_retire,0x8001962c:native_reparent},max_steps=4096)
            exhausted=registers[2]==0xffffffea
            for budget in (1,5,1000):
                data=memory.copy();actual_events=[];retired=[0]
                @READ
                def read(_,address,out):
                    if address not in data:return -99
                    out[0]=data[address];return 0
                @WRITE
                def write(_,address,value):
                    if address not in data:return -99
                    data[address]=value;return 0
                @SEARCH
                def search(_,root,predicate,flags,out):
                    actual_events.append(('search',root,predicate,flags));data[0x8006087c]^=1
                    out[0]=victim if not no_candidate and root==found_root else 0;return 0
                @RETIRE
                def retire(_,candidate,notify):
                    actual_events.append(('retire',candidate,notify));retired[0]+=1
                    if retired[0]==needed:data[free+head_offset]=slot
                    return 0
                @REPARENT
                def reparent(_,node,parent):actual_events.append(('reparent',node,parent));return 0
                services=Services(None,read,write,search,retire,reparent,protected,8)
                admission=Result(0x81300000,0,int(is_player),1,0);cursor=Cursor()
                assert lib.gool_slot_begin(C.byref(cursor),C.byref(admission),C.byref(services))==1
                assert lib.gool_slot_begin(C.byref(cursor),C.byref(admission),C.byref(services))==-82
                assert lib.gool_slot_step(C.byref(cursor),0)==1
                for step in range(100):
                    status=lib.gool_slot_step(C.byref(cursor),budget)
                    if status!=1:break
                assert status==0,(case,budget,status)
                assert cursor.native_result==(-22 if exhausted else 0)
                if not exhausted:
                    assert cursor.slot==(reserved if is_player else slot)
                    assert cursor.parent==(0x80068ed0 if is_player else 0x80068eb8)
                assert actual_events==events,(case,budget,actual_events,events)
                assert data==expected
        # A retirement provider that never publishes a free slot is bounded;
        # completed retirements remain visible and cannot replay after failure.
        data={0x80068e98:reserved,protected:0,free:2,free+4:0,0x8006087c:0}
        actual_events=[];retired=[0];needed=9999;no_candidate=False;found_root=0x80068ed8
        services=Services(None,read,write,search,retire,reparent,protected,2)
        admission=Result(0x81300000,0,0,1,0);cursor=Cursor()
        assert lib.gool_slot_begin(C.byref(cursor),C.byref(admission),C.byref(services))==1
        assert lib.gool_slot_step(C.byref(cursor),100)==-83 and retired[0]==2
        snapshot=list(actual_events)
        assert lib.gool_slot_step(C.byref(cursor),100)==-83 and actual_events==snapshot
        admission.admitted=0;cursor=Cursor()
        assert lib.gool_slot_begin(C.byref(cursor),C.byref(admission),C.byref(services))==-82
        _ctypes.FreeLibrary(lib._handle)
    print('PASS slots: 384 original allocation/reclamation traces under budgets1/5/1000; mutable search flags, reserved/free heads, exhaustion and retry bound')

if __name__=='__main__':main()
