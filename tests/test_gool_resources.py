"""Keep exact original alternate-owner provenance separate from reachability."""
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from gool_resource_audit import build_report


class OriginalResources(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.report = build_report()

    def test_inventory_matches_retained_original_evidence(self):
        stored = json.loads((ROOT/'tests/gool_resource_evidence.json').read_text())
        # JSON object keys stringify integer page types.
        self.assertEqual(json.loads(json.dumps(self.report)), stored)

    def test_missing_slots_are_absent_from_all_original_directories(self):
        totals = self.report['totals']
        for key, value in {'gool_entries': 557, 'states': 15933, 'libraries': 39,
            'unresolved_handler_references': 8110, 'unresolved_eids': 14,
            'unresolved_owner_level_links': 502, 'type5_entries': 166}.items():
            self.assertEqual(totals[key], value, key)
        self.assertEqual(self.report['unresolved_by_behavior'], {'WillC': 7498, 'WEfOC': 612})
        for resource in self.report['resources']:
            self.assertFalse(resource['raw_nsf_occurrences'])
            self.assertFalse(resource['nsd_directory_occurrences'])
            self.assertEqual(len(resource['references']), resource['count'])
            self.assertTrue(all(row[4] & 0x4000 for row in resource['references']))

    def test_local_variants_replace_distinct_template_slots(self):
        slots = {(r['level'], r['behavior']): r['slots']
                 for r in self.report['level_variant_slots']}
        for level, index, name in ((2, 9, 'Wil2C'), (11, 11, 'WilfC'),
                                   (20, 7, 'WilkC'), (13, 8, 'WildC')):
            slot = slots[level, 'WillC'][index]
            self.assertEqual(slot['name'], name)
            self.assertEqual(slot['local_sections'], 3)
        self.assertEqual(slots[2, 'WEfOC'][1]['name'], 'WEf2C')
        self.assertEqual(slots[2, 'WEfOC'][1]['local_sections'], 3)
        self.assertIsNone(slots[11, 'WEfOC'][1]['local_sections'])


if __name__ == '__main__':
    unittest.main()
