"""Verify exported object assets, coverage accounting and frame/UV integrity."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import unittest

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import cb3
import export_object_graphics
import graphics_texture_pool


class ObjectGraphics(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.output = ROOT / "game_data/objects"
        cls.manifest = json.loads((cls.output / "manifest.json").read_text(encoding="utf-8"))

    def test_complete_inventory_accounting(self):
        levels = self.manifest["levels"]
        self.assertEqual(sorted(level["id"] for level in levels), cb3.all_level_ids())
        self.assertEqual(self.manifest["totals"]["archives"], 42)
        self.assertFalse(self.manifest["runtime_integrated"])
        for level in levels:
            exported_names = {pair["geo"] for pair in level["pairs"]
                              if pair.get("geo_provenance",{}).get("resolution","local_archive") == "local_archive"}
            self.assertEqual(len(exported_names) + len(level["tgeo_without_pair"]), level["tgeo_count"])
            self.assertTrue(exported_names.isdisjoint(level["tgeo_without_pair"]))
            for pair in level["pairs"]:
                self.assertIn(pair["asset"], self.manifest["models"])

    def test_all_geometry_and_frame_buffers(self):
        for identity, record in self.manifest["models"].items():
            folder = self.output / record["path"]
            with self.subTest(identity=identity, geo=record["geo"]):
                mesh = json.loads((folder / "mesh.json").read_text(encoding="utf-8"))
                with (folder / "frames.bin").open("rb") as stream:
                    magic, version, frames, corners = struct.unpack("<4sIII", stream.read(16))
                self.assertEqual((magic, version), (b"CBOF", 1))
                self.assertEqual((frames, corners), (mesh["frame_count"], mesh["vertex_count"]))
                self.assertEqual((folder / "frames.bin").stat().st_size, 16 + frames * corners * 12)
                positions = np.memmap(folder / "frames.bin", dtype="<f4", mode="r", offset=16)
                self.assertTrue(np.all(np.isfinite(positions)))
                self.assertEqual(corners, len(mesh["vertices"]))
                self.assertEqual(corners, len(mesh["indices"]))
                self.assertEqual(corners, len(mesh["triangles"]) * 3)
                self.assertEqual(mesh["indices"], list(range(corners)))
                for draw in mesh["draw_calls"]:
                    self.assertLessEqual(draw["start_index"] + draw["index_count"], corners)
                with Image.open(folder / "preview.png") as preview:
                    self.assertEqual(preview.size, (112, 112))

    def test_original_texture_content_hashes(self):
        for path in (self.output / "textures").glob("*.png"):
            with Image.open(path) as image:
                self.assertEqual(image.mode, "RGBA")
                expected = hashlib.sha256(struct.pack("<II", *image.size) + image.tobytes()).hexdigest()
                self.assertEqual(path.stem, expected)

    def test_crash_crate_fruit_are_accounted(self):
        level = next(level for level in self.manifest["levels"] if level["id"] == 11)
        names = {pair["geo"] for pair in level["pairs"]}
        self.assertIn("Cr10G", names)
        self.assertIn("BA10G", names)
        # Fruit can be emitted by GOOL as sprites; keep every declaration visible.
        self.assertTrue(names.isdisjoint(set(level["tgeo_without_pair"])))

    def test_gool_quad_size_uses_rectangle_count_at_eight(self):
        archive = cb3.load_level(11)
        pairs, issues, declarations = export_object_graphics.discover_pairs(archive)
        fruit = [item for item in declarations if item["gool"] == "FruiC"]
        self.assertEqual([(item["kind"], item["frames"], item["rectangles"]) for item in fruit],
                         [(2, 14, 1), (2, 1, 1), (5, 3, 2), (5, 1, 4)])
        self.assertIn(("WD10G", "WD10V"), pairs)

    def test_sprite_and_quad_all_archive_inventory(self):
        manifest = json.loads((self.output / "sprite_manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(sorted(level["id"] for level in manifest["levels"]), cb3.all_level_ids())
        for level in manifest["levels"]:
            for record in level["records"]:
                self.assertEqual(len(record["regions"]),record["frames"]*record["rectangles"])
                for region in record["regions"]:
                    self.assertTrue("file" in region or "error" in region)
                    if "file" in region:
                        self.assertIn(region["texture_provenance"]["source_level"],cb3.all_level_ids())
                        with Image.open(self.output / region["file"]) as image:
                            self.assertEqual(image.size,(region["width"],region["height"]))
        level = next(level for level in manifest["levels"] if level["id"] == 11)
        fruit = next(record for record in level["records"] if record["gool"] == "FruiC" and record["offset"] == 0)
        self.assertEqual(len(fruit["regions"]),14)
        self.assertTrue(all("file" in region for region in fruit["regions"]))

    def test_global_texture_ambiguity_is_rejected(self):
        from types import SimpleNamespace
        archive = SimpleNamespace(level_id=11,texture=lambda name:None)
        pool = {"Single":{"hash-a":{"data":b"pixel-a","source_levels":[2,11]}},
                "Conflict":{"hash-a":{"data":b"pixel-a","source_levels":[2]},
                            "hash-b":{"data":b"pixel-b","source_levels":[11]}}}
        data, provenance = graphics_texture_pool.resolve(pool,archive,"Single")
        self.assertEqual(data,b"pixel-a")
        self.assertEqual(provenance["resolution"],"unique_global_payload")
        self.assertEqual(provenance["source_levels"],[2,11])
        data, provenance = graphics_texture_pool.resolve(pool,archive,"Conflict")
        self.assertIsNone(data)
        self.assertEqual(provenance["resolution"],"ambiguous_global_name")
        self.assertEqual(provenance["distinct_payloads"],2)
        archive.texture = lambda name:SimpleNamespace(data=b"local-pixel")
        data, provenance = graphics_texture_pool.resolve(pool,archive,"Conflict")
        self.assertEqual(data,b"local-pixel")
        self.assertEqual(provenance["resolution"],"local_archive")

    def test_global_entry_ambiguity_is_rejected(self):
        from types import SimpleNamespace
        archive = SimpleNamespace(level_id=11,entry=lambda name:None)
        entry = SimpleNamespace(name="FrameV",type=1,items=[b"frame"])
        pool = {("FrameV",1):{"hash-a":{"entry":entry,"source_levels":[2,11]}}}
        resolved, provenance = export_object_graphics.resolve_entry(pool,archive,"FrameV",1)
        self.assertIs(resolved,entry)
        self.assertEqual(provenance["source_levels"],[2,11])
        pool[("FrameV",1)]["hash-b"] = {"entry":entry,"source_levels":[3]}
        resolved, provenance = export_object_graphics.resolve_entry(pool,archive,"FrameV",1)
        self.assertIsNone(resolved)
        self.assertEqual(provenance["resolution"],"ambiguous_global_name")
        archive.entry = lambda name:SimpleNamespace(type=2)
        resolved, provenance = export_object_graphics.resolve_entry(pool,archive,"FrameV",1)
        self.assertIsNone(resolved)
        self.assertEqual(provenance["resolution"],"wrong_local_type")


if __name__ == "__main__":
    unittest.main()
