"""Default GOOL signal routing audit. Does not deliver/execute a signal.

The viewer rejects bit-0x8000 addresses. Preserve them as explicit script
routes instead of treating a viewer limitation as original-game semantics.
Native SCUS-94244 0x8001DCC8..0x8001DCE0 always selects base code and masks
the offset with 0x3FFF. Unlike state-script addresses, bit 0x4000 is ignored.
"""
from collections import Counter
from dataclasses import dataclass,asdict
import hashlib
import json
from pathlib import Path
import struct


@dataclass(frozen=True)
class Route:
    kind: str
    signal: int
    raw: int=0
    value: int=0


def lookup(base,raw_signal):
    if not 0<=raw_signal<=0xffffffff:raise ValueError('raw signal out of bounds')
    signal=raw_signal>>8
    if len(base.items) not in (3,6) or len(base.items[0])<20:
        raise ValueError('invalid behavior header')
    count=0 if len(base.items)==3 else struct.unpack_from('<I',base.items[0],16)[0]
    if signal>=count:return Route('outside_table',signal)
    if count*2>len(base.items[3]) or len(base.items[3])%2:
        raise ValueError('invalid signal table size')
    raw=struct.unpack_from('<H',base.items[3],signal*2)[0]
    if raw==255:return Route('none',signal,raw)
    if raw&0x8000:return Route('script',signal,raw,raw&0x3fff)
    if len(base.items[4])%16 or raw>=len(base.items[4])//16:
        raise ValueError('signal state out of bounds')
    return Route('state',signal,raw,raw)


def resolve_script(base,route):
    if (route.kind!='script' or not 0<=route.raw<=65535 or not route.raw&0x8000 or
        route.value!=route.raw&0x3fff):
        raise ValueError('not a valid explicit script route')
    owner=base
    if len(owner.items[1])%4 or route.value>=len(owner.items[1])//4:
        raise ValueError('missing owner or explicit script PC out of bounds')
    return owner,route.value


def audit(archives):
    counts=Counter();records=[];identities=[];archive_count=entries=0
    for arc in archives:
        archive_count+=1
        for entry in arc.of_type(11):
            entries+=1
            digest=hashlib.sha256()
            for item in entry.items:digest.update(struct.pack('<Q',len(item))+item)
            identities.append({'level':arc.level_id,'eid':entry.eid,'name':entry.name,
                               'sha256':digest.hexdigest()})
            if len(entry.items)==3:continue
            count=struct.unpack_from('<I',entry.items[0],16)[0]
            for signal in range(count):
                route=lookup(entry,signal<<8);counts[route.kind]+=1
                if route.kind=='none':continue
                record={'level':arc.level_id,'eid':entry.eid,'name':entry.name,**asdict(route)}
                if route.kind=='script':
                    owner,pc=resolve_script(entry,route)
                    record['code_owner']='base'
                    record['first_word']=struct.unpack_from('<I',owner.items[1],pc*4)[0]
                    record['code_bounds_verified']=True
                records.append(record)
    return {'schema':'gool-signal-routes/2','archive_count':archive_count,
            'behavior_occurrences':entries,'entries_by_kind':dict(counts),
            'execution_proven':False,'reachability_proven':False,
            'limits':['No SEND_SIGNAL delivery implementation.',
                      'Explicit-code calling convention still requires native evidence.',
                      'No inference that every table entry is reachable.'],
            'identities':identities,'routes':records}


if __name__=='__main__':
    import cb3
    report=audit(cb3.load_level(lid) for lid in cb3.all_level_ids())
    path=Path(__file__).resolve().parents[1]/'game_data/runtime/gool_signal_routes.json'
    path.write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k not in ('identities','routes')},indent=2))
