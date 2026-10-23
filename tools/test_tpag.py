"""Regression tests for tools/tpag.py and tools/dump_tex.py.

Run::

    python tools/test_tpag.py

Needs the retail disc image at ``%TEMP%\\opencode\\cbwarped_iso`` (same place
``tools/cb3.py`` looks).  Every expected value below was read out of that disc
and is quoted in ``docs/assets/TPAG.md``.

The tests are deliberately concrete: exact page dimensions, exact unique
colour counts, a couple of exact golden pixels, and the negative case (a CLUT
or a span outside the 64 KiB chunk must raise, never wrap).
"""
import os
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import cb3
import tpag

LEVEL_WARPED = 0x0B          # Toad Village
LEVEL_JUNGLE = 0x02          # gives a clean 8 bpp region (Sec2T)
LEVEL_TITLE = 0x3C           # only level with 16 bpp UVs (UI2YT)

#: BA10G UV 0 of level 0x0B, 12 bytes straight out of the TGEO item.
BA10G_UV0_HEX = "603f45017f3f62007f207f20"

#: Md1bT region referenced by El2bG: 64x64 at u 0..63 / v 64..127.
MD1BT_UV_HEX = "3f4041023f7f6200007f007f"


def load(level):
    return cb3.load_level(level)


class TestUvRecord(unittest.TestCase):
    def test_ba10g_uv0(self):
        uv = tpag.parse_uv(bytes.fromhex(BA10G_UV0_HEX))
        self.assertEqual(uv["clut"], 0x145)
        self.assertEqual(uv["tex_page"], 0x0062)
        self.assertEqual(uv["us"], [0x60, 0x7F, 0x7F, 0x7F])
        self.assertEqual(uv["vs"], [0x3F, 0x3F, 0x20, 0x20])
        self.assertEqual(uv["bounds"], (0x60, 0x7F, 0x20, 0x3F))
        self.assertEqual(uv["depth"], 4)
        self.assertEqual(uv["tex_x"], 128)
        self.assertEqual(uv["blend"], 3)
        self.assertEqual(uv["page_slot"], 0)
        self.assertEqual(uv["tex_index"], -1)

    def test_field_offsets(self):
        """clut at 0x2 and texpage at 0x6, little endian (parseUV ~line 347)."""
        raw = bytes([1, 2, 0x34, 0x12, 5, 6, 0x78, 0x56, 9, 10, 11, 12])
        uv = tpag.parse_uv(raw)
        self.assertEqual(uv["clut"], 0x1234)
        self.assertEqual(uv["tex_page"], 0x5678)
        self.assertEqual(uv["us"], [1, 5, 9, 11])
        self.assertEqual(uv["vs"], [2, 6, 10, 12])

    def test_short_record_raises(self):
        with self.assertRaises(ValueError):
            tpag.parse_uv(b"\x00" * 11)

    def test_make_uv_roundtrip(self):
        uv = tpag.parse_uv(bytes.fromhex(MD1BT_UV_HEX))
        again = tpag.make_uv(uv["clut"], uv["tex_page"], uv["bounds"])
        self.assertEqual(again["bounds"], (0, 63, 64, 127))
        self.assertEqual(again["tex_page"], 0x0062)
        self.assertEqual(again["depth"], 4)

    def test_mask_helpers(self):
        # 0x4E0 -> X base 0, 8 bpp, blend 3, slot 1
        self.assertEqual(tpag.tex_depth(0x4E0), 8)
        self.assertEqual(tpag.tex_x_base(0x4E0), 0)
        self.assertEqual(tpag.tex_blend(0x4E0), 3)
        self.assertEqual(tpag.tex_index(0x4E0), 1)
        # 0x163 -> X base 3 (192), 16 bpp, slot 0
        self.assertEqual(tpag.tex_depth(0x163), 16)
        self.assertEqual(tpag.tex_x_base(0x163), 192)


