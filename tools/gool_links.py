"""Audit GOOL alternate owners without selecting ambiguous external variants.

References in unused states are not proof of a missing live object. Initial
ZDAT state matches are reported separately; this is not reachability analysis.
"""
from collections import Counter,defaultdict
import hashlib
import json
from pathlib import Path
import struct
import cb3
import nsf
import zdat

ROOT=Path(__file__).resolve().parents[1]


def identity(entry):
    digest=hashlib.sha256()
    for item in entry.items:
        digest.update(struct.pack('<Q',len(item)))
        digest.update(item)
    return digest.hexdigest()


def classify(local,variants):
    if local is not None:return 'local'
    if not variants:return 'absent_from_disc_index'
    return 'external_unique_candidate' if len(variants)==1 else 'external_ambiguous'


def audit(archives):
    archives=list(archives)
    variants=defaultdict(lambda:defaultdict(list))
    for arc in archives:
        for entry in arc.of_type(11):
            variants[entry.eid][identity(entry)].append(arc.level_id)
    links=[]
    for arc in archives:
        local={entry.eid:entry for entry in arc.of_type(11)}
        initial=defaultdict(list)
        for zone in zdat.parse_zdat_archive(arc):
            for entity in zone.objects:
                if entity.file_name is not None and entity.state_index is not None:
                    initial[entity.file_name,entity.state_index].append({'zone':zone.name,'entity':entity.name})
        for entry in arc.of_type(11):
            if len(entry.items)<5:continue
            grouped={}
            for index in range(len(entry.items[4])//16):
                _,_,alt,on_signal,pre_run,script=struct.unpack_from('<II4H',entry.items[4],index*16)
                if alt>=len(entry.items[2])//4:
                    raise ValueError(f'{arc.level_id:02X}/{entry.name}: invalid alternate index')
                eid=struct.unpack_from('<I',entry.items[2],alt*4)[0]
                # Alternate constants can be used even when the code is base.
                # Keep every state owner link, not only bit-0x4000 handlers.
                link=grouped.setdefault(eid,{'level':arc.level_id,'base':entry.name,
                    'base_eid':entry.eid,'alternate':nsf.eid_str(eid),'alternate_eid':eid,
                    'classification':classify(local.get(eid),variants.get(eid,{})),
                    'candidates':[{'sha256':sha,'levels':levels} for sha,levels in sorted(variants.get(eid,{}).items())],
                    'states':[]})
                handlers=[]
                for kind,raw in (('main',script),('pre_run',pre_run),('on_signal',on_signal)):
                    if kind!='main' and raw==0x3fff:continue
                    address=raw&0x7fff
                    if address&0x4000:
                        owner=local.get(eid)
                        handlers.append({'kind':kind,'pc':address&0x3fff,
                            'local_bounds':None if owner is None else (address&0x3fff)<len(owner.items[1])//4})
                link['states'].append({'index':index,'alternate_handlers':handlers,
                    'initial_entities':initial.get((entry.name,index),[])})
            links.extend(grouped.values())
    counts=Counter(link['classification'] for link in links)
    initial_counts=Counter()
    for link in links:
        if any(state['initial_entities'] for state in link['states']):
            initial_counts[link['classification']]+=1
    return {'schema':'gool-owner-links/1','archive_count':len(archives),
        'links_by_classification':dict(counts),'initial_links_by_classification':dict(initial_counts),
        'reachability_proven':False,'links':links}


def main():
    report=audit(cb3.load_level(lid) for lid in cb3.all_level_ids())
    path=ROOT/'game_data/runtime/gool_links.json'
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k!='links'},indent=2))


if __name__=='__main__':main()
