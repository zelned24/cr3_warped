"""Validate the exact CWT parser used by the console and GPU texture tiling."""
from pathlib import Path
import ctypes
import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import export_world_textures as exporter

class WorldGraphics(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(prefix='crash-cwt-')
        folder=Path(cls.tmp.name)
        source=folder/'parser.c'
        source.write_text('#include "cwt_format.h"\n__declspec(dllexport) int validate(const unsigned char*p,unsigned n,unsigned id){return cwt_validate(p,n,id);}\n')
        library=folder/('parser.dll' if os.name=='nt' else 'parser.so')
        subprocess.run([sys.executable,'-m','ziglang','cc','-shared','-O3','-ffast-math','-fno-finite-math-only','-Wall','-Wextra','-Werror','-I'+str(ROOT/'source'),str(source),'-o',str(library)],check=True)
        cls.lib=ctypes.CDLL(str(library));cls.lib.validate.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_uint]
        cls.data=(ROOT/'game_data/world/lv_0B.cwt').read_bytes()
    @classmethod
    def tearDownClass(cls):
        # Windows retains the loaded DLL until process shutdown.
        if os.name=='nt':
            import _ctypes
            _ctypes.FreeLibrary(cls.lib._handle)
        cls.tmp.cleanup()
    def valid(self,data,id=11):
        buf=ctypes.create_string_buffer(bytes(data));return self.lib.validate(buf,len(data),id)==0
    def test_all_exported_levels(self):
        files=list((ROOT/'game_data/world').glob('lv_*.cwt'))
        manifest=json.loads((ROOT/'game_data/world/manifest.json').read_text())
        self.assertEqual(len(manifest),34)
        self.assertEqual({int(path.stem[3:],16)for path in files},{item['level']for item in manifest})
        for path in files:
            with self.subTest(path=path.name):self.assertTrue(self.valid(path.read_bytes(),int(path.stem[3:],16)))
    def test_character_and_prop_surfaces(self):
        for name,id in (("crash",0xCB3),("wood",0xCB4),("tnt",0xCB5),("nitro",0xCB6),("checkpoint",0xCB7),("wumpa",0xCB8),("spring",0xCB9),("bounce",0xCBA)):
            data=(ROOT/f'game_data/characters/{name}.cwt').read_bytes()
            with self.subTest(asset=name):
                self.assertTrue(self.valid(data,id))
                if name=="crash":self.assertEqual(struct.unpack_from('<I',data,12)[0],1722)
    def test_header_corruption(self):
        for offset,value in ((0,0),(4,9),(8,99),(12,0xffffffff),(20,0xffffffff),(24,0),(28,3),(32,0xffffffff),(36,0),(40,0xffffffff),(44,0),(48,1)):
            bad=bytearray(self.data);struct.pack_into('<I',bad,offset,value)
            with self.subTest(offset=offset):self.assertFalse(self.valid(bad))
    def test_truncation_and_bounds(self):
        for size in (0,1,63,64,len(self.data)-1):self.assertFalse(self.valid(self.data[:size]))
        offsets=struct.unpack_from('<4I',self.data,28);vbo,ibo,batch,texture=offsets
        for offset,value in ((vbo,0x7fc00000),(batch,0xffffffff),(batch+4,0xffffffff),(batch+8,0xffffffff),(batch+12,1),(batch+16,0xffffffff),(batch+20,2),(texture,7),(texture+8,0),(texture+12,1)):
            bad=bytearray(self.data);struct.pack_into('<I',bad,offset,value)
            with self.subTest(offset=offset):self.assertFalse(self.valid(bad))
        bad=bytearray(self.data);bad[ibo:ibo+2]=b'\xff\xff';self.assertFalse(self.valid(bad))
    def test_tiling_rectangular_and_padding(self):
        for w,h in ((1,1),(3,5),(17,9),(32,64),(256,8)):
            rgba=bytes(c for y in range(h)for x in range(w)for c in (x*13%256,y*27%256,(x+y)*9%256,255 if (x+y)%3 else 0))
            pw,ph=exporter.pot(w),exporter.pot(h)
            pixels=exporter.untile5551(exporter.tile5551(rgba,w,h,pw,ph),pw,ph)
            for y in range(h):
                for x in range(w):
                    src=rgba[(y*w+x)*4:(y*w+x)*4+4]
                    expected=bytes(((c>>3)<<3)|(c>>5)for c in src[:3])+bytes((255 if src[3]else 0,))
                    self.assertEqual(pixels[(y*pw+x)*4:(y*pw+x)*4+4],expected)

if __name__=='__main__':unittest.main()