class TestColour(unittest.TestCase):
    def test_expand5to8(self):
        self.assertEqual(tpag.expand5to8(0), 0x00)
        self.assertEqual(tpag.expand5to8(1), 0x08)
        self.assertEqual(tpag.expand5to8(0x1F), 0xFF)

    def test_alpha_rules(self):
        # fillColor (~1307): 0x0000 -> 0x00, bit15 -> 0x80, else 0xFF
        self.assertEqual(tpag.unpack_rgba5551(0x0000), (0, 0, 0, 0x00))
        self.assertEqual(tpag.unpack_rgba5551(0x7FFF), (0xFF, 0xFF, 0xFF, 0xFF))
        self.assertEqual(tpag.unpack_rgba5551(0xFFFF), (0xFF, 0xFF, 0xFF, 0x80))

    def test_rgba5551_roundtrip(self):
        rgba = bytes([0, 0, 0, 0, 255, 0, 0, 255, 0, 128, 255, 0x80,
                      248, 8, 8, 255])
        packed = tpag.to_rgba5551(rgba, 4, 1)
        self.assertEqual(len(packed), 8)
        self.assertEqual(packed[:2], b"\x00\x00")          # alpha 0 -> bit15 0
        back = tpag.from_rgba5551(packed)
        self.assertEqual(back[3], 0x00)                    # stays transparent
        # 0xFF and 0x80 both only have bit15, so unpacking gives 0x80 for
        # both: the 0x80/0xFF split lives in RGBA8 + texture flags only.
        self.assertEqual(back[7], 0x80)
        self.assertEqual(back[11], 0x80)
        # 248 -> 5 bits (31) -> expands back to 255; 8 -> 8 stays exact.
        self.assertEqual(tuple(back[12:15]), (255, 8, 8))
        self.assertEqual(back[15], 0x80)


class TestGeometry(unittest.TestCase):
    def test_page_dimensions(self):
        self.assertEqual(tpag.page_dimensions(4), (1024, 128))
        self.assertEqual(tpag.page_dimensions(8), (512, 128))
        self.assertEqual(tpag.page_dimensions(16), (256, 128))
        for d in tpag.DEPTHS:
            self.assertEqual(tpag.page_bytes(d), 0x10000)
        with self.assertRaises(ValueError):
            tpag.page_dimensions(2)

    def test_cost(self):
        cost = tpag.texture_cost(256, 256)
        self.assertEqual(cost, {"pixels": 65536,
                                "rgba8": 65536 * 4,
                                "rgba5551": 65536 * 2})
        self.assertEqual(tpag.rgba8_size(32, 32), 4096)
        self.assertEqual(tpag.rgba5551_size(32, 32), 2048)


class TestPageDecode(unittest.TestCase):
    """Whole-page decodes: the numbers are the ones reported to the pipeline."""

    @classmethod
    def setUpClass(cls):
        cls.arc = load(LEVEL_WARPED)

    def test_cr10t_page(self):
        page = self.arc.texture("Cr10T")
        self.assertIsNotNone(page)
        self.assertEqual(len(page.data), 0x10000)
        image = tpag.decode_page(page.data, depth=4, clut=0x145, name="Cr10T")
        self.assertEqual((image.width, image.height), (1024, 128))
        self.assertEqual(len(image.rgba), 1024 * 128 * 4)
        self.assertEqual(image.depth, 4)
        # 16-entry palette, two entries hold the same colour -> 15 distinct.
        self.assertEqual(image.unique_colors(), 15)
        self.assertLessEqual(image.unique_colors(), 16)
        self.assertFalse(image.is_uniform())
        # Every entry of this CLUT has bit15 set (STP), so the reference maps
        # all 131072 texels to alpha 0x80.
        self.assertEqual(image.alpha_histogram(), {0x80: 1024 * 128})
        self.assertFalse(image.has_opa)
        self.assertTrue(image.has_xlu)

    def test_md1bt_page(self):
        page = self.arc.texture("Md1bT")
        image = tpag.decode_page(page.data, depth=4, clut=0x20B, name="Md1bT")
        self.assertEqual((image.width, image.height), (1024, 128))
        self.assertEqual(image.unique_colors(), 16)
        self.assertFalse(image.is_uniform())
        self.assertEqual(image.alpha_histogram(), {0x80: 1024 * 128})

    def test_md1bt_region(self):
        """64x64 cell referenced by El2bG -- the 'wooden panel' atlas cell."""
        page = self.arc.texture("Md1bT")
        uv = tpag.parse_uv(bytes.fromhex(MD1BT_UV_HEX))
        self.assertEqual(uv["bounds"], (0, 63, 64, 127))
        image = tpag.decode_texture(page.data, uv)
        self.assertEqual((image.width, image.height), (64, 64))
        self.assertEqual(len(image.rgba), 64 * 64 * 4)
        self.assertEqual(image.unique_colors(), 15)
        self.assertFalse(image.is_uniform())
        # Golden pixels: black outer frame, blue trim, beige panel fill.
        self.assertEqual(image.pixel(0, 0), (0, 0, 0, 0x80))
        self.assertEqual(image.pixel(4, 4), (0, 107, 247, 0x80))
        self.assertEqual(image.pixel(16, 32), (247, 239, 181, 0x80))


