"""Lossless, bounded per-level GOOL/ZDAT transport for a future native loader.

No bytecode is compiled or executed. Original section bytes and NSD metadata
are preserved; geometry, audio and texture pages remain separate resources.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

HEADER=struct.Struct('<4s7I')
RECORD=struct.Struct('<6I')
SECTION=struct.Struct('<2I')
LIMIT=32*1024*1024
MAX_ENTRIES=8192
MAX_SECTIONS=256
ROOT=Path(__file__).resolve().parents[1]


def encode(level_id,entries,nsd=b''):
    entries=list(entries)
    if not 0<=level_id<64 or len(entries)>MAX_ENTRIES:
        raise ValueError('invalid level or entry count')
    if len({e.eid for e in entries})!=len(entries):
        raise ValueError('duplicate EID within level')
    directory=HEADER.size
    cursor=directory+len(entries)*RECORD.size
    tables=[]
    for entry in entries:
        if entry.type not in (7,11) or len(entry.items)>MAX_SECTIONS:
            raise ValueError('invalid type or section count')
        tables.append(cursor)
        cursor+=len(entry.items)*SECTION.size
    nsd_offset=cursor
    cursor+=len(nsd)
    records=[]
    sections=[]
    payloads=[]
    for entry,table in zip(entries,tables):
        start=cursor
        for item in entry.items:
            sections.append((cursor,len(item)))
            payloads.append(item)
            cursor+=len(item)
        records.append((entry.eid,entry.type,len(entry.items),table,start,cursor-start))
    if cursor>LIMIT:raise ValueError('archive exceeds budget')
    data=bytearray(cursor)
    HEADER.pack_into(data,0,b'CGA1',1,level_id,len(entries),directory,nsd_offset,len(nsd),cursor)
    for index,record in enumerate(records):RECORD.pack_into(data,directory+index*RECORD.size,*record)
    table_cursor=directory+len(records)*RECORD.size
    for section in sections:
        SECTION.pack_into(data,table_cursor,*section)
        table_cursor+=SECTION.size
    data[nsd_offset:nsd_offset+len(nsd)]=nsd
    for (offset,size),payload in zip(sections,payloads):data[offset:offset+size]=payload
    # Protect metadata as well as payloads. CRC is corruption detection, not
    # authentication. SHA-256 provenance is recorded separately by the exporter.
    data+=struct.pack('<I',zlib.crc32(data))
    return bytes(data)


def decode(data):
    if len(data)<HEADER.size+4 or len(data)>LIMIT+4:
        raise ValueError('archive size')
    magic,version,lid,count,directory,nsd_offset,nsd_size,total=HEADER.unpack_from(data)
    if magic!=b'CGA1' or version!=1 or lid>=64 or count>MAX_ENTRIES:
        raise ValueError('archive header')
    if total+4!=len(data) or zlib.crc32(data[:total])!=struct.unpack_from('<I',data,total)[0]:
        raise ValueError('archive length or CRC')
    if directory!=HEADER.size or directory+count*RECORD.size>total:
        raise ValueError('entry directory')
    records=[RECORD.unpack_from(data,directory+i*RECORD.size) for i in range(count)]
    table_cursor=directory+count*RECORD.size
    seen=set()
    for eid,kind,n,table,start,size in records:
        if eid in seen or kind not in (7,11) or n>MAX_SECTIONS or table!=table_cursor:
            raise ValueError('entry identity or section directory')
        seen.add(eid)
        table_cursor+=n*SECTION.size
        if table_cursor>total:raise ValueError('truncated sections')
    if nsd_offset!=table_cursor or nsd_size>total-nsd_offset:
        raise ValueError('NSD extent')
    cursor=nsd_offset+nsd_size
    entries=[]
    for eid,kind,n,table,start,size in records:
        if start!=cursor or size>total-start:raise ValueError('entry extent')
        items=[]
        for i in range(n):
            offset,length=SECTION.unpack_from(data,table+i*SECTION.size)
            if offset!=cursor or length>start+size-cursor:
                raise ValueError('section extent')
            items.append(data[offset:offset+length])
            cursor+=length
        if cursor!=start+size:raise ValueError('unclaimed entry bytes')
        entries.append({'eid':eid,'type':kind,'items':items})
    if cursor!=total:raise ValueError('unclaimed archive bytes')
    return {'level':lid,'nsd':data[nsd_offset:nsd_offset+nsd_size],'entries':entries}


def export(level_id,output):
    import cb3
    archive=cb3.load_level(level_id,cache=False)
    entries=[e for e in archive.entries if e.type in (7,11)]
    nsd=archive.nsd.buf if archive.nsd else b''
    data=encode(level_id,entries,nsd)
    restored=decode(data)
    assert restored['nsd']==nsd
    assert [(r['eid'],r['type'],r['items']) for r in restored['entries']]==[
        (e.eid,e.type,list(e.items)) for e in entries]
    output=Path(output)
    output.mkdir(parents=True,exist_ok=True)
    path=output/f'lv_{level_id:02X}.cga'
    path.write_bytes(data)
    return {'level':level_id,'file':path.name,'bytes':len(data),
            'sha256':hashlib.sha256(data).hexdigest(),
            'nsf_sha256':hashlib.sha256(Path(archive.nsf_path).read_bytes()).hexdigest(),
            'nsd_sha256':hashlib.sha256(nsd).hexdigest(),
            'gool':sum(e.type==11 for e in entries),'zdat':sum(e.type==7 for e in entries),
            'sections':sum(len(e.items) for e in entries)}


def main():
    import cb3
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--level',type=lambda s:int(s,0))
    parser.add_argument('--output',type=Path,default=ROOT/'game_data/runtime')
    args=parser.parse_args()
    ids=cb3.all_level_ids() if args.level is None else [args.level]
    records=[export(lid,args.output) for lid in ids]
    manifest={'schema':'CGA1/1','runtime_integrated':False,'archives':records}
    (args.output/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    print(f'Exported and byte-verified {len(records)} archives, {sum(r["bytes"] for r in records)} bytes')


if __name__=='__main__':main()
