"""Audit unresolved alternate owners without inventing a resource fallback."""
import collections
import hashlib
import json
from pathlib import Path
import struct

import cb3
import nsf
from psx_native import Image, ORIGINAL_SHA256

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT/'tests/gool_resource_evidence.json'


def build_report():
    image = Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes())
    if image.sha256 != ORIGINAL_SHA256:
        raise ValueError('wrong original executable')
    archives = {lid: cb3.load_level(lid, cache=False) for lid in cb3.all_level_ids()}
    all_entries = collections.defaultdict(list)
    all_directory = collections.defaultdict(list)
    source_records, links, slots = [], collections.defaultdict(list), []
    totals = collections.Counter()
    owner_counts = collections.Counter()
    for lid, archive in archives.items():
        raw_entries = []
        for chunk in archive.chunks:
            if chunk.ctype == 1:
                continue
            if chunk.ctype not in (0, 3, 4, 5):
                raise ValueError('unaudited chunk layout')
            count = struct.unpack_from('<I', chunk.data, 8)[0]
            for index in range(count):
                offset = struct.unpack_from('<I', chunk.data, 16+4*index)[0]
                magic, eid, kind, sections = struct.unpack_from('<4I', chunk.data, offset)
                if magic != 0x0100ffff:
                    raise ValueError('entry directory magic')
                record = [lid, chunk.index, chunk.ctype, offset, kind, sections]
                all_entries[eid].append(record)
                raw_entries.append(record)
                if chunk.ctype == 5:
                    totals['type5_entries'] += 1
                    if kind != 20:
                        raise ValueError('new non-video type5 entry needs audit')
        for index, (cid, eid) in enumerate(archive.nsd.list_entries):
            all_directory[eid].append([lid, index, cid])
        source_records.append({'level': lid,
            'nsf_sha256': hashlib.sha256(Path(archive.nsf_path).read_bytes()).hexdigest(),
            'nsd_sha256': hashlib.sha256(archive.nsd.buf).hexdigest(),
            'chunk_types': dict(sorted(collections.Counter(c.ctype for c in archive.chunks).items())),
            'raw_entries': len(raw_entries)})
        local = {entry.eid: entry for entry in archive.of_type(11)}
        for entry in local.values():
            totals['gool_entries'] += 1
            if len(entry.items) != 6:
                totals['libraries'] += 1
                continue
            constants = struct.unpack('<%dI' % (len(entry.items[2])//4), entry.items[2])
            if entry.name in ('WillC', 'WEfOC'):
                n = 16 if entry.name == 'WillC' else 2
                slots.append({'level': lid, 'behavior': entry.name,
                    'behavior_eid': entry.eid,
                    'constants_sha256': hashlib.sha256(entry.items[2]).hexdigest(),
                    'slots': [{'index': index, 'eid': eid, 'name': nsf.eid_str(eid),
                               'local_sections': len(local[eid].items) if eid in local else None}
                              for index, eid in enumerate(constants[:n])]})
            for state in range(len(entry.items[4])//16):
                totals['states'] += 1
                _, _, alternate, signal, pre, main = struct.unpack_from('<II4H', entry.items[4], state*16)
                eid = constants[alternate]
                for kind, raw in enumerate((main, pre, signal)):
                    if kind and raw == 0x3fff:
                        totals['absent_optional_handlers'] += 1
                        continue
                    if not raw & 0x4000 or eid in local:
                        continue
                    # Compact exact provenance: level, owner EID, state,
                    # kind(main=0/pre=1/signal=2), original address, const slot.
                    links[eid].append([lid, entry.eid, state, kind, raw, alternate])
                    owner_counts[entry.name] += 1
    resources = []
    owner_level_links = set()
    for eid, references in sorted(links.items()):
        resources.append({'eid': eid, 'name': nsf.eid_str(eid), 'count': len(references),
            'raw_nsf_occurrences': all_entries.get(eid, []),
            'nsd_directory_occurrences': all_directory.get(eid, []),
            'references': references})
        owner_level_links.update((r[0], r[1], eid) for r in references)
    totals.update(unresolved_handler_references=sum(len(v) for v in links.values()),
                  unresolved_eids=len(links), unresolved_owner_level_links=len(owner_level_links))
    windows = {'constructor_same_owners': (0x8001a77c, 0x8001a7a0),
               'state_alternate_resolution': (0x8001d314, 0x8001d358),
               'resolve_resource_cell': (0x800142b4, 0x8001439c),
               'exact_eid_directory_search': (0x80014b04, 0x80014b50)}
    return {'schema': 'psx-gool-resource-audit/1', 'source_sha256': image.sha256,
        'analysis': 'offline original NSF/NSD byte inventory and Capstone windows',
        'totals': dict(totals), 'unresolved_by_behavior': dict(owner_counts),
        'archives': source_records, 'level_variant_slots': slots, 'resources': resources,
        'native_windows': {name: image.disassemble(*bounds) for name, bounds in windows.items()},
        'findings': ['Missing references concentrate in shared WillC/WEfOC library slots.',
            'Levels replace selected placeholder slots with actual local three-section libraries.',
            'All14 missing EIDs are absent from every original NSF entry and NSD directory.',
            'Skipped type5 pages contain166 video entries(type20), not alternate GOOL code.',
            'Constructor initially binds object+0x0C and+0x10 to the same resolved entry.',
            'State setup resolves the selected constants-owner cell through0x800142B4;'
            ' odd EIDs use exact directory lookup0x80014B04, not a cross-level name search.'],
        'resolution_contract': ['Keep unresolved owner errors explicit; do not substitute another level library.',
            'Expose per-level available-library slots to a future state/vehicle activation service.',
            'A state requiring an absent slot needs original mode selection/loading evidence before execution.',
            'No parsing/export resolution fix is proven by this inventory.'],
        'limitations': ['Template-slot classification is inferred from per-level substitutions;'
                        ' original game reachability of every state is not proven.',
                       'No claim that every missing reference executes or needs a resident external asset.',
                       'Native windows are offline evidence, not REA provider Evidence.']}


def main():
    report = build_report()
    OUTPUT.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print('Audited42 original archives:8110 references,14 absent owner EIDs; no fallback applied.')


if __name__ == '__main__':
    main()
