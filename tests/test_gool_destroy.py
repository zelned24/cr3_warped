"""Compare entity teardown control flow and metadata with original MIPS.

Notification and audio bodies are explicit fixture hooks in both runs. They
are not credited as full signal/audio integration or physical DMA validation.
"""
import copy
import ctypes as ct
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import cb3
from psx_native import Image,ORIGINAL_SHA256
from test_gool_stack_native import replay
from test_gool_tree import READ,WRITE,VISIT

NOTIFY=ct.CFUNCTYPE(ct.c_int,ct.c_void_p,ct.c_uint32,ct.c_uint32)
STOP=ct.CFUNCTYPE(ct.c_int,ct.c_void_p,ct.c_uint32)
WRAPPER=r'''
#include "gool_destroy.h"
#include "gool_forest.h"
__declspec(dllexport) int forest(const uint32_t *roots,uint32_t free_root,
 uint32_t *protected_object,uint32_t *detached_object,uint32_t *usage,
 int (*read)(void*,uint32_t,uint32_t,uint32_t*),
 int (*write)(void*,uint32_t,uint32_t,uint32_t),
 int (*visit)(void*,uint32_t,int32_t*),
 int (*signal)(void*,uint32_t,uint32_t),int (*audio)(void*,uint32_t),uint32_t budget){
 gool_forest f={0};gool_destroy_services s={.links={.read=read,.write=write,.visit=visit},
  .protected_object=protected_object,.detached_object=detached_object,.usage=usage,
  .usage_count=64,.free_root=free_root,.scan_limit=128,.notify=signal,.stop_audio=audio};
 int result=gool_forest_begin(&f,roots,&s);
 if(result!=1)return result;
 for(unsigned i=0;i<10000&&result==1;i++)result=gool_forest_step(&f,budget);
 return result;
}
__declspec(dllexport) int destroy(uint32_t object,uint32_t notify,uint32_t free_root,
 uint32_t *protected_object,uint32_t *detached_object,uint32_t *usage,
 int (*read)(void*,uint32_t,uint32_t,uint32_t*),
 int (*write)(void*,uint32_t,uint32_t,uint32_t),
 int (*signal)(void*,uint32_t,uint32_t),int (*audio)(void*,uint32_t),uint32_t budget){
 gool_destroy cursor={0};gool_destroy_services s={.links={.read=read,.write=write},
  .protected_object=protected_object,.detached_object=detached_object,.usage=usage,
  .usage_count=64,.free_root=free_root,.scan_limit=128,.notify=signal,.stop_audio=audio};
 int result=gool_destroy_begin(&cursor,object,notify,&s);
 if(result!=1)return result;
 if(!budget)return gool_destroy_step(&cursor,0);
 for(unsigned i=0;i<10000&&result==1;i++)result=gool_destroy_step(&cursor,budget);
 if(result<0&&gool_destroy_step(&cursor,100)!=result)return -999;
 return result;
}
'''
GP=0x80060878 # Original startup 0x80048a74..0x80048a78.
PROTECTED=GP+0x218
DETACHED=0x80068e98
USAGE=0x8006b688
FREE=0x80068ee0


