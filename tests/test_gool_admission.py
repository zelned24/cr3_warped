"""Original creation admission control flow versus production C providers."""
import _ctypes
import ctypes as C
from pathlib import Path
import random
import struct
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
SCALAR=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_uint32,C.c_uint32,C.POINTER(C.c_uint32))
PATH=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_uint32,C.POINTER(C.c_uint32))
class Services(C.Structure):
    _fields_=[('context',C.c_void_p),('read',READ),('write',WRITE),('scalar',SCALAR),('path',PATH),
              ('protected_address',C.c_uint32),('usage_address',C.c_uint32),('usage_count',C.c_uint32)]
class Result(C.Structure):
    _fields_=[('database',C.c_uint32),('previous_chain',C.c_uint32),('protected_slot',C.c_uint32),
              ('admitted',C.c_int),('native_result',C.c_int32)]

def main():
    image=Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes());assert image.sha256==ORIGINAL_SHA256
    rng=random.Random(196244)
    entry,header,db=0x81000000,0x81001000,0x81002000
    current,previous,old_db=0x81003000,0x81004000,0x81005000
    protected,usage=0x80060a90,0x8006b688
    head,height,source=0x80069210,0x80069214,0x80069218
    with tempfile.TemporaryDirectory(prefix='crash-admission-') as directory:
        dll=Path(directory)/'admission.dll'
        subprocess.run([sys.executable,'-m','ziglang','cc','-shared','-O3','-Wall','-Wextra',
                        '-Werror',str(ROOT/'source/gool_admission.c'),'-o',str(dll)],check=True)
        lib=C.CDLL(str(dll));lib.gool_admission.argtypes=[C.c_uint32,C.c_uint32,C.POINTER(Services),C.POINTER(Result)]
        branches=set()
        for case in range(1024):
            entity=case%9;file=(0,2,34,35)[case%4];lookup=rng.randrange(16)
            keys={0xa9:file,0x9f:lookup,0x342:case%3}
            memory={entry+16:header,header+0x184:2,header+0x188:3,
                    entry+16+(5+entity)*4:db,protected:0 if case%5 else 0x81200000,
                    head:0x81201000,height:rng.getrandbits(32),source:old_db if case%3 else 0}
            memory.update({usage+i*4:rng.getrandbits(32)&~3 for i in range(16)})
            memory[usage+lookup*4]|=rng.randrange(4)
            origin=tuple(rng.randint(-20000,20000) for _ in range(3))
            point=tuple(origin[i]+((0,400,0)[i] if case%5 else 100) for i in range(3))
            for address,vector in ((current,point),(previous,origin)):
                raw=struct.pack('<3h',*vector)+bytes(2)
                for offset in (0,4):memory[address+offset]=struct.unpack_from('<I',raw,offset)[0]
            native=memory.copy();events=[]
            def scalar_hook(regs,ram):
                assert regs[4]==db and regs[6]==regs[7]==0
                assert ram[regs[29]+16]==0 and ram[regs[29]+20]==2 and ram[regs[29]+24]==0
                events.append(('scalar',regs[4],regs[5]));regs[2]=int(keys[regs[5]])
            def path_hook(regs,ram):
                assert regs[5]==0x4b and regs[6]==regs[7]==0
                events.append(('path',regs[4]));regs[2]=current if regs[4]==db else previous
                if case%7==0 and regs[4]==old_db:ram[source]=0
            regs=replay(image,0x80019ce4,{4:entry,5:entity,28:0x80060878,29:0x810f0000},native,
                        stops=(0x8001a084,0x8001a400),hooks={0x80032df8:scalar_hook,0x80032710:path_hook},max_steps=2048)
            # At the allocation boundary key342 is even and occupancy admitted;
            # all early returns carry native -14. Preserve native S4/S5 outputs.
            admitted=regs[2]!=0xfffffff2
            expected=(db,regs[21] if file==34 else 0,regs[20],int(admitted),0 if admitted else -14)
            data=memory.copy();actual_events=[]
            @READ
            def read(_,address,out):
                if address not in data:return -99
                out[0]=data[address];return 0
            @WRITE
            def write(_,address,value):
                if address not in data:return -99
                data[address]=value;return 0
            @SCALAR
            def scalar(_,database,key,out):
                actual_events.append(('scalar',database,key))
                if database!=db or key not in keys:return -99
                out[0]=int(keys[key]);return 0
            @PATH
            def path(_,database,out):
                actual_events.append(('path',database));out[0]=current if database==db else previous
                if case%7==0 and database==old_db:data[source]=0
                return 0
            services=Services(None,read,write,scalar,path,protected,usage,16);result=Result()
            assert lib.gool_admission(entry,entity,C.byref(services),C.byref(result))==0
            actual=(result.database,result.previous_chain,result.protected_slot,result.admitted,result.native_result)
            assert actual==expected,(case,actual,expected)
            assert actual_events==events,(case,actual_events,events)
            assert all(data[address]==native[address] for address in memory),case
            branches.add((file,result.protected_slot,result.admitted))
        # A bad occupancy index cannot address adjacent engine memory; failure
        # preserves caller output, while earlier native writes remain visible.
        keys={0xa9:2,0x9f:0xffffffff,0x342:0};result=Result(1,2,3,4,5)
        before=bytes(result)
        assert lib.gool_admission(entry,entity,C.byref(services),C.byref(result))==-81
        assert bytes(result)==before
        # Null creation rejects without touching an entry or any provider.
        assert lib.gool_admission(0,0,C.byref(services),C.byref(result))==0
        assert not result.admitted and result.native_result==-14 and not result.database
        _ctypes.FreeLibrary(lib._handle)
    print(f'PASS creation admission: 1024 original MIPS traces, {len(branches)} branch classes, ordered DB calls/global writes and bounded occupancy')

if __name__=='__main__':main()
