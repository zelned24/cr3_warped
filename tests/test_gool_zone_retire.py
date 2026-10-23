"""Compare zone-retirement traversal with original SCUS_942.44 MIPS.

Signal and retirement services are explicit fixture hooks in both executions.
Async/cancel tests check cursor ownership; actual actor/audio teardown and
hardware quiescence remain outside the fixture providers.
"""
import copy
import ctypes as ct
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import cb3
from psx_native import Image, ORIGINAL_SHA256
from test_gool_stack_native import replay

ROOTS = 0x80068ea0
GLOBAL = 0x80065d60
NODES = 0x80070000
ENTRY = 0x81000000
SIGNAL = ct.CFUNCTYPE(ct.c_int, ct.c_void_p, ct.c_uint32, ct.c_uint32)
RETIRE = ct.CFUNCTYPE(ct.c_int, ct.c_void_p, ct.c_uint32, ct.c_uint32, ct.POINTER(ct.c_int32))
CANCEL = ct.CFUNCTYPE(ct.c_int, ct.c_void_p, ct.c_uint32, ct.c_int)
WRAPPER = r'''
#include "gool_zone_retire.h"
static gool_domain domain;
static gool_zone_retire owner;
static gool_domain_lease leases[3];
static unsigned mapped;
__declspec(dllexport) int start(uint32_t *roots,uint32_t *global,uint32_t *nodes,uint32_t limit,
 int(*signal)(void*,uint32_t,uint32_t),int(*retire)(void*,uint32_t,uint32_t,int32_t*),
 int(*cancel)(void*,uint32_t,int)) {
 if(mapped||owner.opened)return -900;
 int e=gool_domain_map_mutable(&domain,0x80068ea0,roots,64,&leases[mapped]);if(e)return e;mapped++;
 e=gool_domain_map_mutable(&domain,0x80065d60,global,4,&leases[mapped]);if(e)return e;mapped++;
 e=gool_domain_map_mutable(&domain,0x80070000,nodes,0x8000,&leases[mapped]);if(e)return e;mapped++;
 gool_zone_retire_services s={.domain=&domain,.max_nodes=limit,.signal=signal,.retire=retire,.cancel=cancel};
 return gool_zone_retire_begin(&owner,0x81000000,&s);
}
__declspec(dllexport) int step(uint32_t budget){return gool_zone_retire_step(&owner,budget);}
__declspec(dllexport) int close_owner(void){return gool_zone_retire_close(&owner);}
__declspec(dllexport) int opened(void){return owner.opened;}
__declspec(dllexport) int active(void){return owner.callback_active;}
__declspec(dllexport) int release_maps(void){
 if(owner.opened)return -901;
 while(mapped){int e=gool_domain_release(&domain,leases[mapped-1]);if(e)return e;mapped--;}
 return gool_domain_empty(&domain)?0:-902;
}
'''


