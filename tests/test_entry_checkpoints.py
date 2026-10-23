"""Pure Python asset/source evidence. Does not execute production C."""
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from inspect_entry_checkpoints import checkpoints


class EntryCheckpointEvidence(unittest.TestCase):
    def test_retail_toad_entry_and_identity(self):
        rows = checkpoints((ROOT / "game_data/levels.bin").read_bytes())
        toad = next(row for row in rows if row["level"] == 11)
        self.assertTrue(toad["has_checkpoint"])
        self.assertEqual(toad["raw_position"], [12427939, 819200, 104243200])
        self.assertEqual(toad["position"], [3034.164794921875, 200.0, 25450.0])
        self.assertEqual(toad["record_index"], 0)
        self.assertNotEqual(toad["zdat_eid"], 0)

    def test_all_exported_checkpoints_equal_raw_nsd(self):
        import cb3
        checked = 0
        for row in checkpoints((ROOT / "game_data/levels.bin").read_bytes()):
            if not row["has_checkpoint"]:
                continue
            archive = cb3.load_level(row["level"])
            nsd = archive.nsd
            base = 0x520 + nsd.num_list_entries * 8
            self.assertGreater(struct.unpack_from("<I", nsd.buf, base)[0], 0)
            eid, reserved, index, x, y, z = struct.unpack_from("<IIIiii", nsd.buf, base + 0x2DC)
            self.assertEqual([x, y, z], row["raw_position"])
            self.assertEqual((eid, reserved, index), (row["zdat_eid"], row["reserved"], row["record_index"]))
            checked += 1
        self.assertGreaterEqual(checked, 30)

    def test_invalid_blob_rejected(self):
        for blob in (b"", b"CBWB" + bytes(12), b"nope" + bytes(40)):
            if blob[:4] == b"CBWB":
                self.assertEqual(checkpoints(blob), [])
            else:
                with self.assertRaises(ValueError):
                    checkpoints(blob)
        blob = b"CBWB" + struct.pack("<III", 1, 65, 0)
        with self.assertRaises(ValueError):
            checkpoints(blob)

    def test_runtime_preserves_opaque_identity(self):
        source = (ROOT / "source/level.c").read_text()
        self.assertIn("read_u32(metadata + 20)", source)
        self.assertIn("read_u32(metadata + 28)", source)
        self.assertIn("level_entry_checkpoint_get", source)
        self.assertNotIn("mi < 3 && mi < s_model_count", source)
        self.assertIn("for (int mi = 0; mi < s_model_count; mi++)", source)
        self.assertIn("ground >= y-max_drop", source)
        self.assertIn("!isfinite(y+max_rise)", source)


if __name__ == "__main__":
    unittest.main()