class TestBa10gRegion(unittest.TestCase):
    """The control region required by the task: BA10G UV 0 on Cr10T."""

    @classmethod
    def setUpClass(cls):
        cls.page = load(LEVEL_WARPED).texture("Cr10T")
        cls.uv = tpag.parse_uv(bytes.fromhex(BA10G_UV0_HEX))
        cls.image = tpag.decode_texture(cls.page.data, cls.uv)

    def test_dimensions(self):
        self.assertEqual((self.image.width, self.image.height), (32, 32))
        self.assertEqual(len(self.image.rgba), 32 * 32 * 4)

    def test_not_uniform(self):
        self.assertFalse(self.image.is_uniform())
        self.assertGreater(self.image.unique_colors(), 1)
        self.assertNotEqual(self.image.pixel(0, 0), self.image.pixel(16, 16))

    def test_colour_and_alpha(self):
        self.assertEqual(self.image.unique_colors(), 15)
        self.assertEqual(self.image.alpha_histogram(), {0x80: 1024})
        # Golden pixels: grey frame corner, yellow sign body.
        self.assertEqual(self.image.pixel(0, 0), (173, 173, 173, 0x80))
        self.assertEqual(self.image.pixel(16, 16), (255, 255, 49, 0x80))

    def test_pack(self):
        payload, meta = tpag.pack_texture(self.image)
        self.assertEqual(len(payload), 2048)
        self.assertEqual(meta["width"], 32)
        self.assertEqual(meta["height"], 32)
        self.assertEqual(meta["format"], 0)
        self.assertEqual(meta["size"], 2048)
        self.assertEqual(meta["flags"] & 2, 2)   # has semi-transparent texels
        self.assertEqual(meta["flags"] & 1, 0)   # no fully transparent texel


class TestOtherDepths(unittest.TestCase):
    """8 bpp and 16 bpp use different addressing; both must stay coherent."""

    def test_8bpp_region(self):
        arc = load(LEVEL_JUNGLE)
        page = arc.texture("Sec2T")
        uv = tpag.make_uv(0x40, 0x4E0, (0, 63, 64, 127))
        self.assertEqual(uv["depth"], 8)
        image = tpag.decode_texture(page.data, uv)
        self.assertEqual((image.width, image.height), (64, 64))
        self.assertGreaterEqual(image.unique_colors(), 200)
        self.assertFalse(image.is_uniform())

    def test_16bpp_region(self):
        arc = load(LEVEL_TITLE)
        page = arc.texture("UI2YT")
        uv = tpag.make_uv(0x0000, 0x0120, (0, 63, 64, 127))
        self.assertEqual(uv["depth"], 16)
        self.assertEqual(uv["tex_x"], 0)
        image = tpag.decode_texture(page.data, uv)
        self.assertEqual((image.width, image.height), (64, 64))
        self.assertGreaterEqual(image.unique_colors(), 500)
        self.assertFalse(image.is_uniform())

    def test_8bpp_palette_is_256(self):
        page = load(LEVEL_JUNGLE).texture("Sec2T")
        self.assertEqual(len(tpag.read_palette(page.data, 0x40, 8)), 256)
        self.assertEqual(len(tpag.read_palette(page.data, 0x40, 4)), 16)


class TestBoundsGuards(unittest.TestCase):
    """Out-of-chunk reads must raise, not silently wrap into other rows."""

    def test_span_past_end(self):
        page = load(LEVEL_WARPED).texture("Cr10T")
        # 8 bpp page is 512 wide; xbase 3 shifts by 2*192 bytes, so a full
        # 512x128 window no longer fits in the 64 KiB chunk.
        uv = tpag.make_uv(0x0000, 0x0083, (0, 255, 0, 127))
        self.assertEqual((uv["depth"], uv["tex_x"]), (8, 192))
        with self.assertRaises(ValueError):
            tpag.decode_texture(page.data, uv)

    def test_clut_past_end(self):
        page = load(LEVEL_WARPED).texture("Cr10T")
        with self.assertRaises(ValueError):
            tpag.read_palette(page.data, 0x1FFF, 8)

    def test_empty_bounds(self):
        page = load(LEVEL_WARPED).texture("Cr10T")
        with self.assertRaises(ValueError):
            tpag.make_uv(0x145, 0x0062, (5, 4, 0, 0))
        uv = tpag.make_uv(0x145, 0x0062, (0, 31, 0, 31))
        uv["min_u"], uv["max_u"] = 5, 4
        with self.assertRaises(ValueError):
            tpag.decode_texture(page.data, uv)


