"""Compare production hierarchy order with the original recursive walker.

Visit/deletion bodies are declared fixture providers in both executions. This
proves traversal and mutation ordering, not their unreconstructed engine bodies.
"""
import ctypes as ct
import copy
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
from test_gool_stack_native import replay,MASK

READ=ct.CFUNCTYPE(ct.c_int,ct.c_void_p,ct.c_uint32,ct.c_uint32,ct.POINTER(ct.c_uint32))
VISIT=ct.CFUNCTYPE(ct.c_int,ct.c_void_p,ct.c_uint32,ct.POINTER(ct.c_int32))
ERASE=ct.CFUNCTYPE(ct.c_int,ct.c_void_p,ct.c_uint32)
WRITE=ct.CFUNCTYPE(ct.c_int,ct.c_void_p,ct.c_uint32,ct.c_uint32,ct.c_uint32)

WRAPPER=r'''
#include "gool_tree.h"
__declspec(dllexport) int move_node(uint32_t node,uint32_t parent,
 int (*read)(void*,uint32_t,uint32_t,uint32_t*),
 int (*write)(void*,uint32_t,uint32_t,uint32_t),uint32_t limit){
 gool_tree_services services={.read=read,.write=write};
 return gool_tree_reparent(node,parent,&services,limit);
}
__declspec(dllexport) int walk(const uint32_t *roots,
 int (*read)(void*,uint32_t,uint32_t,uint32_t*),
 int (*visit)(void*,uint32_t,int32_t*),int (*erase)(void*,uint32_t),uint32_t budget){
 gool_tree tree={0};gool_tree_services services={.read=read,.visit=visit,.erase=erase};
 int result=gool_tree_begin(&tree,roots,&services);if(result!=GOOL_TREE_BUDGET)return result;
 if(!budget)return gool_tree_step(&tree,0);
 for(unsigned i=0;i<10000&&result==GOOL_TREE_BUDGET;i++)result=gool_tree_step(&tree,budget);
 if(result<0 && gool_tree_step(&tree,100)!=result)return -999;
 return result;
}
'''


