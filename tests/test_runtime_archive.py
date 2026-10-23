"""Pure Python transport checks, including real disc bytes and malformed input."""
from pathlib import Path
import struct
import sys
from types import SimpleNamespace
import unittest
import zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import runtime_archive as runtime
import cb3


class RuntimeArchive(unittest.TestCase):
    def fixture(self):
        return runtime.encode(11,[SimpleNamespace(eid=123,type=11,items=[b'ab',b'',b'c'])],b'nsd')

    def test_roundtrip_and_boundaries(self):
        data=self.fixture()
        self.assertEqual(runtime.decode(data),{'level':11,'nsd':b'nsd',
            'entries':[{'eid':123,'type':11,'items':[b'ab',b'',b'c']}]})
        self.assertEqual(runtime.decode(runtime.encode(2,[]))['entries'],[])

    def test_truncations_corruption_trailing(self):
        data=self.fixture()
        for size in range(len(data)):
            with self.assertRaises(ValueError):runtime.decode(data[:size])
        for offset in range(len(data)):
            changed=bytearray(data);changed[offset]^=1
            with self.assertRaises(ValueError):runtime.decode(changed)
        with self.assertRaises(ValueError):runtime.decode(data+b'junk')

    def test_metadata_extents_with_valid_crc(self):
        # Exercise extent checks independently of checksum rejection.
        cases=[(0,0),(4,99),(8,64),(12,8193),(16,0),(20,0),(24,0xffffffff),
               (36,12),(40,257),(44,0),(48,0),(52,0xffffffff),(56,0),(60,0xffffffff)]
        for offset,value in cases:
            with self.subTest(offset=offset,value=value):
                changed=bytearray(self.fixture())
                struct.pack_into('<I',changed,offset,value)
                struct.pack_into('<I',changed,len(changed)-4,zlib.crc32(changed[:-4]))
                with self.assertRaises(ValueError):runtime.decode(changed)

    def test_duplicate_identity_rejected(self):
        e=SimpleNamespace(eid=1,type=11,items=[])
        with self.assertRaises(ValueError):runtime.encode(11,[e,e])
        with self.assertRaises(ValueError):runtime.encode(-1,[])
        with self.assertRaises(ValueError):runtime.encode(11,[SimpleNamespace(eid=1,type=2,items=[])])

    def test_all_disc_sections_exact(self):
        total=0
        for lid in cb3.all_level_ids():
            arc=cb3.load_level(lid,cache=False)
            entries=[e for e in arc.entries if e.type in (7,11)]
            nsd=arc.nsd.buf if arc.nsd else b''
            a=runtime.encode(lid,entries,nsd)
            self.assertEqual(a,runtime.encode(lid,entries,nsd))
            result=runtime.decode(a)
            self.assertEqual(result['nsd'],nsd)
            for original,restored in zip(entries,result['entries']):
                self.assertEqual((original.eid,original.type,list(original.items)),
                                 (restored['eid'],restored['type'],restored['items']))
                total+=len(original.items)
            self.assertEqual(len(result['entries']),len(entries))
        print(f'Exact GOOL/ZDAT section roundtrip: {total} sections, all 42 disc archives')


if __name__=='__main__':unittest.main()
