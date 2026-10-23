"""Table routing tests only; no delivery/ack or native-code execution claim."""
from pathlib import Path
import struct
import sys
from types import SimpleNamespace
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from gool_signal import Route,lookup,resolve_script,audit
import cb3


class GoolSignal(unittest.TestCase):
    def entry(self,table=(255,1,0x8004)):
        header=bytearray(20);struct.pack_into('<I',header,16,len(table))
        return SimpleNamespace(items=[header,b'\0'*32,b'',struct.pack('<%dH'%len(table),*table),b'\0'*32,b''])

    def test_none_state_script_and_outside_table(self):
        e=self.entry()
        self.assertEqual(lookup(e,0),Route('none',0,255))
        self.assertEqual(lookup(e,0x100),Route('state',1,1,1))
        self.assertEqual(lookup(e,0x200),Route('script',2,0x8004,4))
        self.assertEqual(lookup(e,0xffffff00),Route('outside_table',0xffffff))
        self.assertEqual(resolve_script(e,lookup(e,0x200)),(e,4))

    def test_signal_index_uses_unsigned_word_shift_not_signed(self):
        e=self.entry()
        for low in range(256):
            self.assertEqual(lookup(e,0x100|low),Route('state',1,1,1))
        self.assertEqual(lookup(e,0x80000000),Route('outside_table',0x800000))
        for raw in (-1,1<<32):
            with self.assertRaises(ValueError):lookup(e,raw)

    def test_code_route_stays_code_even_when_value_looks_like_state(self):
        e=self.entry((0x8001,))
        r=lookup(e,0)
        self.assertEqual((r.kind,r.value),('script',1))
        self.assertEqual(resolve_script(e,r),(e,1))

    def test_explicit_route_uses_base_even_with_bit_4000_and_checks_bounds(self):
        e=self.entry((0xc004,))
        route=lookup(e,0)
        self.assertEqual(resolve_script(e,route),(e,4))
        e.items[3]=struct.pack('<H',0x8008)
        with self.assertRaises(ValueError):resolve_script(e,lookup(e,0))
        for r in (Route('state',0,1,1),Route('script',0,0x8004,5)):
            with self.assertRaises(ValueError):resolve_script(e,r)

    def test_corrupt_state_and_table_are_not_silent_ignored_routes(self):
        e=self.entry((2,))
        with self.assertRaises(ValueError):lookup(e,0)
        e=self.entry();e.items[3]=e.items[3][:-1]
        with self.assertRaises(ValueError):lookup(e,0)

    def test_library_exposes_no_table_despite_declared_signal_count(self):
        e=self.entry();e.items=e.items[:3]
        self.assertEqual(lookup(e,0),Route('outside_table',0))

    def test_all_original_entries_preserve_explicit_addresses(self):
        report=audit(cb3.load_level(lid) for lid in cb3.all_level_ids())
        self.assertEqual(report['archive_count'],42)
        self.assertEqual(report['behavior_occurrences'],557)
        self.assertEqual(report['entries_by_kind'],{'none':39488,'script':969,'state':1558})
        self.assertFalse(report['execution_proven'])
        scripts=[r for r in report['routes'] if r['kind']=='script']
        self.assertEqual(len(scripts),969)
        self.assertTrue(all(r['code_bounds_verified'] and r['code_owner']=='base' for r in scripts))
        # All occurrences keep their identity; equal names are not assumed equal.
        self.assertEqual(len(report['identities']),557)
        self.assertTrue(all(len(r['sha256'])==64 for r in report['identities']))


if __name__=='__main__':unittest.main()
