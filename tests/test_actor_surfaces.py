"""Verify runtime CWT1 actor assets against source topology and bounds."""
import json
from pathlib import Path
import struct
import sys
import unittest

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import cb3
import tgeo
from export_world_textures import untile5551


class ActorSurfaces(unittest.TestCase):
    def test_runtime_actors(self):
        for index, name in enumerate(("crash", "wood", "tnt", "nitro", "checkpoint", "wumpa", "spring", "bounce", "aku_mask")):
            with self.subTest(name=name):
                path = ROOT / "game_data/characters" / (name + ".cwt")
                data = path.read_bytes()
                header = struct.unpack_from("<4s15I", data)
                magic, version, identifier, nv, ni, nb, nt, vo, io, bo, to, po, total, *_ = header
                self.assertEqual((magic, version, identifier), (b"CWT1", 1, 0xCB3 + index))
                self.assertEqual(total, len(data))
                self.assertEqual(nv, ni)
                self.assertEqual(nv % 3, 0)
                vertices = np.frombuffer(data, dtype="<f4", count=nv * 8, offset=vo).reshape(nv, 8)
                self.assertTrue(np.all(np.isfinite(vertices)))
                self.assertTrue(np.all(vertices[:, 6:] >= 0))
                self.assertTrue(np.all(vertices[:, 6:] <= 1))
                self.assertEqual(list(struct.unpack_from("<%dH" % ni, data, io)), list(range(nv)))
                if name in ("crash", "aku_mask"):
                    self.assertEqual(nv, 1722 if name == "crash" else 81)
                    arc = cb3.load_level(11)
                    geo,vtx=("Cr10G","Cr10V") if name=="crash" else ("WD10G","WD10V")
                    mesh = tgeo.build_mesh(tgeo.parse_tgeo(arc.entry(geo)), arc.entry(vtx))
                    np.testing.assert_allclose(vertices[:, :3], [v["pos"] for v in mesh["vertices"]], rtol=1e-6)
                elif name != "wumpa":
                    np.testing.assert_allclose(vertices[:, :3].min(axis=0), [-.5, 0, -.5], atol=1e-6)
                    np.testing.assert_allclose(vertices[:, :3].max(axis=0), [.5, 1, .5], atol=1e-6)
                    report=json.loads(path.with_name(name+'_surface.json').read_text())
                    arc=cb3.load_level(11)
                    source=tgeo.build_mesh(tgeo.parse_tgeo(arc.entry(report['source_geo'])),arc.entry(report['source_svtx']))
                    expected=np.array([v['pos'] for v in source['vertices']],dtype=np.float64)
                    lo,hi=np.array(source['bbox']['min']),np.array(source['bbox']['max'])
                    origin=(lo+hi)/2;origin[1]=lo[1]
                    expected=(expected-origin)/np.maximum(hi-lo,1e-6)
                    np.testing.assert_allclose(vertices[:,:3],expected,rtol=1e-6,atol=1e-6)
                else:
                    self.assertEqual(nv, 84)
                    self.assertEqual(nb,14)
                    self.assertEqual(struct.unpack_from("<I",data,52)[0],14)
                    self.assertTrue(np.all(vertices[:, 2] == 0))
                covered = []
                for batch in range(nb):
                    base, count, first, length, material, *_ = struct.unpack_from("<8I6f", data, bo + batch * 56)
                    self.assertEqual((base, count), (0, nv))
                    self.assertLess(material, nt)
                    self.assertLessEqual(first + length, ni)
                    covered.extend(range(first, first + length))
                self.assertEqual(covered, list(range(ni)))
                for texture in range(nt):
                    width, height, offset, length, *_ = struct.unpack_from("<8I", data, to + texture * 32)
                    self.assertEqual(length, width * height * 2)
                    self.assertLessEqual(offset + length, len(data))
                    self.assertGreaterEqual(offset, po)
                    self.assertGreaterEqual(width, 8)
                    self.assertEqual(width & (width - 1), 0)
                    self.assertEqual(height & (height - 1), 0)
                    self.assertEqual(len(untile5551(data[offset:offset + length], width, height)), width * height * 4)


if __name__ == "__main__":
    unittest.main()