class NativeHierarchy(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image=Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes())
        assert cls.image.sha256==ORIGINAL_SHA256
        cls.tmp=tempfile.TemporaryDirectory(prefix='crash-tree-')
        p=Path(cls.tmp.name);(p/'wrapper.c').write_text(WRAPPER)
        lib=p/'tree.dll'
        subprocess.run([sys.executable,'-m','ziglang','cc','-shared','-O3','-Wall','-Wextra','-Werror',
            '-I'+str(ROOT/'source'),str(p/'wrapper.c'),str(ROOT/'source/gool_tree.c'),'-o',str(lib)],check=True)
        cls.lib=ct.CDLL(str(lib));cls.lib.walk.argtypes=[ct.POINTER(ct.c_uint32),READ,VISIT,ERASE,ct.c_uint32]
        cls.lib.move_node.argtypes=[ct.c_uint32,ct.c_uint32,READ,WRITE,ct.c_uint32]

    @classmethod
    def tearDownClass(cls):
        if os.name=='nt':
            import _ctypes
            _ctypes.FreeLibrary(cls.lib._handle)
        cls.tmp.cleanup()

    def fixture(self,seed):
        rng=random.Random(seed)
        roots=[0x80068ea0+i*8 for i in range(8)]
        nodes=[0x80070000+i*0x200 for i in range(24)]
        ram={};children={root:[] for root in roots}
        for i,node in enumerate(nodes):
            ram[node]=rng.choice((0,1,1,1))
            ram[node+0x48]=0;ram[node+0x4c]=0
            parent=rng.choice(roots+nodes[:i]);children.setdefault(parent,[]).append(node)
            ram[node+0x44]=parent
        for root in roots:ram[root]=2;ram[root+4]=0
        for parent,kids in children.items():
            ram[parent+(4 if parent in roots else 0x4c)]=kids[0] if kids else 0
            for i,node in enumerate(kids):ram[node+0x48]=kids[i+1] if i+1<len(kids) else 0
        results={node:rng.choice((0,0,0,-25,-10,7)) for node in nodes}
        # A visited node may change its child pointer; the walker must read it
        # after visit. Only clear edges here to preserve an acyclic fixture.
        mutations={node:(node+0x4c,0) for node in nodes if rng.randrange(4)==0}
        # Its own sibling may change, but caller's captured sibling wins.
        mutations.update({node:(node+0x48,0) for node in nodes if rng.randrange(4)==0})
        return roots,ram,results,mutations

    def original(self,roots,ram,results,mutations):
        log=[]
        def visit(regs,memory):
            node=regs[4];log.append(('visit',node))
            if node in mutations:memory[mutations[node][0]]=mutations[node][1]
            regs[2]=results.get(node,0)&MASK
        def erase(regs,memory):
            node=regs[4];log.append(('erase',node));memory[node]=0;memory[node+0x4c]=0
        replay(self.image,0x80019a38,{4:0x80019900,5:0x80100000,6:0,29:0x801f0000},ram,
               hooks={0x80100000:visit,0x8001a844:erase},max_steps=20000)
        return log

    def production(self,roots,ram,results,mutations,budget=1,fail_read=None,fail_visit=None,fail_erase=None):
        log=[]
        @READ
        def read(_,node,offset,out):
            address=node+offset
            if address==fail_read or address not in ram:return -700
            out[0]=ram[address];return 0
        @VISIT
        def visit(_,node,out):
            if node==fail_visit:return -701
            log.append(('visit',node))
            if node in mutations:ram[mutations[node][0]]=mutations[node][1]
            out[0]=results.get(node,0);return 0
        @ERASE
        def erase(_,node):
            if node==fail_erase:return -702
            log.append(('erase',node));ram[node]=0;ram[node+0x4c]=0;return 0
        result=self.lib.walk((ct.c_uint32*8)(*roots),read,visit,erase,budget)
        return result,log

    def test_256_original_mutating_hierarchies_under_budget_continuation(self):
        for seed in range(256):
            roots,ram,results,mutations=self.fixture(seed)
            original=copy.deepcopy(ram);expected=self.original(roots,original,results,mutations)
            for budget in (1,7,1000):
                production=copy.deepcopy(ram)
                result,log=self.production(roots,production,results,mutations,budget)
                self.assertEqual(result,0,seed);self.assertEqual(log,expected,seed)
                self.assertEqual(production,{k:original[k] for k in production},seed)

    def test_root_type_zero_sibling_and_new_child_order(self):
        roots,ram,_,_=self.fixture(123)
        a,b,c,d=[0x80080000+i*0x200 for i in range(4)]
        for root in roots:ram[root]=2;ram[root+4]=0
        ram[roots[0]+4]=a
        for node in (a,b,c,d):ram.update({node:1,node+0x48:0,node+0x4c:0})
        ram[a]=0;ram[a+0x48]=b;ram[b+0x4c]=c;ram[c]=0;ram[c+0x48]=d
        mutations={a:(a+0x48,0)}
        expected=self.original(roots,copy.deepcopy(ram),{},mutations)
        self.assertEqual(expected,[('visit',a),('visit',b)])
        self.assertEqual(self.production(roots,ram,{},mutations),(0,expected))

    def test_provider_failures_cycles_and_depth_limit(self):
        roots,ram,_,_=self.fixture(0)
        a=0x80080000;ram[roots[0]+4]=a
        ram.update({a:1,a+0x48:0,a+0x4c:0})
        self.assertEqual(self.production(roots,copy.deepcopy(ram),{},{},fail_read=roots[0])[0],-700)
        self.assertEqual(self.production(roots,copy.deepcopy(ram),{},{},fail_visit=a)[0],-701)
        self.assertEqual(self.production(roots,copy.deepcopy(ram),{a:-25},{},fail_erase=a)[0],-702)
        for root in roots:ram[root]=2;ram[root+4]=0
        ram[roots[0]+4]=a;ram[a]=1;ram[a+0x48]=0;ram[a+0x4c]=a
        self.assertEqual(self.production(roots,copy.deepcopy(ram),{},{})[0],-3)
        for i in range(65):
            node=a+i*0x200;ram.update({node:1,node+0x48:0,node+0x4c:node+0x200 if i<64 else 0})
        self.assertEqual(self.production(roots,ram,{},{})[0],-2)
        self.assertEqual(self.production(roots,ram,{},{},budget=0),(1,[]))

    def test_retained_instruction_windows(self):
        evidence=json.loads((ROOT/'tests/gool_tree_native_evidence.json').read_text())
        self.assertEqual(evidence['source_sha256'],self.image.sha256)
        for w in evidence['windows'].values():
            self.assertEqual(w['raw_hex'],self.image.read(w['start'],w['end']-w['start']).hex())

    def test_512_native_reparent_operations_and_bounded_cycle(self):
        for seed in range(256):
            roots,ram,_,_=self.fixture(seed)
            rng=random.Random(seed)
            nodes=[0x80070000+i*0x200 for i in range(24)]
            # Destination roots may have either root-sentinel or object layout.
            for root in roots:
                if rng.randrange(2):ram[root]=1;ram[root+0x4c]=ram[root+4]
            for detached in (False,True):
                original=copy.deepcopy(ram);node=rng.choice(nodes);parent=rng.choice(roots)
                if detached:original[node+0x44]=0
                production=copy.deepcopy(original)
                result=replay(self.image,0x8001962c,{4:node,5:parent},original)[2]
                @READ
                def read(_,obj,offset,out):
                    if obj+offset not in production:return -700
                    out[0]=production[obj+offset];return 0
                @WRITE
                def write(_,obj,offset,value):production[obj+offset]=value;return 0
                self.assertEqual(self.lib.move_node(node,parent,read,write,100),result)
                self.assertEqual(production,original)
        # A corrupt sibling cycle is bounded before any link changes.
        node,parent,old,head=1,2,3,4
        ram={node+0x44:old,old:2,old+4:head,head+0x48:head}
        @READ
        def read(_,obj,offset,out):
            if obj+offset not in ram:return -700
            out[0]=ram[obj+offset];return 0
        @WRITE
        def reject_write(_,obj,offset,value):return -999
        self.assertEqual(self.lib.move_node(node,parent,read,reject_write,10),-2)
        ram[head+0x48]=0
        self.assertEqual(self.lib.move_node(node,parent,read,reject_write,10),-24)


if __name__=='__main__':unittest.main()
