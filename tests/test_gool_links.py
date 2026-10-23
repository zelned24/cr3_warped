"""Pure Python owner-link audit regressions; no compilation or emulation."""
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import cb3
import gool_links


class GoolLinks(unittest.TestCase):
    def test_variant_policy(self):
        self.assertEqual(gool_links.classify(object(),{'a':[], 'b':[]}),'local')
        self.assertEqual(gool_links.classify(None,{}),'absent_from_disc_index')
        self.assertEqual(gool_links.classify(None,{'a':[2,3]}),'external_unique_candidate')
        self.assertEqual(gool_links.classify(None,{'a':[2], 'b':[3]}),'external_ambiguous')

    def test_disc_coverage(self):
        report=gool_links.audit(cb3.load_level(lid) for lid in cb3.all_level_ids())
        self.assertEqual(report['archive_count'],42)
        self.assertFalse(report['reachability_proven'])
        seen=set();state_count=initial=missing_with_handlers=0
        for link in report['links']:
            key=link['level'],link['base_eid'],link['alternate_eid']
            self.assertNotIn(key,seen);seen.add(key)
            for state in link['states']:
                state_count+=1
                initial+=len(state['initial_entities'])
                for handler in state['alternate_handlers']:
                    if link['classification']=='local':self.assertTrue(handler['local_bounds'])
                    else:self.assertIsNone(handler['local_bounds'])
                if link['classification']=='absent_from_disc_index' and state['alternate_handlers']:
                    missing_with_handlers+=1
        self.assertEqual(state_count,15933)
        self.assertGreater(initial,0)
        self.assertGreater(missing_with_handlers,0)
        # Known raw constants: a generic alternate absent from the index must
        # remain separate from the level-specific local WilfC implementation.
        toad=[l for l in report['links'] if l['level']==11 and l['base']=='WillC']
        self.assertEqual(next(l for l in toad if l['alternate']=='2001I')['classification'],'absent_from_disc_index')
        self.assertEqual(next(l for l in toad if l['alternate']=='WilfC')['classification'],'local')


if __name__=='__main__':unittest.main()
