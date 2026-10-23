"""Write small native behavior catalogs without copying original code payloads."""
import argparse
from pathlib import Path
import struct
import zlib
import runtime_archive

ROOT=Path(__file__).resolve().parents[1]

def encode(archive):
    level=archive['level']
    entries=[entry for entry in archive['entries'] if entry['type']==11]
    if len(entries)>1024:
        raise ValueError('GOOL catalog exceeds native code address capacity')
    nsd=archive['nsd']
    if len(nsd)<0x520:
        raise ValueError('Missing original NSD directory')
    count=struct.unpack_from('<I',nsd,0x404)[0]
    if 0x520+count*8>len(nsd):
        raise ValueError('Truncated original NSD directory')
    directory={struct.unpack_from('<I',nsd,0x524+i*8)[0] for i in range(count)}
    data=bytearray(struct.pack('<4sIII',b'GLC1',1,level,len(entries)))
    for lookup,entry in enumerate(entries):
        if entry['eid'] not in directory or not entry['eid']&1:
            raise ValueError('Behavior EID is absent from original level NSD')
        data+=struct.pack('<II',entry['eid'],lookup)
    data+=struct.pack('<I',zlib.crc32(data))
    return bytes(data)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input',type=Path,default=ROOT/'game_data/runtime')
    parser.add_argument('--output',type=Path,default=ROOT/'game_data/runtime')
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    sources=sorted(args.input.glob('lv_*.cga'))
    if not sources:
        raise SystemExit('No original runtime archives found')
    for source in sources:
        archive=runtime_archive.decode(source.read_bytes())
        target=args.output/f"lv_{archive['level']:02X}.glc"
        data=encode(archive);temporary=target.with_suffix('.glc.tmp')
        temporary.write_bytes(data);temporary.replace(target)
        print(f'{target.name}: {(len(data)-20)//8} behaviors, {len(data)} bytes')

if __name__=='__main__':main()