class ZoneRetirement(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = Image.parse((Path(cb3.ISO) / "SCUS_942.44").read_bytes())
        if cls.image.sha256 != ORIGINAL_SHA256:
            raise RuntimeError("requires audited SCUS_942.44 image")
        cls.temporary = tempfile.TemporaryDirectory(prefix="crash-zone-retire-")
        folder = Path(cls.temporary.name)
        (folder / "wrapper.c").write_text(WRAPPER)
        library = folder / "retire.dll"
        subprocess.run([sys.executable, "-m", "ziglang", "cc", "-shared", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-I" + str(ROOT / "source"), str(folder / "wrapper.c"),
                        str(ROOT / "source/gool_zone_retire.c"), str(ROOT / "source/gool_domain.c"),
                        "-o", str(library)], check=True)
        cls.lib = ct.CDLL(str(library))
        word_pointer = ct.POINTER(ct.c_uint32)
        cls.lib.start.argtypes = [word_pointer, word_pointer, word_pointer, ct.c_uint32, SIGNAL, RETIRE, CANCEL]
        cls.lib.step.argtypes = [ct.c_uint32]

    @classmethod
    def tearDownClass(cls):
        if os.name == "nt":
            import _ctypes
            _ctypes.FreeLibrary(cls.lib._handle)
        cls.temporary.cleanup()

    def fixture(self, seed, count=24):
        rng = random.Random(seed)
        roots = [ROOTS + i * 8 for i in range(8)]
        nodes = [NODES + i * 0x200 for i in range(count)]
        ram = {GLOBAL: rng.choice((0, 0xffffffff))}
        for root in roots:
            ram[root] = 2
            ram[root + 4] = 0
        children = {root: [] for root in roots}
        for i, node in enumerate(nodes):
            ram.update({node + offset: 0 for offset in range(0, 0x200, 4)})
            ram[node] = rng.choice((0, 1, 1, 1))
            ram[node + 0x18] = rng.choice((ENTRY, ENTRY, ENTRY + 0x200))
            ram[node + 0xb8] = rng.choice((0, 0, 0, 1))
            ram[node + 0xac] = rng.choice((0, 0, 0, 0x01000000))
            ram[node + 0x100] = rng.choice((0, 0, 0, 0x10000))
            parent = rng.choice(roots + nodes[:i])
            children.setdefault(parent, []).append(node)
        for parent, kids in children.items():
            ram[parent + (4 if parent in roots else 0x4c)] = kids[0] if kids else 0
            for i, node in enumerate(kids):
                ram[node + 0x48] = kids[i + 1] if i + 1 < len(kids) else 0
        returns = {node: rng.choice((0, 0, -25, -255, 7)) for node in nodes}
        moves = {node for node in nodes if rng.randrange(3) == 0}
        return ram, returns, moves

    def original(self, ram, returns, moves):
        log = []
        def signal(regs, memory):
            self.assertEqual((regs[4], regs[6], regs[7]), (0, 0x1a00, 0))
            node = regs[5]
            log.append(("signal", node))
            if node in moves:
                memory[node + 0x18] = ENTRY + 0x200
            # A mutation of own sibling must not discard caller's snapshot.
            memory[node + 0x48] = 0
        def retire(regs, memory):
            self.assertEqual(regs[5], 0)
            node = regs[4]
            log.append(("retire", node))
            memory[node] = 0
            regs[2] = returns.get(node, 0) & 0xffffffff
        replay(self.image, 0x8001aa98, {4: ENTRY, 29: 0x801f0000}, ram,
               hooks={0x8001db38: signal, 0x8001a844: retire}, max_steps=30000)
        return log

    def begin(self, ram, returns=None, moves=(), defer=(), fail=None, limit=1000):
        returns = returns or {}
        self.buffers = [(ct.c_uint32 * 16)(), (ct.c_uint32 * 1)(), (ct.c_uint32 * (0x8000 // 4))()]
        def location(address):
            for base, buffer in zip((ROOTS, GLOBAL, NODES), self.buffers):
                if base <= address < base + ct.sizeof(buffer):
                    return buffer, (address - base) // 4
            raise AssertionError(hex(address))
        for address, value in ram.items():
            buffer, index = location(address)
            buffer[index] = value
        self.log = []
        self.polls = []
        self.cancels = []
        self.cancel_result = 0
        self.waiting = None
        def waiting(kind, node):
            self.polls.append((kind, node))
            if kind not in defer:
                return False
            if self.waiting is None:
                self.waiting = (kind, node)
                return True
            self.assertEqual(self.waiting, (kind, node))
            self.waiting = None
            return False
        @SIGNAL
        def signal(_, node, value):
            self.assertEqual(value, 0x1a00)
            if fail == "signal":
                return -701
            if waiting("signal", node):
                return 1
            self.log.append(("signal", node))
            if node in moves:
                buffer, index = location(node + 0x18)
                buffer[index] = ENTRY + 0x200
            buffer, index = location(node + 0x48)
            buffer[index] = 0
            return 0
        @RETIRE
        def retire(_, node, notify, result):
            self.assertEqual(notify, 0)
            if fail == "retire":
                return -702
            if waiting("retire", node):
                return 1
            self.log.append(("retire", node))
            buffer, index = location(node)
            buffer[index] = 0
            result[0] = returns.get(node, 0)
            return 0
        @CANCEL
        def cancel(_, node, retiring):
            self.cancels.append((node, retiring))
            return self.cancel_result
        self.callbacks = (signal, retire, cancel)
        self.location = location
        self.assertEqual(self.lib.start(*self.buffers, limit, *self.callbacks), 1)

    def run_to_boundary(self, budget=1):
        result = 1
        for _ in range(10000):
            result = self.lib.step(budget)
            if result != 1:
                return result
        self.fail("retirement cursor failed to terminate")

    def finish(self):
        self.cancel_result = 0
        self.assertEqual(self.lib.close_owner(), 0)
        self.assertFalse(self.lib.opened())
        self.assertEqual(self.lib.release_maps(), 0)

    def test_128_original_mutating_forests_and_deferred_services(self):
        for seed in range(128):
            ram, returns, moves = self.fixture(seed)
            original = copy.deepcopy(ram)
            expected = self.original(original, returns, moves)
            for budget, defer in ((1, ()), (7, ("signal", "retire")), (1000, ())):
                self.begin(ram, returns, moves, defer)
                try:
                    self.assertEqual(self.run_to_boundary(budget), 0, (seed, budget))
                    self.assertEqual(self.log, expected, (seed, budget))
                    before = list(self.log)
                    self.assertEqual(self.lib.step(1000), 0)
                    self.assertEqual(self.log, before)
                    for address in ram:
                        buffer, index = self.location(address)
                        self.assertEqual(buffer[index], original[address], (seed, budget, hex(address)))
                finally:
                    self.finish()

    def simple(self):
        ram, _, _ = self.fixture(0, 1)
        for root in range(8):
            ram[ROOTS + root * 8 + 4] = NODES if root == 0 else 0
        ram.update({NODES: 1, NODES + 0x18: ENTRY, NODES + 0x48: 0,
                    NODES + 0x4c: 0, NODES + 0xb8: 0, NODES + 0xac: 0, NODES + 0x100: 0})
        return ram

    def test_predicate_filters_and_zone_mutation(self):
        for offset, value in ((0xb8, 1), (0, 0), (0x18, ENTRY + 4), (0xac, 0x01000000), (0x100, 0x10000)):
            ram = self.simple()
            ram[NODES + offset] = value
            self.begin(ram)
            try:
                self.assertEqual(self.run_to_boundary(), 0)
                self.assertEqual(self.log, [])
            finally:
                self.finish()

    def test_minus25_retires_parent_before_preserved_sibling(self):
        ram, _, _ = self.fixture(2, 3)
        a, b, c = (NODES + i * 0x200 for i in range(3))
        for root in range(8):
            ram[ROOTS + root * 8 + 4] = a if root == 0 else 0
        for node in (a, b, c):
            ram.update({node: 1, node + 0x18: ENTRY, node + 0x48: 0, node + 0x4c: 0,
                        node + 0xb8: 0, node + 0xac: 0, node + 0x100: 0})
        ram[a + 0x4c] = b
        ram[a + 0x48] = c
        expected = [("signal", b), ("retire", b), ("retire", a), ("signal", c), ("retire", c)]
        self.assertEqual(self.original(copy.deepcopy(ram), {b: -25}, ()), expected)
        for defer in ((), ("retire",)):
            self.begin(ram, {b: -25}, defer=defer)
            try:
                self.assertEqual(self.run_to_boundary(), 0)
                self.assertEqual(self.log, expected)
            finally:
                self.finish()
    def test_signal_zone_change_and_global_force_policy(self):
        for global_value, expected in ((0, [("signal", NODES)]),
                                       (0xffffffff, [("signal", NODES), ("retire", NODES)])):
            ram = self.simple()
            ram[GLOBAL] = global_value
            self.begin(ram, moves={NODES})
            try:
                self.assertEqual(self.run_to_boundary(), 0)
                self.assertEqual(self.log, expected)
            finally:
                self.finish()

    def test_async_cancellation_refusal_failure_and_no_replay(self):
        for service, retiring in (("signal", 0), ("retire", 1)):
            for failure in (False, True):
                self.begin(self.simple(), defer=(service,), fail=service if failure else None)
                try:
                    for _ in range(100):
                        status = self.lib.step(1)
                        if self.lib.active():
                            break
                    else:
                        self.fail("callback never became active")
                    self.assertEqual(status, (-701 if service == "signal" else -702) if failure else 1)
                    if failure:
                        self.assertEqual(self.lib.step(100), status)
                    snapshot = list(self.log)
                    self.cancel_result = 1
                    self.assertEqual(self.lib.close_owner(), 1)
                    self.assertTrue(self.lib.opened())
                    self.cancel_result = -703
                    self.assertEqual(self.lib.close_owner(), -703)
                    self.assertTrue(self.lib.opened())
                    self.assertEqual(self.log, snapshot)
                    self.assertEqual(self.cancels, [(NODES, retiring)] * 2)
                finally:
                    self.finish()

    def test_cycles_node_budget_and_zero_budget(self):
        for mutation in ("child", "sibling", "limit"):
            ram = self.simple()
            if mutation == "child":
                ram[NODES + 0x4c] = NODES
            if mutation == "sibling":
                ram[NODES + 0x48] = NODES
                ram[NODES] = 0  # prevent the fixture signal from clearing it
            self.begin(ram, limit=1 if mutation == "limit" else 16)
            try:
                self.assertEqual(self.lib.step(0), 1)
                self.assertEqual(self.log, [])
                self.assertEqual(self.run_to_boundary(), -180)
                self.assertEqual(self.lib.step(100), -180)
            finally:
                self.finish()


if __name__ == "__main__":
    unittest.main()
