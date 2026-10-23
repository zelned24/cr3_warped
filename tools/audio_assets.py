"""Read original NSF audio chunks without changing the generic NSF parser.

Inventory: python tools/audio_assets.py inventory --out docs/evidence/audio_inventory.json
Extraction (one clip, raw ADPCM + PCM16; no guessed pitch):
  python tools/audio_assets.py extract --level 0x0b --name SAMPLE --out NEW_DIR
Optional --sample-rate writes a WAV at a caller-selected diagnostic rate.
No sample/event mapping, sequencer, ADSR or console playback is implemented here.
"""
from collections import Counter
from dataclasses import dataclass
import argparse
import hashlib
import json
from pathlib import Path
import struct
import wave

import cb3
import nsf
from psx_adpcm import AdpcmError, decode_stream, inspect_stream


class AudioAssetError(ValueError):
    pass


@dataclass(frozen=True)
class AudioEntry:
    eid: int
    type: int
    offset: int
    item_offsets: tuple
    items: tuple
    chunk_index: int
    cid: int

    @property
    def name(self):
        return nsf.eid_str(self.eid)


def parse_audio_chunk(page, *, chunk_index=0):
    """Validate an entire decompressed audio page before returning entries."""
    page = bytes(page)
    if len(page) != 65536:
        raise AudioAssetError('audio page must contain exactly 65536 bytes')
    magic, ctype, cid, count = struct.unpack_from('<HHII', page)
    if magic != 0x1234 or ctype not in (3, 4, 5):
        raise AudioAssetError('not a supported decompressed audio page')
    if count > (len(page) - 20) // 4:
        raise AudioAssetError('entry count exceeds page bounds')
    directory_end = 16 + (count + 1) * 4
    offsets = struct.unpack_from('<%dI' % (count + 1), page, 16)
    if offsets[0] < directory_end or offsets[-1] > len(page):
        raise AudioAssetError('audio directory overlaps or exceeds page')
    result = []
    for offset, end in zip(offsets, offsets[1:]):
        if offset + 20 > end:
            raise AudioAssetError('unordered or truncated entry span')
        entry_magic, eid, etype, items = struct.unpack_from('<IIII', page, offset)
        if entry_magic != 0x0100FFFF:
            raise AudioAssetError('invalid audio entry magic')
        if etype != {3: 12, 4: 14, 5: 20}[ctype]:
            raise AudioAssetError('unexpected entry type in audio page')
        if items > (end - offset - 20) // 4:
            raise AudioAssetError('item count exceeds entry bounds')
        header_end = 16 + (items + 1) * 4
        item_offsets = struct.unpack_from('<%dI' % (items + 1), page, offset + 16)
        if item_offsets[0] < header_end or item_offsets[-1] > end - offset:
            raise AudioAssetError('audio item overlaps header or exceeds entry')
        if any(a > b for a, b in zip(item_offsets, item_offsets[1:])):
            raise AudioAssetError('unordered audio item directory')
        payloads = tuple(page[offset + a:offset + b]
                         for a, b in zip(item_offsets, item_offsets[1:]))
        result.append(AudioEntry(eid, etype, offset, tuple(item_offsets), payloads,
                                 chunk_index, cid))
    return tuple(result)


def audio_entries(archive):
    for chunk in archive.chunks:
        if chunk.ctype in (3, 4, 5):
            yield from parse_audio_chunk(chunk.data, chunk_index=chunk.index)