class TestTextureCache(unittest.TestCase):
    def setUp(self):
        self.arc = load(LEVEL_WARPED)
        self.cache = tpag.TextureCache.from_archive(self.arc)

    def test_unknown_page(self):
        uv = tpag.parse_uv(bytes.fromhex(BA10G_UV0_HEX))
        self.assertEqual(self.cache.find_or_parse("nopeT", uv), -1)
        self.assertEqual(len(self.cache), 0)

    def test_dedup_and_extend(self):
        uv_a = tpag.make_uv(0x145, 0x0062, (96, 127, 32, 63))
        uv_b = tpag.make_uv(0x145, 0x0062, (80, 111, 16, 47))
        uv_c = tpag.make_uv(0x1CC, 0x0062, (0, 31, 0, 31))

        self.assertEqual(self.cache.find_or_parse("Cr10T", uv_a), 0)
        self.assertEqual(uv_a["tex_index"], 0)
        self.assertEqual(len(self.cache), 1)

        # Same page/texpage/clut -> same atlas slot, bounds grow to the union.
        self.assertEqual(self.cache.find_or_parse("Cr10T", uv_b), 0)
        self.assertEqual(len(self.cache), 1)
        self.assertEqual(self.cache.tex_list[0].bounds, (80, 127, 16, 63))
        self.assertEqual((self.cache.tex_list[0].width,
                          self.cache.tex_list[0].height), (48, 48))

        # Different CLUT -> different slot.
        self.assertEqual(self.cache.find_or_parse("Cr10T", uv_c), 1)
        self.assertEqual(len(self.cache), 2)
        self.assertEqual(uv_c["tex_index"], 1)

    def test_key_masks_texture_index_only(self):
        uv_a = tpag.make_uv(0x145, 0x0062, (0, 31, 0, 31))
        uv_b = tpag.make_uv(0x145, 0x0462, (0, 31, 0, 31))  # same, slot 1
        self.assertEqual(tpag.make_key("Cr10T", uv_a),
                         tpag.make_key("Cr10T", uv_b))
        uv_c = tpag.make_uv(0x145, 0x00E2, (0, 31, 0, 31))  # 8 bpp
        self.assertNotEqual(tpag.make_key("Cr10T", uv_a),
                            tpag.make_key("Cr10T", uv_c))

    def test_page_name_format(self):
        uv = tpag.parse_uv(bytes.fromhex(BA10G_UV0_HEX))
        self.assertEqual(tpag.tex_page_name("Cr10T", uv), "Cr10T_62_145")

    def test_contains_and_extend(self):
        uv = tpag.make_uv(0x145, 0x0062, (96, 127, 32, 63))
        self.assertTrue(tpag.contains_uvs((96, 127, 32, 63), uv))
        self.assertTrue(tpag.contains_uvs((0, 127, 0, 63), uv))
        other = tpag.make_uv(0x145, 0x0062, (0, 63, 0, 63))
        self.assertFalse(tpag.contains_uvs((96, 127, 32, 63), other))
        self.assertEqual(tpag.extend_bounds((96, 127, 32, 63), other),
                         (0, 127, 0, 63))

    def test_total_bytes(self):
        uv = tpag.make_uv(0x145, 0x0062, (0, 31, 0, 31))
        self.cache.find_or_parse("Cr10T", uv)
        self.assertEqual(self.cache.total_bytes(16), 32 * 32 * 2)
        self.assertEqual(self.cache.total_bytes(32), 32 * 32 * 4)


class TestDumpCli(unittest.TestCase):
    """End to end: the exact command from the task must produce two PNGs."""

    def test_dump_cr10t(self):
        out = tempfile.mkdtemp(prefix="tpag_dump_")
        cmd = [sys.executable, os.path.join(HERE, "dump_tex.py"),
               "--level", "0x0B", "--page", "Cr10T", "--out", out,
               "--uv-clut", "0x145", "--uv-page", "0x62"]
        proc = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertIn("1024x128", proc.stdout)
        self.assertIn("32x32", proc.stdout)
        page_png = os.path.join(out, "Cr10T_page_4bpp_clut0145.png")
        uv_png = os.path.join(out, "Cr10T_uv_0145_96-127_32-63.png")
        for path in (page_png, uv_png):
            self.assertTrue(os.path.exists(path), path)
            with open(path, "rb") as fh:
                head = fh.read(24)
            self.assertEqual(head[:8], b"\x89PNG\r\n\x1a\n")
            w, h = struct.unpack(">II", head[16:24])
            self.assertEqual((w, h), (1024, 128) if path == page_png else (256, 256))


if __name__ == "__main__":
    unittest.main(verbosity=2)
