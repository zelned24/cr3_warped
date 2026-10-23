#include "gool_data.h"

static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint16_t half(const uint8_t *p) {return (uint16_t)((uint16_t)p[0]|(uint16_t)p[1]<<8);}
static int32_t signed_half(const uint8_t *p) {
    uint32_t n=half(p);
    return n>=0x8000u?(int32_t)n-65536:(int32_t)n;
}

int gool_data_bind_items(gool_data *out,uint32_t eid,uint32_t entry,uint32_t lookup,
                           const gool_data_item *items,uint32_t count,const gool_data_source *source) {
    if(!out||!items||(count!=3&&count!=6)||lookup>=1024||
       (source&&(!source->open||!source->lookup)))return -1;
    const uint8_t *sections[6]={0};uint32_t sizes[6]={0};
    for(uint32_t i=0;i<count;i++) {
        if(!items[i].data)return -1;
        sections[i]=items[i].data;sizes[i]=items[i].size;
    }
    if(sizes[0]<20||sections[0][5]>=32||sizes[1]%4||sizes[2]%4)return -1;
    uint32_t declared=word(sections[0]+16),signals=0,objects=0;
    if(count==6) {
        if(sizes[3]%2||declared>sizes[3]/2||sizes[4]%16)return -1;
        signals=declared;objects=sizes[3]/2-signals;
        for(uint32_t i=0;i<sizes[4]/16;i++)
            if(half(sections[4]+i*16+8)>=sizes[2]/4)return -1;
    }
    /* Three-section library entries have no states/index table even when
     * maxSignal in their header is nonzero. Do not read a nonexistent item. */
    *out=(gool_data){.eid=eid,.entry=entry,.lookup_index=lookup,.category=sections[0][5],
        .stack_start=word(sections[0]+12),.declared_signals=declared,
        .code=sections[1],.constants=sections[2],.indices=sections[3],
        .states=sections[4],.models=sections[5],.code_words=sizes[1]/4,
        .constant_words=sizes[2]/4,.signal_count=signals,.object_count=objects,
        .state_count=sizes[4]/16,.model_bytes=sizes[5],.source=source};
    return 0;
}

int gool_data_open(gool_data *out,const cga_archive *archive,uint32_t eid) {
    if(!out)return -1;
    uint32_t index;cga_entry entry;
    if(cga_find(archive,eid,&index)||cga_entry_at(archive,index,&entry)||entry.type!=11||
       (entry.section_count!=3&&entry.section_count!=6))return -1;
    gool_data_item items[6];
    for(uint32_t i=0;i<entry.section_count;i++)
        if(cga_section(archive,index,i,&items[i].data,&items[i].size))return -1;
    uint32_t lookup=0;
    for(uint32_t i=0;i<index;i++) {
        cga_entry prior;if(cga_entry_at(archive,i,&prior))return -1;
        if(prior.type==11)lookup++;
    }
    gool_data next={0};
    if(gool_data_bind_items(&next,eid,index,lookup,items,entry.section_count,NULL))return -1;
    next.archive=archive;*out=next;return 0;
}

int gool_data_open_relative(gool_data *out,const gool_data *scope,uint32_t eid) {
    if(!out||!scope)return -1;
    if(!scope->source)return gool_data_open(out,scope->archive,eid);
    if(!scope->source->open)return -1;
    gool_data next={0};
    if(scope->source->open(scope->source->context,eid,&next)||next.eid!=eid||
       next.source!=scope->source)return -1;
    *out=next;return 0;
}
int gool_data_lookup_relative(gool_data *out,const gool_data *scope,uint32_t lookup) {
    if(!out||!scope)return -1;
    if(!scope->source)return gool_data_lookup(out,scope->archive,lookup);
    if(!scope->source->lookup)return -1;
    gool_data next={0};
    if(scope->source->lookup(scope->source->context,lookup,&next)||next.lookup_index!=lookup||
       next.source!=scope->source)return -1;
    *out=next;return 0;
}

int gool_data_lookup(gool_data *out,const cga_archive *archive,uint32_t lookup) {
    if(!out||!archive||!archive->data)return -1;
    uint32_t seen=0;
    for(uint32_t i=0;i<archive->count;i++) {
        cga_entry entry;
        if(cga_entry_at(archive,i,&entry))return -1;
        if(entry.type==11) {
            if(seen==lookup)return gool_data_open(out,archive,entry.eid);
            seen++;
        }
    }
    return -1;
}

int gool_data_constant(const gool_data *d,uint32_t index,uint32_t *value) {
    if(!d||!d->constants||!value||index>=d->constant_words)return -1;
    *value=word(d->constants+index*4);return 0;
}
int gool_data_instruction(const gool_data *d,uint32_t pc,uint32_t *instruction) {
    if(!d||!d->code||!instruction||pc>=d->code_words)return -1;
    *instruction=word(d->code+pc*4);return 0;
}
int gool_data_signal(const gool_data *d,uint32_t index,int32_t *value) {
    if(!d||!d->indices||!value||index>=d->signal_count)return -1;
    *value=signed_half(d->indices+index*2);return 0;
}
int gool_data_object(const gool_data *d,uint32_t index,int32_t *value) {
    if(!d||!d->indices||!value||index>=d->object_count)return -1;
    *value=signed_half(d->indices+(d->signal_count+index)*2);return 0;
}
int gool_data_state(const gool_data *d,uint32_t index,gool_state_data *out) {
    if(!d||!d->states||!out||index>=d->state_count)return -1;
    const uint8_t *p=d->states+index*16;
    uint16_t alternate=half(p+8);uint32_t eid;
    if(alternate>=d->constant_words)return -1;
    const uint8_t *constants=d->original_constants?d->original_constants:d->constants;
    if(!constants)return -1;
    eid=word(constants+(uint32_t)alternate*4);
    *out=(gool_state_data){.motion_flags=word(p),.interact_flags=word(p+4),
        .alternate_eid=eid,.alternate_index=alternate,.on_signal=half(p+10),
        .pre_run=half(p+12),.script=half(p+14)};
    return 0;
}
int gool_data_script(const gool_data *base,uint32_t index,gool_script_kind kind,
                     gool_data *owner,uint32_t *pc) {
    if(!owner||!pc)return -1;
    gool_state_data state;
    if(gool_data_state(base,index,&state))return -1;
    uint16_t raw;
    switch(kind) {
        case GOOL_SCRIPT_MAIN:raw=state.script;break;
        case GOOL_SCRIPT_PRE_RUN:raw=state.pre_run;break;
        case GOOL_SCRIPT_SIGNAL:raw=state.on_signal;break;
        default:return -1;
    }
    if(kind!=GOOL_SCRIPT_MAIN&&raw==0x3fff)return 1;
    uint32_t address=raw&0x7fff,offset=address&0x3fff;
    gool_data target;
    if(address&0x4000) {
        if(gool_data_open_relative(&target,base,state.alternate_eid))return -1;
    } else target=*base;
    if(offset>=target.code_words)return -1;
    *owner=target;*pc=offset;return 0;
}