def inspect_vab_header(data):
    """Inspect a standard VAB header; do not join unknown body fragments."""
    data = bytes(data)
    if len(data) < 32 or data[:4] != b'pBAV':
        raise AudioAssetError('not a standard little-endian VAB header')
    version, bank_id, declared_size = struct.unpack_from('<III', data, 4)
    programs, tones, samples = struct.unpack_from('<HHH', data, 18)
    if programs > 128 or tones > 2048 or samples > 255:
        raise AudioAssetError('VAB count outside standard table bounds')
    table_offset = 32 + 128 * 16 + programs * 16 * 32
    if table_offset + 512 != len(data):
        raise AudioAssetError('VAB header does not match declared program table size')
    sizes = struct.unpack_from('<256H', data, table_offset)
    if sizes[0] != 0:
        raise AudioAssetError('unsupported nonzero reserved VAB sample slot')
    sample_sizes = [size * 8 for size in sizes[1:samples + 1]]
    return dict(version=version, bank_id=bank_id, declared_size=declared_size,
                programs=programs, tones=tones, samples=samples,
                sample_sizes=sample_sizes, sample_bytes=sum(sample_sizes))


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def inventory_archive(archive):
    rows, errors = [], []
    types, chunk_types = Counter(), Counter(chunk.ctype for chunk in archive.chunks)
    for entry in audio_entries(archive):
        types[entry.type] += 1
        row = dict(name=entry.name, eid='0x%08x' % entry.eid, type=entry.type,
                   chunk=entry.chunk_index, cid=entry.cid, page_offset=entry.offset,
                   item_offsets=list(entry.item_offsets), sizes=[len(x) for x in entry.items],
                   item_sha256=[sha256(x) for x in entry.items])
        if entry.type == 12:
            if len(entry.items) != 1:
                raise AudioAssetError('sample entry has an unexpected item count')
            try:
                info = inspect_stream(entry.items[0])
                row['adpcm'] = info.__dict__
            except AdpcmError as exc:
                row['error'] = str(exc)
                errors.append(dict(name=entry.name, chunk=entry.chunk_index, error=str(exc)))
        rows.append(row)
    banks = []
    for entry in archive.of_type(13):
        row = dict(name=entry.name, sizes=[len(item) for item in entry.items],
                   item_sha256=[sha256(item) for item in entry.items],
                   sequences=[i for i, item in enumerate(entry.items) if item[:4] == b'pQES'])
        row['vab'] = []
        for index, item in enumerate(entry.items):
            if item[:4] == b'pBAV':
                try:
                    row['vab'].append(dict(item=index, **inspect_vab_header(item)))
                except AudioAssetError as exc:
                    row.setdefault('errors', []).append(str(exc))
        banks.append(row)
    return dict(level=archive.level_id, source=str(archive.nsf_path),
                nsf_sha256=sha256(Path(archive.nsf_path).read_bytes()),
                chunk_types=dict(sorted(chunk_types.items())),
                audio_entry_types=dict(sorted(types.items())), entries=rows,
                music_entries=banks, sample_errors=errors)


def extract_sample(archive, name, output, *, sample_rate=None):
    """Extract an exact named sample once; output directory must be new."""
    if sample_rate is not None and (not isinstance(sample_rate, int)
                                   or not 1 <= sample_rate <= 192000):
        raise AudioAssetError('diagnostic sample rate must be in 1..192000')
    matches = [entry for entry in audio_entries(archive)
               if entry.type == 12 and entry.name == name]
    if len(matches) != 1:
        raise AudioAssetError('sample name must match exactly one audio entry')
    entry = matches[0]
    if len(entry.items) != 1:
        raise AudioAssetError('sample entry has an unexpected item count')
    raw = entry.items[0]
    decoded = decode_stream(raw)
    metadata = dict(level=archive.level_id, name=name, eid=entry.eid,
                    nsf_sha256=sha256(Path(archive.nsf_path).read_bytes()),
                    page_index=entry.chunk_index, cid=entry.cid,
                    page_offset=entry.offset, item_offset=entry.item_offsets[0],
                    adpcm_sha256=sha256(raw), pcm_sha256=sha256(decoded.pcm),
                    stream=decoded.info.__dict__, diagnostic_sample_rate=sample_rate,
                    sample_rate_provenance='caller-selected' if sample_rate else 'unknown',
                    limitations='Raw mono sample only; no ADSR, pitch, event mapping or mix.')
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    (output / 'sample.adpcm').write_bytes(raw)
    (output / 'sample.pcm16le').write_bytes(decoded.pcm)
    if sample_rate:
        with wave.open(str(output / 'sample.wav'), 'wb') as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(sample_rate)
            wav.writeframes(decoded.pcm)
    (output / 'provenance.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    inv = sub.add_parser('inventory')
    inv.add_argument('--out', type=Path, required=True)
    ext = sub.add_parser('extract')
    ext.add_argument('--level', type=lambda value: int(value, 0), required=True)
    ext.add_argument('--name', required=True)
    ext.add_argument('--out', type=Path, required=True)
    ext.add_argument('--sample-rate', type=int)
    args = parser.parse_args()
    if args.command == 'extract':
        result = extract_sample(cb3.load_level(args.level), args.name, args.out,
                                sample_rate=args.sample_rate)
        print(json.dumps(result, indent=2))
    else:
        levels = [inventory_archive(cb3.load_level(lid, cache=False)) for lid in cb3.all_level_ids()]
        result = dict(format='crash3-original-audio-inventory-v1', levels=levels,
                      limitations='No SPU playback, event mapping, music sequencing or body-fragment reconstruction.')
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(dict(levels=len(levels), entries=sum(len(x['entries']) for x in levels),
                              samples=sum(x['audio_entry_types'].get(12, 0) for x in levels),
                              sample_errors=sum(len(x['sample_errors']) for x in levels))))


if __name__ == '__main__':
    main()
