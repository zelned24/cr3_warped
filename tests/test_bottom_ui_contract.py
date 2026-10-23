"""Source contract checks only; no compiler, emulator or raster validation.

These catch reintroduced top-screen counters and missing producer/consumer
bindings while the user has suspended executable validation.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
MAIN = (ROOT / "source/main.c").read_text()
UI = (ROOT / "source/bottom_ui.c").read_text()
HEADER = (ROOT / "source/bottom_ui.h").read_text()


def body(source, name):
    match = re.search(r"\b" + name + r"\([^;]*?\)\s*\{", source)
    if not match:
        raise AssertionError("Missing function " + name)
    start = match.end()
    depth, pos = 1, start
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[start:pos - 1]


class BottomUiContract(unittest.TestCase):
    def test_top_gameplay_only_reticle(self):
        hud = body(MAIN, "draw_hud")
        self.assertIn("draw_crosshair_2d", hud)
        self.assertNotIn("text_draw", hud)
        self.assertNotIn("g_stats", hud)
        self.assertNotIn("draw_pause_menu", MAIN)
        self.assertNotIn("draw_warp_room_hud", MAIN)

    def test_live_status_is_bound_to_bottom(self):
        draw = body(MAIN, "draw_bottom_screen")
        for field in ("aku_aku", "time_trial", "time_trial_ms", "boss_active",
                      "boss_hp", "boss_max_hp", "paused", "pause_index", "aiming"):
            with self.subTest(field=field):
                self.assertIn("." + field + " =", draw)
                self.assertIn("s->" + field, UI)
        self.assertIn("props_get_time_trial_ms()", draw)
        self.assertIn("props_boss_hp()", draw)

    def test_buttons_and_hits_share_one_table(self):
        self.assertIn("buttons[i].x", body(UI, "bottom_ui_hit_test"))
        self.assertIn("buttons[i].width", body(UI, "bottom_ui_hit_test"))
        self.assertIn("buttons[i].x,buttons[i].width", body(UI, "bottom_ui_build"))
        self.assertIn("bottom_ui_hit_test(touch.px,touch.py", MAIN)
        self.assertNotIn("touch.px >= 84", MAIN)
        self.assertNotIn("touch.px >= 158", MAIN)

    def test_pause_touch_uses_existing_state_machine(self):
        self.assertIn("action==BOTTOM_UI_RESTART) s_pause_index=1", MAIN)
        self.assertIn("action==BOTTOM_UI_TITLE) s_pause_index=3", MAIN)
        self.assertIn("kDown |= KEY_A", MAIN)
        self.assertIn('"UP/DOWN SELECT   A OK   B RESUME"', UI)

    def test_context_and_powers_are_alternatives(self):
        draw = body(UI, "bottom_ui_build")
        self.assertIn("else if(s->context)", draw)
        self.assertIn("power_icon", draw)
        self.assertIn("context_detail", HEADER)
        self.assertIn("props_get_nearest_portal", body(MAIN, "draw_bottom_screen"))


if __name__ == "__main__":
    unittest.main()
