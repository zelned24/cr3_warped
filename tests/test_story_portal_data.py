"""Source/data contract checks only: no C compiler, game or emulator is run.

The future C execution harness is tests/test_story_portals.c. These checks
validate the actual descriptor table and its topology, not C execution or
the original game's still unverified unlock/spawn rules.
"""
from pathlib import Path
import re
import unittest
try:
    from pycparser import c_parser
except ImportError:
    c_parser = None

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / 'source/story.c').read_text(encoding='utf-8')
HEADER = (ROOT / 'source/story.h').read_text(encoding='utf-8')


def descriptors():
    table = SOURCE.split('static const StoryPortal s_portals[] = {', 1)[1].split('};', 1)[0]
    rows = re.findall(r'\{\s*(\d+),\s*(0x[0-9A-F]+),\s*(\d+),\s*(\d+),\s*'
                      r'(STORY_ENTRY_\w+),\s*(\d+)\s*\}', table)
    return [dict(id=int(pid), level=int(level, 16), chamber=int(chamber),
                 slot=int(slot), kind=kind, relics=int(relics))
            for pid, level, chamber, slot, kind, relics in rows]


class StoryPortalData(unittest.TestCase):
    def setUp(self):
        self.rows = descriptors()

    def test_ids_are_dense_and_unique(self):
        self.assertEqual([row['id'] for row in self.rows], list(range(35)))
        self.assertEqual(len({(row['chamber'], row['slot']) for row in self.rows}), 35)
        for row in self.rows:
            self.assertEqual(row['id'], row['chamber'] * 6 + row['slot'])

    def test_each_main_chamber_has_five_levels_and_one_boss(self):
        expected = [[11, 14, 10, 12, 25], [15, 22, 21, 18, 17],
                    [16, 28, 26, 20, 29], [30, 19, 13, 27, 24],
                    [35, 32, 34, 23, 36]]
        for chamber, levels in enumerate(expected):
            group = [row for row in self.rows if row['chamber'] == chamber]
            self.assertEqual([row['level'] for row in group[:5]], levels)
            self.assertTrue(all(row['kind'] == 'STORY_ENTRY_NORMAL' for row in group[:5]))
            self.assertEqual(group[5]['kind'], 'STORY_ENTRY_BOSS')
            self.assertTrue(all(row['relics'] == 0 for row in group))

    def test_boss_table_matches_descriptors_and_has_five_elements(self):
        table = SOURCE.split('s_boss_level_ids[TOTAL_BOSSES] = {', 1)[1].split('};', 1)[0]
        ids = [int(value, 16) for value in re.findall(r'0x[0-9A-F]+', table)]
        self.assertEqual(ids, [6, 3, 4, 5, 7])
        self.assertEqual([row['level'] for row in self.rows if row['kind'] == 'STORY_ENTRY_BOSS'], ids)
        self.assertRegex(HEADER, r'#define\s+TOTAL_BOSSES\s+5\b')
        self.assertIn('bosses_defeated[TOTAL_BOSSES]', HEADER)
        self.assertNotIn('s_boss_level_ids[TOTAL_CHAMBERS]', SOURCE)
        self.assertNotIn('s_boss_powers[TOTAL_CHAMBERS]', SOURCE)

    def test_basement_contains_five_existing_policy_entries(self):
        basement = [row for row in self.rows if row['chamber'] == 5]
        self.assertEqual([row['slot'] for row in basement], [0, 1, 2, 3, 4])
        self.assertEqual([row['relics'] for row in basement], [5, 10, 15, 20, 25])
        self.assertEqual([row['level'] for row in basement], [33, 22, 37, 27, 31])
        self.assertFalse(any(row['level'] in (0x26, 0x27) for row in self.rows))

    def test_shared_archive_has_distinct_entrance_and_same_collectible_index(self):
        for level, normal_id, secret_id, kind in (
            (0x16, 7, 31, 'STORY_ENTRY_HANG_EM_HIGH_SECRET'),
            (0x1B, 21, 33, 'STORY_ENTRY_FUTURE_FRENZY_SECRET'),
        ):
            pair = [row for row in self.rows if row['level'] == level]
            self.assertEqual([row['id'] for row in pair], [normal_id, secret_id])
            self.assertEqual([row['kind'] for row in pair], ['STORY_ENTRY_NORMAL', kind])

    def test_no_unsupported_original_spawn_coordinates_are_fabricated(self):
        portal = HEADER.split('typedef struct {', 1)[1].split('} StoryPortal;', 1)[0]
        self.assertIn('StoryEntryKind entry_kind;', portal)
        self.assertNotRegex(portal, r'\b(spawn_[xyz]|zone_id|placement_id)\b')
        self.assertIn('native verification pending', portal)

    @unittest.skipIf(c_parser is None, 'optional pycparser not installed')
    def test_story_source_parses_as_c_without_a_compiler(self):
        # Only parser syntax is checked: no preprocessing, type checking,
        # linking, object generation or execution is claimed here.
        prefix = 'typedef unsigned char uint8_t; typedef unsigned int uint32_t;'
        prefix += 'typedef unsigned int size_t; typedef int FILE;'
        prefix += 'typedef int SfxId;'
        text = prefix + HEADER + SOURCE
        text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
        text = re.sub(r'^\s*#.*$', '', text, flags=re.M)
        tree = c_parser.CParser().parse(text)
        names = {node.decl.name for node in tree.ext if type(node).__name__ == 'FuncDef'}
        self.assertIn('story_resolve_portal', names)
        self.assertIn('story_on_boss_defeated', names)


if __name__ == '__main__':
    unittest.main()
