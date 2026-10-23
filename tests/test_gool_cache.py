"""GOOL cache identity regressions. Pure Python; never invokes a compiler."""
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import cb3
import zdat


def snapshot(table):
    return (table.name,table.category,table.constants,table.object_indices,
            [s.as_dict() for s in table.states],
            [m.as_dict() for m in table.models])


class GoolCache(unittest.TestCase):
    def setUp(self):
        zdat._GOOL_CACHE.clear()

    def test_same_eid_different_payload(self):
        a=SimpleNamespace(eid=1,name='same',items=[b'a',b'bc'])
        b=SimpleNamespace(eid=1,name='same',items=[b'ab',b'c'])
        c=SimpleNamespace(eid=1,name='same',items=[b'a',b'bd'])
        tables=[zdat.parse_gool_table(e) for e in (a,b,c)]
        self.assertEqual(len(zdat._GOOL_CACHE),3)
        self.assertEqual(len({id(t) for t in tables}),3)
        clone=SimpleNamespace(eid=1,name='same',items=[b'a',b'bc'])
        self.assertIs(zdat.parse_gool_table(clone),tables[0])

    def test_disc_variants_independent_of_archive_order(self):
        expected={}
        variants={}
        ids=cb3.all_level_ids()
        self.assertEqual(len(ids),42)
        # Parse each archive in isolation to establish expected decoded tables.
        for lid in ids:
            zdat._GOOL_CACHE.clear()
            for e in cb3.load_level(lid,cache=False).of_type(11):
                value=snapshot(zdat.parse_gool_table(e))
                expected[lid,e.eid]=value
                variants.setdefault((e.eid,len(e.items)),set()).add(repr(value))
        differing=sum(len(v)>1 for v in variants.values())
        self.assertGreater(differing,0)
        # Reuse one cache across every archive, then reverse the load order.
        for order in (ids,list(reversed(ids))):
            zdat._GOOL_CACHE.clear()
            for lid in order:
                for e in cb3.load_level(lid,cache=False).of_type(11):
                    with self.subTest(level=lid,name=e.name,reverse=order[0]!=ids[0]):
                        self.assertEqual(snapshot(zdat.parse_gool_table(e)),expected[lid,e.eid])
        print(f'Checked {len(expected)} GOOL occurrences across 42 archives; '
              f'{differing} old cache keys have differing decoded tables.')


if __name__=='__main__':unittest.main()
