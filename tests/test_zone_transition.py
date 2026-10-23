"""Compare production zone predicates/entry writes with original MIPS bytes."""
import ctypes as C
import _ctypes
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

READ=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_uint32,C.POINTER(C.c_uint32))
WRITE=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_uint32,C.c_uint32)
class Services(C.Structure):
    _fields_=[('context',C.c_void_p),('read',READ),('write',WRITE)]
class Cursor(C.Structure):
    _fields_=[('services',Services)]+[(n,C.c_uint32) for n in
        ('entry','candidate','header','count','candidate_eid','index','scan_limit','phase')]+[
        ('active',C.c_int),('status',C.c_int),('contains',C.c_int)]

def main():
    image=Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes())
    assert image.sha256==ORIGINAL_SHA256
    with tempfile.TemporaryDirectory(prefix='crash-zone-native-') as folder:
        dll=Path(folder)/'zone.dll'
        subprocess.run([sys.executable,'-m','ziglang','cc','-shared','-O3','-Wall','-Wextra',
                        '-Werror',str(ROOT/'source/zone_transition.c'),'-o',str(dll)],check=True)
        lib=C.CDLL(str(dll))
        lib.zone_transition_contains_begin.argtypes=[C.POINTER(Cursor),C.c_uint32,C.c_uint32,C.c_uint32,C.POINTER(Services)]
        lib.zone_transition_contains_step.argtypes=[C.POINTER(Cursor),C.c_uint32]
        lib.zone_transition_enter.argtypes=[C.c_uint32,C.POINTER(Services)]
        rng=random.Random(214184);entry=0x81000000;header=0x81001000;candidate=0x81002000
        def providers(memory,writes,fail=None):
            @READ
            def read(_,address,out):
                if address==fail or address not in memory:return -99
                out[0]=memory[address];return 0
            @WRITE
            def write(_,address,value):
                if address==fail or address not in memory:return -99
                writes.append((address,value));memory[address]=value;return 0
            return Services(None,read,write)
        for case in range(768):
            count=rng.randrange(33);eid=rng.getrandbits(31)*2+1
            memory={entry+16:header,header+0x190:count,candidate+4:eid}
            for i in range(count):
                other=eid if rng.randrange(12)==0 else rng.getrandbits(31)*2+1
                if rng.randrange(2):memory[header+0x194+i*4]=other
                else:
                    pointer=0x81003000+i*16
                    memory[header+0x194+i*4]=pointer;memory[pointer+4]=other
            expected=replay(image,0x800214b8,{4:entry,5:candidate},memory.copy(),max_steps=4096)[2]
            for budget in (1,7,100):
                data=memory.copy();services=providers(data,[]);cursor=Cursor()
                assert lib.zone_transition_contains_begin(C.byref(cursor),entry,candidate,64,C.byref(services))==1
                assert lib.zone_transition_contains_step(C.byref(cursor),0)==1
                for step in range(100):
                    result=lib.zone_transition_contains_step(C.byref(cursor),budget)
                    if result!=1:break
                assert result==0 and cursor.contains==expected
                assert data==memory
        globals_=(0x80069210,0x80069214,0x80069218)
        for case in range(512):
            memory={entry+16:header,header+0x2bc:rng.getrandbits(32)}
            memory.update({address:rng.getrandbits(32) for address in globals_})
            expected=memory.copy();replay(image,0x80021530,{4:entry},expected)
            writes=[];services=providers(memory,writes)
            assert lib.zone_transition_enter(entry,C.byref(services))==0 and memory==expected
            if writes:assert [a for a,v in writes]==[globals_[0],globals_[2],globals_[1],header+0x2bc]
        # Signed nonpositive counts must skip even an invalid candidate pointer.
        for count in (0,0x80000000,0xffffffff):
            memory={entry+16:header,header+0x190:count};services=providers(memory,[]);cursor=Cursor()
            assert lib.zone_transition_contains_begin(C.byref(cursor),entry,0,64,C.byref(services))==1
            assert lib.zone_transition_contains_step(C.byref(cursor),10)==0 and not cursor.contains
        memory={entry+16:header,header+0x190:65};services=providers(memory,[]);cursor=Cursor()
        assert lib.zone_transition_contains_begin(C.byref(cursor),entry,candidate,64,C.byref(services))==1
        assert lib.zone_transition_contains_step(C.byref(cursor),10)==-71
        assert lib.zone_transition_contains_step(C.byref(cursor),10)==-71
        memory={entry+16:header,header+0x190:1,candidate+4:3,header+0x194:0}
        services=providers(memory,[]);cursor=Cursor()
        assert lib.zone_transition_contains_begin(C.byref(cursor),entry,candidate,64,C.byref(services))==1
        assert lib.zone_transition_contains_step(C.byref(cursor),10)==-70
        # A failed write retains original ordered partial side effects.
        memory={entry+16:header,header+0x2bc:0,**{a:9 for a in globals_}}
        writes=[];services=providers(memory,writes,globals_[1])
        assert lib.zone_transition_enter(entry,C.byref(services))==-99
        assert writes==[(globals_[0],0),(globals_[2],0)] and memory[header+0x2bc]==0
        # Runtime providers may modify flags while publishing engine globals;
        # the native final load must preserve those changes.
        memory={entry+16:header,header+0x2bc:0,**{a:9 for a in globals_}}
        services=providers(memory,[])
        @WRITE
        def mutate_flags(_,address,value):
            memory[address]=value
            if address==globals_[1]:memory[header+0x2bc]=0x80000004
            return 0
        services.write=mutate_flags
        assert lib.zone_transition_enter(entry,C.byref(services))==0
        assert memory[header+0x2bc]==0x80000005
        _ctypes.FreeLibrary(lib._handle)
    print('PASS zone transition: 768 native predicates across budgets, 512 entry initializers, signed counts and provider failure ownership')

if __name__=='__main__':main()
