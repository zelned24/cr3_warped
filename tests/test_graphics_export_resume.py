"""An interrupted inventory may resume only with the same decoder sources."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import export_object_graphics as exporter


class GraphicsExportResume(unittest.TestCase):
    def inventory(self, old_fingerprint):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "objects"
            folder = output / "models" / "source-pair"
            folder.mkdir(parents=True)
            (folder / "mesh.json").write_text("{}", encoding="utf-8")
            (folder / "frames.bin").write_bytes(b"existing-frame-data")
            previous = {"models": {"identity": {"path": "models/source-pair"}}}
            if old_fingerprint is not None:
                previous["decoder_fingerprint"] = old_fingerprint
            (output / "manifest.json").write_text(json.dumps(previous), encoding="utf-8")
            with patch.object(exporter, "decoder_fingerprint", return_value="current"), \
                 patch.object(exporter, "build_entry_pool", return_value={}), \
                 patch.object(exporter, "build_pool", return_value={}), \
                 patch.object(exporter.cb3, "all_level_ids", return_value=[]), \
                 contextlib.redirect_stdout(io.StringIO()):
                return exporter.export_all(output, Path(temporary) / "contacts")

    def test_same_decoder_preserves_completed_models(self):
        result = self.inventory("current")
        self.assertEqual(result["totals"]["unique_assets"], 1)

    def test_changed_decoder_does_not_claim_old_meshes_as_current(self):
        result = self.inventory("older-decoder")
        self.assertEqual(result["totals"]["unique_assets"], 0)
        self.assertEqual(result["decoder_fingerprint"], "current")

    def test_legacy_inventory_requires_reexport(self):
        self.assertEqual(self.inventory(None)["models"], {})


if __name__ == "__main__":
    unittest.main()
