"""Compare native initializer writes and service boundaries with original MIPS."""
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
from test_gool_admission import READ,WRITE

GEOMETRY=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_uint32,C.POINTER(C.c_uint32))
LOAD=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_uint32,C.POINTER(C.c_uint32))
STATE=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_uint32,C.c_uint32,C.c_uint32,C.c_uint32,C.POINTER(C.c_uint32))
class Services(C.Structure):
    _fields_=[('context',C.c_void_p),('read',READ),('write',WRITE),('geometry',GEOMETRY),('load',LOAD),('enter_state',STATE)]
class Request(C.Structure):
    _fields_=[(n,C.c_uint32) for n in ('node','class_index','object_id','argument_count','arguments','placed')]

def main():
    image=Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes());assert image.sha256==ORIGINAL_SHA256
    rng=random.Random(165200)
    node,parent,behavior,header,indices,nsd,db=tuple(0x81200000+i*0x10000 for i in range(7))
    with tempfile.TemporaryDirectory(prefix='crash-initialize-') as folder:
        dll=Path(folder)/'initialize.dll'
        subprocess.run([sys.executable,'-m','ziglang','cc','-shared','-O3','-Wall','-Wextra',
                        '-Werror',str(ROOT/'source/gool_initialize.c'),'-o',str(dll)],check=True)
        lib=C.CDLL(str(dll));lib.gool_initialize_native.argtypes=[C.POINTER(Request),C.POINTER(Services),C.POINTER(C.c_int32)]
        outcomes=set()
        for case in range(1536):
            class_index=(0,1,4,5,6,29,34,127,128)[case%9]
            object_id=rng.getrandbits(32) if case%7==0 else rng.randrange(256)
            count=rng.randrange(9);arguments=0x81800000;placed=case%3
            geometry=rng.getrandbits(32);state=(255,65535,0,7)[case%4]
            load_result=behavior if case%5 else (0,0xffffffff,0xfffffff2,0xffffff02)[case%4]
            state_result=(0xffffff01,0xffffff00,0xfffffff2,0)[(case//4)%4]
            magic=case%13==0;parent_self=case%17==0
            memory={node+offset:rng.getrandbits(32) for offset in range(0,0x140,4)}
            memory.update({parent+offset:rng.getrandbits(32) for offset in range(0,0x84,4)})
            memory.update({node+4:3 if case%11 else 2,node+0x44:node if parent_self else parent,
                           node+0xf0:db,parent:1 if case%2 else 2,
                           0x80061044:nsd,0x80068e98:0x81700000,
                           nsd+16+class_index*4:0x6396347f if magic else 0x213579bd,
                           behavior+16:header,header+16:6,behavior+0x1c:indices})
            state_address=indices+(6+(object_id&65535))*2
            memory[state_address&~3]=state<<((state_address&2)*8)
            initial=dict(memory);native_writes=[];events=[]
            class NativeMemory(dict):
                def __setitem__(self,key,value):
                    if key in initial:native_writes.append((key,value&0xffffffff))
                    super().__setitem__(key,value&0xffffffff)
            native=NativeMemory(memory)
            native[0x810f0010]=arguments;native[0x810f0014]=placed
            def scalar_hook(regs,ram):
                assert regs[4]==db and regs[5]==0x47 and regs[6]==regs[7]==0
                assert ram[regs[29]+16]==0 and ram[regs[29]+20]==2 and ram[regs[29]+24]==0
                events.append(('geometry',regs[4]));regs[2]=geometry
            def load_hook(regs,ram):
                assert regs[4]==nsd+16+class_index*4 and regs[5]==1 and regs[6]==0
                events.append(('load',regs[4]));ram[regs[4]]=load_result;regs[2]=load_result
            def state_hook(regs,ram):
                events.append(('state',regs[4],regs[5],regs[6],regs[7]))
                ram[node+0x100]=0xa5a50000|case;regs[2]=state_result
            regs=replay(image,0x8001a5c8,{4:node,5:class_index,6:object_id,7:count,29:0x810f0000},
                        native,hooks={0x80032df8:scalar_hook,0x800142b4:load_hook,0x8001d2d4:state_hook},max_steps=1024)
            data=dict(initial);writes=[];actual_events=[]
            @READ
            def read(_,address,out):
                if address not in data:return -99
                out[0]=data[address];return 0
            @WRITE
            def write(_,address,value):
                if address not in data:return -99
                writes.append((address,value));data[address]=value;return 0
            @GEOMETRY
            def geometry_service(_,database,out):actual_events.append(('geometry',database));out[0]=geometry;return 0
            @LOAD
            def load(_,address,out):
                actual_events.append(('load',address));writes.append((address,load_result));data[address]=load_result
                out[0]=load_result;return 0
            @STATE
            def enter(_,object_,state_,argc,args,out):
                actual_events.append(('state',object_,state_,argc,args))
                value=0xa5a50000|case;data[node+0x100]=value;writes.append((node+0x100,value));out[0]=state_result;return 0
            services=Services(None,read,write,geometry_service,load,enter)
            request=Request(node,class_index,object_id,count,arguments,placed);result=C.c_int32(12345)
            assert lib.gool_initialize_native(C.byref(request),C.byref(services),C.byref(result))==0,case
            assert result.value==(regs[2] if regs[2]<0x80000000 else regs[2]-0x100000000),case
            assert actual_events==events,(case,actual_events,events)
            assert writes==native_writes,(case,writes,native_writes)
            assert all(data[a]==native[a] for a in initial),case
            outcomes.add(result.value)
        # Provider failure is not a successful native initialization; preceding
        # writes persist, output is preserved, and no later phase is executed.
        data=dict(initial);data[node+0x44]=0;writes=[];actual_events=[]
        request=Request(node,34,2,0,0,0);result=C.c_int32(12345)
        assert lib.gool_initialize_native(C.byref(request),C.byref(services),C.byref(result))==-84
        assert result.value==12345 and writes and not actual_events
        # Invalid argument domain fails before any object writes.
        writes.clear();request.node=0xfffffffc
        assert lib.gool_initialize_native(C.byref(request),C.byref(services),C.byref(result))==-84
        assert not writes
        _ctypes.FreeLibrary(lib._handle)
    assert outcomes=={-14,-27,-255}
    print('PASS initializer:1536 original MIPS traces; exact object/provider writes, placed/child paths, parent alias, behavior failures and state returns')

if __name__=='__main__':main()
