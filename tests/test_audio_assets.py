"""Bounded original audio extraction checks; no compilation or NDSP calls."""
from pathlib import Path
from types import SimpleNamespace
import json
import struct
import sys
import tempfile
import unittest
import wave

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import audio_assets as audio
import cb3
from psx_adpcm import decode_stream


def fixture(ctype=3, etype=12, payload=bytes((12, 1)) + b'\x21' * 14):
    page = bytearray(65536)
    struct.pack_into('<HHIII', page, 0, 0x1234, ctype, 7, 1, 0)
    struct.pack_into('<II', page, 16, 32, 56 + len(payload))
    struct.pack_into('<IIIIII', page, 32, 0x0100FFFF, 0x4CB22049, etype, 1, 24, 24 + len(payload))
    page[56:56 + len(payload)] = payload
    return page


class AudioAssets(unittest.TestCase):
    def test_sample_page_preserves_exact_payload_identity_and_offsets(self):
        raw = fixture()
        entry, = audio.parse_audio_chunk(raw, chunk_index=11)
        self.assertEqual((entry.type, entry.offset, entry.chunk_index, entry.cid), (12, 32, 11, 7))
        self.assertEqual(entry.items, (bytes(raw[56:72]),))
        self.assertEqual(entry.item_offsets, (24, 40))

    def test_every_supported_page_type_checks_matching_entry_type(self):
        for ctype, etype in ((3, 12), (4, 14), (5, 20)):
            self.assertEqual(audio.parse_audio_chunk(fixture(ctype, etype))[0].type, etype)
            with self.assertRaises(audio.AudioAssetError):
                audio.parse_audio_chunk(fixture(ctype, etype + 1))

    def test_malformed_directories_and_header_cannot_escape_page(self):
        patches = [(0, '<H', 0), (2, '<H', 0), (8, '<I', 0xFFFFFFFF),
                   (16, '<I', 4), (20, '<I', 65537), (20, '<I', 51),
                   (32, '<I', 0), (44, '<I', 0xFFFFFFFF),
                   (48, '<I', 4), (52, '<I', 41)]
        for offset, fmt, value in patches:
            raw = fixture()
            struct.pack_into(fmt, raw, offset, value)
            with self.subTest(offset=offset, value=value), self.assertRaises(audio.AudioAssetError):
                audio.parse_audio_chunk(raw)
        for size in (0, 16, 65535, 65537):
            with self.assertRaises(audio.AudioAssetError):
                audio.parse_audio_chunk(bytes(size))

    def test_item_directory_must_be_ordered(self):
        page = fixture()
        struct.pack_into('<I', page, 44, 2)
        struct.pack_into('<III', page, 48, 28, 27, 40)
        with self.assertRaises(audio.AudioAssetError):
            audio.parse_audio_chunk(page)

    def test_vab_table_sizes_are_bounded(self):
        raw = bytearray(32 + 128 * 16 + 512 + 512)
        raw[:4] = b'pBAV'
        struct.pack_into('<III', raw, 4, 7, 123, len(raw) + 48)
        struct.pack_into('<HHH', raw, 18, 1, 2, 2)
        struct.pack_into('<3H', raw, len(raw) - 512, 0, 2, 4)
        parsed = audio.inspect_vab_header(raw)
        self.assertEqual(parsed['sample_sizes'], [16, 32])
        self.assertEqual(parsed['sample_bytes'], 48)
        self.assertEqual(parsed['programs'], 1)
        for length in (0, 31, len(raw) - 1, len(raw) + 1):
            with self.assertRaises(audio.AudioAssetError):
                audio.inspect_vab_header(raw[:length] if length <= len(raw) else raw + b'\0')
        struct.pack_into('<H', raw, 18, 129)
        with self.assertRaises(audio.AudioAssetError):
            audio.inspect_vab_header(raw)

    def test_extraction_has_no_guessed_sample_rate_or_unsafe_filename(self):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            source = folder / 'source.nsf'
            source.write_bytes(b'fixture source')
            page = bytes(fixture())
            arc = SimpleNamespace(level_id=11, nsf_path=str(source), chunks=[
                SimpleNamespace(ctype=3, data=page, index=0)])
            name = audio.parse_audio_chunk(page)[0].name
            out = folder / 'extracted'
            metadata = audio.extract_sample(arc, name, out)
            self.assertEqual(metadata['sample_rate_provenance'], 'unknown')
            self.assertIsNone(metadata['diagnostic_sample_rate'])
            self.assertFalse((out / 'sample.wav').exists())
            self.assertEqual((out / 'sample.adpcm').read_bytes(), page[56:72])
            self.assertEqual(struct.unpack('<28h', (out / 'sample.pcm16le').read_bytes()), (1, 2) * 14)
            self.assertEqual(json.loads((out / 'provenance.json').read_text()), metadata)
            with self.assertRaises(FileExistsError):
                audio.extract_sample(arc, name, out)
            with self.assertRaises(audio.AudioAssetError):
                audio.extract_sample(arc, '../escape', folder / 'not-created')
            self.assertFalse((folder / 'not-created').exists())

    def test_wav_rate_is_explicit_diagnostic_metadata(self):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            source = folder / 'source.nsf'
            source.write_bytes(b'fixture source')
            page = bytes(fixture())
            arc = SimpleNamespace(level_id=11, nsf_path=str(source), chunks=[
                SimpleNamespace(ctype=3, data=page, index=0)])
            name = audio.parse_audio_chunk(page)[0].name
            metadata = audio.extract_sample(arc, name, folder / 'wav', sample_rate=32000)
            self.assertEqual(metadata['sample_rate_provenance'], 'caller-selected')
            with wave.open(str(folder / 'wav/sample.wav'), 'rb') as wav:
                self.assertEqual((wav.getframerate(), wav.getnframes(), wav.getnchannels()), (32000, 28, 1))
            for rate in (0, -1, 192001, 22050.5):
                with self.assertRaises(audio.AudioAssetError):
                    audio.extract_sample(arc, name, folder / 'bad', sample_rate=rate)

    def test_duplicate_sample_name_is_rejected(self):
        page = bytes(fixture())
        chunk = SimpleNamespace(ctype=3, data=page, index=0)
        arc = SimpleNamespace(chunks=[chunk, chunk])
        with self.assertRaises(audio.AudioAssetError):
            audio.extract_sample(arc, audio.parse_audio_chunk(page)[0].name, 'must-not-be-created')

    def test_real_original_audio_pages_and_vab_headers_when_available(self):
        if not Path(cb3.ISO).is_dir():
            self.skipTest('original archives not present')
        total, banks = 0, 0
        for lid in cb3.all_level_ids():
            arc = cb3.load_level(lid, cache=False)
            inventory = audio.inventory_archive(arc)
            self.assertEqual(inventory['sample_errors'], [], hex(lid))
            total += inventory['audio_entry_types'].get(12, 0)
            for entry in inventory['music_entries']:
                self.assertNotIn('errors', entry)
                banks += len(entry['vab'])
        self.assertEqual(total, 1977)
        self.assertEqual(banks, 39)

    def test_decodes_three_real_samples_with_finite_storage_bound(self):
        if not Path(cb3.ISO).is_dir():
            self.skipTest('original archives not present')
        archive = cb3.load_level(11, cache=False)
        entries = [entry for entry in audio.audio_entries(archive) if entry.type == 12][:3]
        self.assertEqual(len(entries), 3)
        for entry in entries:
            decoded = decode_stream(entry.items[0])
            self.assertTrue(decoded.ended)
            self.assertEqual(len(decoded.pcm), decoded.info.samples * 2)
            self.assertLessEqual(decoded.info.consumed_bytes, len(entry.items[0]))
            self.assertTrue(any(decoded.pcm), entry.name)


if __name__ == '__main__':
    unittest.main()
