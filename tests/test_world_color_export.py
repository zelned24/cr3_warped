"""Exercise exporter -> CWT -> tiled texel -> documented TEV color equation.

No target/host compilation and no generated game assets are written.
"""
from pathlib import Path
import struct
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import cb3
import export_world_textures as exporter
import wgeo


class WorldColorExport(unittest.TestCase):
    def test_color_only_and_textured_materials_have_distinct_gain(self):
        colors = ((32, 96, 160), (128, 200, 255), (255, 0, 64))
        vertices = [dict(x=i, y=0, z=0, u=0, v=0,
                         r=rgb[0], g=rgb[1], b=rgb[2])
                    for i, rgb in enumerate(colors)] * 2
        mesh = {"vertices": vertices, "indices": list(range(6)), "draw_calls": [
            dict(tex_anim_index=-1, texture_index=-1, start_index=0, index_count=3),
            dict(tex_anim_index=-1, texture_index=0, start_index=3, index_count=3,
                 texture_name="page", page=0, clut=0)]}
        model = SimpleNamespace(build_mesh=lambda: mesh, origin=(0, 0, 0),
                                bounds_ref=lambda: [0, 0, 0, 2, 0, 0], is_skybox=False)
        archive = SimpleNamespace(of_type=lambda kind: [object()],
                                  texture=lambda name: SimpleNamespace(data=b"source"))
        image = SimpleNamespace(rgba=bytes((128, 64, 32, 255)), width=1, height=1,
                                transparent_pixels=0, xlu_pixels=0, blend=0)
        with patch.object(exporter.cb3, "load_level", return_value=archive), \
             patch.object(exporter.wgeo, "Wgeo", return_value=model), \
             patch.object(exporter.tpag, "decode_texture", return_value=image):
            data, report = exporter.build_level(11)
        vo, _, bo, to = struct.unpack_from("<4I", data, 28)
        for batch in range(2):
            base, count, _, _, material = struct.unpack_from("<5I", data, bo + batch * 56)
            self.assertEqual(count, 3)
            width, height, off, length = struct.unpack_from("<4I", data, to + material * 32)
            pixel = exporter.untile5551(data[off:off + length], width, height)[:3]
            for vertex, source in enumerate(colors):
                rgb = struct.unpack_from("<3f", data, vo + (base + vertex) * 32 + 12)
                rendered = [min(1, pixel[channel] / 255 * rgb[channel] * 2)
                            for channel in range(3)]
                expected = [source[channel] / 255 if batch == 0 else
                            min(1, source[channel] / 255 * pixel[channel] / 255 * 2)
                            for channel in range(3)]
                for actual, value in zip(rendered, expected):
                    self.assertAlmostEqual(actual, value, places=6)
        self.assertIn("authored RGB", report["untextured_color"])

    @unittest.skipUnless(Path(cb3.ISO).is_dir(), "retail archives unavailable")
    def test_retail_has_authored_color_only_world_geometry(self):
        # Verify that the affected path exists in supplied original data,
        # rather than relying on a purely invented material fixture.
        affected = triangles = 0
        for entry in cb3.load_level(0x0B).of_type(3):
            mesh = wgeo.Wgeo(entry).build_mesh()
            for call in mesh["draw_calls"]:
                if call["tex_anim_index"] < 0 and call["texture_index"] < 0:
                    affected += 1
                    triangles += call["index_count"] // 3
        self.assertGreater(affected, 0)
        self.assertGreater(triangles, 0)


if __name__ == "__main__":
    unittest.main()
