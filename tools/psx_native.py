"""Read-only PS-X EXE mapping and selected MIPS evidence, not execution.

Capstone output is ordinary offline analysis, not REA provider Evidence.
No executable wrapper, compiler, emulator, dynamic loader or configuration edits.
"""
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import struct

ORIGINAL_SHA256='1b93cc563c90966e1d79b750f7d19bc1662859cceeb991775e9e9a22656d71d1'


@dataclass(frozen=True)
class Image:
    data: bytes
    load: int
    entry: int
    sha256: str
    file_size: int

    @classmethod
    def parse(cls,blob):
        if len(blob)<2048 or blob[:8]!=b'PS-X EXE':raise ValueError('invalid PS-X EXE header')
        entry,=struct.unpack_from('<I',blob,16)
        load,size=struct.unpack_from('<2I',blob,24)
        if (load%4 or not size or size%4 or load<0x80000000 or load+size>0x80200000 or
            size>len(blob)-2048 or not load<=entry<load+size or entry%4):
            raise ValueError('invalid mapped extent or entry point')
        return cls(blob[2048:2048+size],load,entry,hashlib.sha256(blob).hexdigest(),len(blob))

    def read(self,address,size):
        if size<0 or address<self.load or address+size>self.load+len(self.data):
            raise ValueError('address outside file-backed payload')
        return self.data[address-self.load:address-self.load+size]

    def word(self,address):return struct.unpack('<I',self.read(address,4))[0]

    def disassemble(self,start,end):
        import capstone
        if start%4 or end%4 or end<=start:raise ValueError('invalid instruction window')
        data=self.read(start,end-start)
        md=capstone.Cs(capstone.CS_ARCH_MIPS,capstone.CS_MODE_MIPS32|capstone.CS_MODE_LITTLE_ENDIAN)
        rows=[{'address':i.address,'bytes':i.bytes.hex(),'mnemonic':i.mnemonic,'operands':i.op_str}
              for i in md.disasm(data,start)]
        covered=sum(len(bytes.fromhex(r['bytes'])) for r in rows)
        return {'start':start,'end':end,'file_offset':2048+start-self.load,
                'raw_hex':data.hex(),'decoded_bytes':covered,
                'complete_window':covered==len(data),'instructions':rows}


def signal_evidence(image):
    import capstone
    if image.sha256!=ORIGINAL_SHA256:raise ValueError('not the audited SCUS-94244 image')
    ranges={
        'dispatch':(0x80038e28,0x80038f30),
        'signal_opcode_stubs':(0x80039b94,0x80039bec),
        'send_wrapper':(0x80037edc,0x8003808c),
        'signal_delivery':(0x8001db38,0x8001ddf4),
        'receive_service':(0x800383dc,0x800385c0),
        'return_dispatch':(0x800399fc,0x80039aac),
        'frame_enter_return':(0x8001d588,0x8001d674),
        'operand_frame_address':(0x80035410,0x80035538),
        'operand_fast_read':(0x80038c94,0x80038dd8),
        'jal_frame':(0x80039900,0x80039978),
        'jalr_frame':(0x80039978,0x800399fc),
        'fast_operand_address':(0x80038b40,0x80038c24),
        'code_pointer_store':(0x800392b8,0x800392fc),
        'simple_store_address':(0x80038db0,0x80038dd8),
        'frame_setters':(0x80037cd8,0x80037edc),
        'entity_update_gate':(0x8001d82c,0x8001d8d8),
        'state_setup':(0x8001d2d4,0x8001d588),
        'array_read_write':(0x80039724,0x80039784),
        'state_wrapper':(0x80037bec,0x80037cd8),
        'state_conditional_stubs':(0x80039b1c,0x80039b7c),
    }
    return {'schema':'psx-gool-signal-native/1','analysis':'offline-capstone-static',
        'source_sha256':image.sha256,'source_file_size':image.file_size,
        'payload_sha256':hashlib.sha256(image.data).hexdigest(),
        'load':image.load,'entry':image.entry,'payload_bytes':len(image.data),
        'capstone_version':capstone.__version__,
        'dispatch_table':{'address':0x8005d120,'entries':[
            {'opcode':op,'address':image.word(0x8005d120+op*4)} for op in [0x18,0x2a,0x47,0x4e,*range(0x31,0x40)]]},
        'windows':{name:image.disassemble(*bounds) for name,bounds in ranges.items()},
        'limitations':['No target execution, native decompilation or reachability proof.',
            'Delay-slot assembly is retained; higher-level semantics are analyst interpretation.',
            'Windows cover selected routines, not the entire executable.',
            'Native three-word frames are prepared but C and mode integration remain unvalidated.',
            'Receive sourceOp 3 and global/nested signal modes remain unresolved.',
            'SEND route application and bounded word spans are prepared, not integrated into game runtime.']}


if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser();parser.add_argument('source');parser.add_argument('output')
    args=parser.parse_args()
    report=signal_evidence(Image.parse(Path(args.source).read_bytes()))
    path=Path(args.output);path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k not in ('windows','dispatch_table')},indent=2))
