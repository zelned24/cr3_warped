"""Offline byte/provenance checks. Never execute or compile game code."""
from pathlib import Path
import hashlib
import json
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from psx_native import Image,ORIGINAL_SHA256,signal_evidence
import cb3


class PsxNative(unittest.TestCase):
    def fixture(self):
        blob=bytearray(2064);blob[:8]=b'PS-X EXE'
        struct.pack_into('<I',blob,16,0x80010000)
        struct.pack_into('<II',blob,24,0x80010000,16)
        struct.pack_into('<I',blob,2048,0x24020001)
        return blob

    def test_header_mapping_and_file_bounds(self):
        blob=self.fixture();image=Image.parse(blob)
        self.assertEqual(image.word(0x80010000),0x24020001)
        self.assertEqual(image.read(0x80010010,0),b'')
        self.assertEqual(image.sha256,hashlib.sha256(blob).hexdigest())
        for address,size in ((0x8000ffff,1),(0x80010010,1),(0x80010000,-1)):
            with self.assertRaises(ValueError):image.read(address,size)

    def test_invalid_header_extent_and_entry_are_rejected(self):
        for offset,value in ((16,0x80010010),(16,0x80010001),(24,0x80010001),
                             (28,0),(28,17),(28,20),(24,0x80200000)):
            blob=self.fixture();struct.pack_into('<I',blob,offset,value)
            with self.assertRaises(ValueError):Image.parse(blob)
        with self.assertRaises(ValueError):Image.parse(b'PS-X EXE')
        blob=self.fixture();blob[0]=0
        with self.assertRaises(ValueError):Image.parse(blob)

    def test_disassembly_preserves_selected_bytes_and_mapping(self):
        image=Image.parse(self.fixture());window=image.disassemble(image.load,image.load+16)
        self.assertTrue(window['complete_window'])
        self.assertEqual(window['file_offset'],2048)
        self.assertEqual(bytes.fromhex(window['raw_hex']),image.data)
        self.assertEqual(window['instructions'][0]['mnemonic'],'addiu')
        self.assertEqual(window['instructions'][0]['operands'],'$v0, $zero, 1')
        for start,end in ((image.load+1,image.load+4),(image.load,image.load),
                          (image.load,image.load+20)):
            with self.assertRaises(ValueError):image.disassemble(start,end)

    def test_audit_requires_exact_original_image(self):
        with self.assertRaises(ValueError):signal_evidence(Image.parse(self.fixture()))

    def test_original_evidence_matches_every_retained_byte(self):
        image=Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes())
        self.assertEqual(image.sha256,ORIGINAL_SHA256)
        report=json.loads((ROOT/'docs/evidence/psx_signal_native.json').read_text(encoding='utf-8'))
        self.assertEqual(report['source_sha256'],image.sha256)
        for window in report['windows'].values():
            blob=image.read(window['start'],window['end']-window['start'])
            self.assertEqual(bytes.fromhex(window['raw_hex']),blob)
            self.assertTrue(window['complete_window'])
            self.assertEqual(b''.join(bytes.fromhex(i['bytes']) for i in window['instructions']),blob)
        table={r['opcode']:r['address'] for r in report['dispatch_table']['entries']}
        self.assertEqual(table[0x3c],0x80039b94)
        self.assertEqual((table[0x2a],table[0x4e]),(0x80039724,0x80039754))
        self.assertEqual((table[0x3d],table[0x3e]),(0x80039bb8,0x80039bb8))
        # Direct native instructions: base behavior, PC mask, base code pointer.
        self.assertEqual(image.word(0x8001dcc8),0x8c82000c)
        self.assertEqual(image.word(0x8001dccc),0x32233fff)
        self.assertEqual(image.word(0x8001dcd0),0x8c420014)
        self.assertEqual(image.word(0x8001d5d4),0xace30004) # third frame word store


if __name__=='__main__':unittest.main()
