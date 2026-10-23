"""Pure Python contracts/data checks; production C is not executed here."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
MAIN = (ROOT / "source/main.c").read_text()
UI = (ROOT / "source/bottom_ui.c").read_text()
STORY = (ROOT / "source/story.c").read_text()


def body(source, name):
    match = re.search(r"\b" + name + r"\([^;]*?\)\s*\{", source)
    if not match:
        raise AssertionError(name)
    start = pos = match.end()
    depth = 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[start:pos-1]


class TrialUiContract(unittest.TestCase):
    def test_availability_queries_table_without_generic_fallback(self):
        query = body(STORY, "story_has_relic_targets")
        self.assertIn("s_canonical_levels[i].level_id==level_id", query)
        self.assertIn("return 0", query)
        self.assertNotIn("90000", query)
        self.assertNotIn("g_save", query)

    def test_registered_targets_are_positive_and_ordered(self):
        table = STORY[STORY.index("s_canonical_levels[]"):STORY.index("int story_get_canonical_crate_count")]
        rows = re.findall(r"\{\s*(0x[0-9A-Fa-f]+)\s*/\*.*?\*/\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}", table)
        self.assertEqual(len(rows), 30)
        ids = set()
        for level, crates, sapphire, gold, platinum in rows:
            ids.add(int(level, 16))
            self.assertGreater(int(platinum), 0)
            self.assertGreaterEqual(int(gold), int(platinum))
            self.assertGreaterEqual(int(sapphire), int(gold))
        self.assertEqual(len(ids), 30)
        self.assertTrue(ids.isdisjoint({2, 5, 6, 7, 255, -1}))

    def test_draw_only_uses_targets_when_available(self):
        draw = body(MAIN, "draw_bottom_screen")
        self.assertIn("trial_supported_route && story_has_relic_targets(trial_lid)", draw)
        self.assertRegex(draw, r"if\(trial_available\)\s*story_get_canonical_relic_times")
        self.assertIn("g_save.levels[trial_lid].best_time_ms", draw)
        self.assertIn("g_save.levels[trial_lid].relic_tier", draw)
        self.assertIn("selected->entry_kind<=STORY_ENTRY_BOSS", draw)
        self.assertNotIn("story_get_relic_tier", draw)
        self.assertNotIn("story_on_level_cleared", draw)

    def test_page_navigation_precedes_pause_activation(self):
        index = MAIN.index("bottom_ui_action action=bottom_ui_hit_test")
        touch = MAIN[index:MAIN.index("/* State machine logic", index)]
        self.assertLess(touch.index("action==BOTTOM_UI_STATUS_PAGE"), touch.index("s_state == STATE_PAUSE"))
        self.assertIn("s_bottom_page=action==BOTTOM_UI_STATUS_PAGE", touch)
        self.assertIn("else if (s_state == STATE_PAUSE", touch)

    def test_tab_hits_use_rendered_bounds_in_reserved_touch_strip(self):
        hit = body(UI, "bottom_ui_hit_test")
        tabs = body(UI, "page_tabs")
        self.assertIn("y>=218 && y<236", hit)
        self.assertIn("tabs[i].x", hit)
        self.assertIn("tabs[i].width", hit)
        self.assertIn("tabs[i].x,218,tabs[i].width,18", tabs)
        camera = body(MAIN, "update_camera")
        self.assertIn("touch.py >= 168", camera)

    def test_missing_best_and_unsupported_targets_have_placeholders(self):
        self.assertIn('"--:--.--"', body(UI, "bottom_ui_format_time"))
        trial = body(UI, "trial_page")
        self.assertIn("s->best_time_ms!=0", trial)
        self.assertIn('"NO TARGETS FOR THIS AREA"', trial)
        self.assertIn("s->saved_relic_tier==i+1", trial)
        self.assertIn("strlen(value)>8?1:2", body(UI, "time_value"))
        self.assertIn('"TIME TRIAL / PORT TARGETS"', UI)


if __name__ == "__main__":
    unittest.main()