class NativeDestroy(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image=Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes())
        assert cls.image.sha256==ORIGINAL_SHA256
        cls.tmp=tempfile.TemporaryDirectory(prefix='crash-destroy-')
        folder=Path(cls.tmp.name);(folder/'wrapper.c').write_text(WRAPPER)
        lib=folder/'destroy.dll'
        subprocess.run([sys.executable,'-m','ziglang','cc','-shared','-O3','-Wall','-Wextra','-Werror',
            '-I'+str(ROOT/'source'),str(folder/'wrapper.c'),str(ROOT/'source/gool_destroy.c'),
            str(ROOT/'source/gool_tree.c'),str(ROOT/'source/gool_forest.c'),'-o',str(lib)],check=True)
        cls.lib=ct.CDLL(str(lib))
        cls.lib.destroy.argtypes=[ct.c_uint32]*3+[ct.POINTER(ct.c_uint32)]*3+[READ,WRITE,NOTIFY,STOP,ct.c_uint32]
        cls.lib.forest.argtypes=[ct.POINTER(ct.c_uint32),ct.c_uint32]+[ct.POINTER(ct.c_uint32)]*3+[READ,WRITE,VISIT,NOTIFY,STOP,ct.c_uint32]

    @classmethod
    def tearDownClass(cls):
        if os.name=='nt':
            import _ctypes
            _ctypes.FreeLibrary(cls.lib._handle)
        cls.tmp.cleanup()

    def fixture(self,seed):
        rng=random.Random(seed);root=0x80068ea0
        nodes=[0x80070000+i*0x400 for i in range(16)]
        ram={root:2,root+4:nodes[0],FREE:2,FREE+4:0,PROTECTED:0,DETACHED:0x80080000}
        for offset in (0x44,0x48,0x4c):ram[0x80080000+offset]=0x12345678
        children={n:[] for n in nodes}
        for i,node in enumerate(nodes):
            parent=root if i==0 else rng.choice(nodes[:i])
            ram.update({node:1,node+0x44:parent,node+0x48:0,node+0x4c:0,node+0xb8:i*256+rng.randrange(256)})
            if i:children[parent].append(node)
        for node,kids in children.items():
            ram[node+0x4c]=kids[0] if kids else 0
            for i,kid in enumerate(kids):ram[kid+0x48]=kids[i+1] if i+1<len(kids) else 0
        for i in range(64):ram[USAGE+i*4]=rng.getrandbits(32)
        if seed%7==0:ram[PROTECTED]=rng.choice(nodes)
        if seed%3==0:ram[FREE]=1;ram[FREE+0x4c]=0
        return ram,nodes

    def mutation(self,node,ram,target,when,protected=None):
        if node==target:
            if protected is None:ram[PROTECTED]=node
            else:protected.value=node

    def original(self,ram,node,notify,target,when):
        log=[]
        def signal(regs,memory):
            self.assertEqual((regs[4],regs[6],regs[7]),(0,0x1a00,0))
            obj=regs[5];log.append(('signal',obj))
            if when=='signal':self.mutation(obj,memory,target,when)
        def audio(regs,memory):
            obj=regs[4];log.append(('audio',obj))
            if when=='audio':self.mutation(obj,memory,target,when)
        regs=replay(self.image,0x8001a844,{4:node,5:notify,28:GP,29:0x801f0000},ram,
            hooks={0x8001db38:signal,0x800334d0:audio},max_steps=30000)
        self.assertEqual(ct.c_int32(regs[2]).value,-255)
        return log

    def production(self,ram,node,notify,target,when,budget=1,fail=None,defer=None,roots=None,visit_results=None):
        log=[];protected=ct.c_uint32(ram[PROTECTED]);detached=ct.c_uint32(ram[DETACHED])
        usage=(ct.c_uint32*64)(*[ram[USAGE+i*4] for i in range(64)])
        pending_signal=set();pending_audio=set()
        pending_visit=set()
        @READ
        def read(_,obj,offset,out):
            if obj+offset not in ram:return -700
            out[0]=ram[obj+offset];return 0
        @WRITE
        def write(_,obj,offset,value):
            if fail=='write':return -701
            ram[obj+offset]=value;return 0
        @NOTIFY
        def signal(_,obj,value):
            if value!=0x1a00:return -999
            if fail=='signal':return -702
            if obj in pending_signal:pending_signal.remove(obj);return 0
            log.append(('signal',obj))
            if when=='signal':self.mutation(obj,ram,target,when,protected)
            if defer=='signal':pending_signal.add(obj);return 2
            return 0
        @STOP
        def audio(_,obj):
            if fail=='audio':return -703
            if obj in pending_audio:pending_audio.remove(obj);return 0
            log.append(('audio',obj))
            if when=='audio':self.mutation(obj,ram,target,when,protected)
            if defer=='audio':pending_audio.add(obj);return 2
            return 0
        @VISIT
        def visit(_,obj,out):
            if obj in pending_visit:
                pending_visit.remove(obj);out[0]=visit_results.get(obj,0);return 0
            log.append(('visit',obj))
            if defer=='visit':pending_visit.add(obj);return 2
            out[0]=visit_results.get(obj,0);return 0
        if roots is None:
            result=self.lib.destroy(node,notify,FREE,ct.byref(protected),ct.byref(detached),usage,
                                   read,write,signal,audio,budget)
        else:
            result=self.lib.forest((ct.c_uint32*8)(*roots),FREE,ct.byref(protected),ct.byref(detached),usage,
                                  read,write,visit,signal,audio,budget)
        ram[PROTECTED]=protected.value;ram[DETACHED]=detached.value
        for i in range(64):ram[USAGE+i*4]=usage[i]
        return result,log

    def test_384_original_subtree_teardowns_with_mutable_protected_slot(self):
        for seed in range(128):
            ram,nodes=self.fixture(seed)
            target=nodes[seed%len(nodes)]
            for when in ('none','signal','audio'):
                original=copy.deepcopy(ram)
                expected=self.original(original,nodes[0],seed%2,target,when)
                for budget in (1,7,1000):
                    production=copy.deepcopy(ram)
                    status,log=self.production(production,nodes[0],seed%2,target,when,budget)
                    self.assertEqual(status,0,(seed,when))
                    self.assertEqual(log,expected,(seed,when))
                    self.assertEqual(production,{k:original[k] for k in production},(seed,when))

    def test_guard_budget_failures_and_invalid_usage(self):
        ram,nodes=self.fixture(1)
        self.assertEqual(self.production(copy.deepcopy(ram),0,1,0,'none'),(0,[]))
        ram[PROTECTED]=nodes[0]
        self.assertEqual(self.production(copy.deepcopy(ram),nodes[0],1,0,'none'),(0,[]))
        ram[PROTECTED]=0
        self.assertEqual(self.production(copy.deepcopy(ram),nodes[0],1,0,'none',budget=0),(1,[]))
        for key,expected in (('signal',-702),('audio',-703),('write',-701)):
            self.assertEqual(self.production(copy.deepcopy(ram),nodes[0],1,0,'none',fail=key)[0],expected)
        node=nodes[-1];ram[node+0x4c]=0;ram[node+0xb8]=0xffffffff
        before=copy.deepcopy(ram)
        self.assertEqual(self.production(ram,node,0,0,'none')[0],-4)
        self.assertEqual(ram,before)

    def test_original_instruction_evidence(self):
        evidence=json.loads((ROOT/'tests/gool_destroy_native_evidence.json').read_text())
        self.assertEqual(evidence['source_sha256'],self.image.sha256)
        for w in evidence['windows'].values():
            self.assertEqual(w['raw_hex'],self.image.read(w['start'],w['end']-w['start']).hex())
        self.assertEqual(self.image.word(0x80048a74),0x3c1c8006)
        self.assertEqual(self.image.word(0x80048a78),0x279c0878)

    def test_pending_services_resume_without_repeating_guard_or_effects(self):
        for defer in ('signal','audio'):
            ram,nodes=self.fixture(1);original=copy.deepcopy(ram)
            expected=self.original(original,nodes[0],1,nodes[0],defer)
            status,log=self.production(ram,nodes[0],1,nodes[0],defer,budget=1000,defer=defer)
            self.assertEqual(status,0);self.assertEqual(log,expected)
            self.assertEqual(ram,{k:original[k] for k in ram})

    def test_actual_forest_and_deletion_composition_matches_native(self):
        for seed in range(96):
            ram,nodes=self.fixture(seed);roots=[0x80068ea0+i*8 for i in range(8)]
            for root in roots[1:]:ram[root]=2;ram[root+4]=0
            rng=random.Random(seed)
            returns={node:rng.choice((-25,-25,0,0,-10,7)) for node in nodes}
            original=copy.deepcopy(ram);expected=[]
            def visit(regs,memory):
                expected.append(('visit',regs[4]));regs[2]=returns.get(regs[4],0)&0xffffffff
            def audio(regs,memory):expected.append(('audio',regs[4]))
            replay(self.image,0x80019a38,{4:0x80019900,5:0x80100000,6:0,28:GP,29:0x801f0000},original,
                hooks={0x80100000:visit,0x800334d0:audio},max_steps=30000)
            for budget,defer in ((1,None),(7,'audio'),(1000,'visit')):
                production=copy.deepcopy(ram)
                result,log=self.production(production,0,0,0,'none',budget=budget,defer=defer,
                                           roots=roots,visit_results=returns)
                self.assertEqual(result,0,(seed,budget,defer));self.assertEqual(log,expected,(seed,budget,defer))
                self.assertEqual(production,{k:original[k] for k in production},(seed,budget,defer))

    def test_reserved_object_is_the_retiring_node(self):
        for when in ('signal','audio'):
            ram,nodes=self.fixture(1);ram[DETACHED]=nodes[0]
            original=copy.deepcopy(ram)
            expected=self.original(original,nodes[0],1,nodes[0],when)
            status,log=self.production(ram,nodes[0],1,nodes[0],when,budget=1,defer=when)
            self.assertEqual(status,0);self.assertEqual(log,expected)
            self.assertEqual(ram,{k:original[k] for k in ram})
            self.assertEqual([ram[nodes[0]+offset] for offset in (0x44,0x48,0x4c)],[0,0,0])


if __name__=='__main__':unittest.main()
