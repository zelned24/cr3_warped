"""Conservative GOOL control-flow audit of an entity's first graphics assignment.

This is an offline analysis, not a runtime VM. Unknown/native execution and
signals stop a path; no result from such a path is promoted to a unique model.
Operand layouts follow the MIT noclip reference stored in tools/reference.
"""
import argparse
import json
import struct
from pathlib import Path
import cb3
import zdat
from gool_operand import decode as decode_operand, simple as simple_operand

ROOT=Path(__file__).resolve().parents[1]

SIMPLE_STORES=frozenset((0x11,0x12,0x14,0x17,0x19,0x27,0x2b,0x2c))
CONTROL_SOURCES=frozenset((0x20,0x21,0x22,0x23,0x24))

def simple_destination(value):
    return simple_operand(value & 0xfff)

def written_source(argument):
    """Return (source, possible_alias); frame/constant writes remain unknown.

    An indirect SELF reference aliases a direct member. Other object links
    can also alias SELF at runtime, so cannot silently bypass geometry checks.
    """
    if argument & 0xe00 == 0xe00:return argument & 0x1ff,False
    if argument & 0xc00 == 0xc00:
        return argument & 0x3f,((argument>>6)&7)!=0
    return None,True

def signed(value,bits):
    return (value & ((1<<(bits-1))-1))-(value & (1<<(bits-1)))

def constant(arg,base,alternate):
    try:operand=decode_operand(arg)
    except ValueError:return None
    if operand.kind=='constant':
        data=(alternate if operand.base else base).items[2]
        offset=operand.value*4
        if offset+4>len(data):return None
        return struct.unpack_from('<i',data,offset)[0]
    if operand.kind in ('literal','boolean'):return operand.value
    return None

def audit(archive,entity,budget=4096):
    result={'name':entity.name,'gool':entity.file_name,'object_id':entity.object_id,
            'state':entity.state_index,'assignments':[],'stops':[],'visited':0}
    base=archive.entry(entity.file_name)
    table=zdat.parse_gool_table(base) if base and base.type==11 else None
    if not table or entity.state is None:
        result['stops']=['unresolved_initial_state'];result['resolution']='unresolved';return result

    entry_stops=set()
    def enter(state_index):
        if state_index is None or state_index<0 or state_index>=len(table.states):return None
        state=table.states[state_index]
        if state.pre_run is not None:entry_stops.add('pre_run_not_audited')
        alternate=archive.entry(state.alt_name)
        owner=alternate if state.script & 0x4000 else base
        if not alternate or not owner:return None
        return owner.name,state.script & 0x3fff,alternate.name,()

    initial=enter(entity.state_index)
    pending=[initial] if initial else []
    seen=set();assignments={};stops=set()
    if not initial:stops.add('missing_state_file')
    while pending and len(seen)<budget:
        node=pending.pop()
        if node in seen:
            # A loop before graphics is not proof that graphics will be reached.
            stops.add('control_flow_cycle_or_merge');continue
        seen.add(node)
        owner_name,pc,alternate_name,returns=node
        owner=archive.entry(owner_name);alternate=archive.entry(alternate_name)
        data=owner.items[1]
        if pc<0 or pc*4+4>len(data):stops.add('script_bounds');continue
        inst=struct.unpack_from('<I',data,pc*4)[0];op=inst>>24
        def queue(next_pc,stack=returns):pending.append((owner_name,next_pc,alternate_name,stack))
        model_offset=None
        if op==0x38:model_offset=(inst>>5)&0x7fc
        elif op in SIMPLE_STORES or op==0x29:
            destination=simple_destination(inst) if op in SIMPLE_STORES else inst & 0xfff
            source,aliased=written_source(destination)
            if source in CONTROL_SOURCES:
                stops.add('unresolved_control_store');continue
            if source==0x2a and aliased:
                stops.add('possibly_aliased_geometry_store');continue
            if source==0x2a and op==0x27:
                value=constant((inst>>12)&0xfff,base,alternate)
                if value is None:stops.add('dynamic_geometry_operand');continue
                model_offset=(value & 0xffffffff)>>6
            elif source==0x2a:
                # Null geometry can be deliberately assigned before SET_FRAME.
                value=constant((inst>>12)&0xfff,base,alternate) if op==0x11 else None
                if value!=0:stops.add('unresolved_geometry_store');continue
        elif op==0x18:
            destination=(inst>>18)&0x3f
            if destination==0x2a:stops.add('unresolved_geometry_store');continue
            if destination in CONTROL_SOURCES:stops.add('unresolved_control_store');continue
        if model_offset is not None:
            model=next((m for m in table.models if m.offset==model_offset),None)
            if model is None:stops.add('invalid_model_offset');continue
            assignments[(model_offset,owner_name,pc)]={'offset':model_offset,'script':owner_name,
                'pc':pc,'instruction':f'{inst:08X}',**model.as_dict()}
            continue
        if op==0x32:queue(pc+1+signed(inst,10))
        elif op in (0x33,0x34):
            queue(pc+1);queue(pc+1+signed(inst,10))
        elif op==0x3b:
            target=inst & 0x7fff
            target_owner=alternate_name if target & 0x4000 else base.name
            if len(returns)>=16:stops.add('call_depth');continue
            pending.append((target_owner,target & 0x3fff,alternate_name,returns+((owner_name,pc+1),)))
        elif op==0x31:
            if not returns:stops.add('return_before_graphics');continue
            caller,return_pc=returns[-1]
            pending.append((caller,return_pc,alternate_name,returns[:-1]))
        elif op in (0x35,0x36,0x37):
            # State changes also depend on interact flags; inspect both outcomes.
            queue(pc+1)
            new=enter(inst & 0x3ff)
            if new:pending.append(new)
            else:stops.add('invalid_state_transition')
        elif op==0x49:stops.add('native_mips')
        elif op in (0x1c,0x3c,0x3d,0x3e,0x3f,0x40,0x44,0x45,0x46,0x47):stops.add(f'external_control_{op:02x}')
        elif op==0x39:stops.add('frame_before_resolved_geometry')
        elif op<=0x42 and op not in (0x2e,0x3a):queue(pc+1)
        else:stops.add(f'unsupported_opcode_{op:02x}')
    if pending:stops.add('instruction_budget')
    stops.update(entry_stops)
    result['assignments']=list(assignments.values());result['stops']=sorted(stops);result['visited']=len(seen)
    offsets={a['offset'] for a in result['assignments']}
    result['resolution']='unique_first_assignment' if len(offsets)==1 and not stops else 'candidates_only' if offsets else 'unresolved'
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--level',type=lambda x:int(x,0),default=11)
    args=parser.parse_args();archive=cb3.load_level(args.level)
    records=[audit(archive,e) for zone in zdat.parse_zdat_archive(archive) for e in zone.objects]
    counts={state:sum(r['resolution']==state for r in records) for state in ('unique_first_assignment','candidates_only','unresolved')}
    output=ROOT/'game_data/objects'/f'initial_graphics_{args.level:02X}.json'
    output.write_text(json.dumps({'schema':'initial-gool-graphics/1','level':args.level,'counts':counts,'entities':records},indent=2),encoding='utf-8')
    print(output);print(json.dumps(counts))

if __name__=='__main__':main()
