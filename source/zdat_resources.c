#include "zdat_resources.h"
#include "zdat_runtime.h"
#include <string.h>
static uint32_t rd(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void wr(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static int release_io(zdat_resource_slot *s) {
    int first=0,error;
    if(s->view.leased){error=nsf_resource_release_view(&s->view);if(error)first=error;}
    if(s->has_ticket) {
        error=nsf_resource_release_ticket(s->ticket);
        if(error){if(!first)first=error;}else s->has_ticket=0;
    }
    return first;
}
static int idle(const zdat_resources *b,const zdat_resource_slot *s) {
    if(s->references)return ZDAT_RESOURCE_BUSY;
    if(s->mapped) {
        uint32_t references;int error=gool_domain_references(b->domain,s->mapping,&references);if(error)return error;
        if(references!=1)return ZDAT_RESOURCE_BUSY;
    }
    return 0;
}
static int remove_slot(zdat_resources *b,zdat_resource_slot *s) {
    int error=idle(b,s);if(error)return error;
    if(s->has_ticket){error=nsf_resource_cancel(s->ticket);if(error)return error;}
    error=release_io(s);if(error)return error;
    if(s->mapped){error=gool_domain_release(b->domain,s->mapping);if(error)return error;}
    *s=(zdat_resource_slot){0};return 0;
}
int zdat_resources_open(zdat_resources *b,gool_domain *domain,void *storage,uint32_t base) {
    if(!b||b->open||!domain||!storage||!base||(base&3u)||
       (uint64_t)base+ZDAT_RESOURCES_SLOTS*0x20000u>UINT32_MAX||b->generation==UINT64_MAX)
        return ZDAT_RESOURCE_INVALID;
    uintptr_t bytes=(uintptr_t)storage,owner=(uintptr_t)b;
    if(bytes>UINTPTR_MAX-ZDAT_RESOURCES_SLOTS*ZDAT_RESOURCE_BYTES||owner>UINTPTR_MAX-sizeof(*b)||
       (bytes<owner+sizeof(*b)&&owner<bytes+ZDAT_RESOURCES_SLOTS*ZDAT_RESOURCE_BYTES))
        return ZDAT_RESOURCE_INVALID;
    uint64_t generation=b->generation;
    *b=(zdat_resources){.domain=domain,.storage=storage,.base=base,.generation=generation,
        .allowed_types=1u<<7,.open=1};return 0;
}
int zdat_resources_open_models(zdat_resources *b,gool_domain *domain,void *storage,uint32_t base) {
    int error=zdat_resources_open(b,domain,storage,base);if(error)return error;
    b->allowed_types=(1u<<1)|(1u<<2);return 0;
}
int zdat_resources_request(zdat_resources *b,uint32_t eid) {
    if(!b||!b->open||b->closing||!(eid&1u)||b->generation==UINT64_MAX)return ZDAT_RESOURCE_INVALID;
    zdat_resource_slot *empty=0;uint32_t index=0;
    for(uint32_t i=0;i<ZDAT_RESOURCES_SLOTS;i++) {
        zdat_resource_slot *s=&b->slots[i];
        if(s->state&&s->eid==eid)return s->state==ZDAT_RESOURCE_FAILED?s->error:0;
        if(!s->state&&!empty){empty=s;index=i;}
    }
    if(!empty)return ZDAT_RESOURCE_FULL;
    nsf_resource_ticket ticket;int error=nsf_resource_request(eid,&ticket);if(error)return error;
    *empty=(zdat_resource_slot){.ticket=ticket,.eid=eid,.address=b->base+index*0x20000u,
        .generation=++b->generation,.state=ZDAT_RESOURCE_LOADING,.has_ticket=1};return 0;
}
static int copy_view_entry(zdat_resources *b,uint32_t index,const nsf_resource_view *v) {
    zdat_resource_slot *s=&b->slots[index];int error;
    if(v->entry_type>=32||!(b->allowed_types&(1u<<v->entry_type))||!v->entry||
       !v->item_count||v->entry_size>ZDAT_RESOURCE_BYTES)
        return ZDAT_RESOURCE_INVALID;
    if(v->entry_type==7) {
    if(v->item_count<2)return ZDAT_RESOURCE_INVALID;
    const uint8_t *header,*offsets;uint32_t header_size,offset_size;
    error=nsf_resource_item(v,0,&header,&header_size);if(error)return error;
    error=nsf_resource_item(v,1,&offsets,&offset_size);if(error)return error;
    if(header_size<0x194||offset_size<12||rd(header+0x184)!=2||
       (uint64_t)2+rd(header+0x188)+rd(header+0x18c)!=v->item_count||
       (uint64_t)0x194+(uint64_t)rd(header+0x190)*4>header_size)return ZDAT_RESOURCE_INVALID;
    for(uint32_t i=2;i<v->item_count;i++) {
        const uint8_t *data;uint32_t size;zdat_database db;
        error=nsf_resource_item(v,i,&data,&size);if(error)return error;
        error=zdat_database_bind(&db,(zdat_span){data,size});if(error)return error;
    }
    } else {
        for(uint32_t i=0;i<v->item_count;i++) {
            const uint8_t *data;uint32_t size;
            error=nsf_resource_item(v,i,&data,&size);if(error)return error;
            if(v->entry_type==1) {
                if(size<0x1cu||(uint64_t)0x1cu+(uint64_t)rd(data+0x14u)*0x28u>size)
                    return ZDAT_RESOURCE_INVALID;
            } else if(v->entry_type==2&&!i&&size<0x4cu)return ZDAT_RESOURCE_INVALID;
        }
    }
    uint8_t *destination=b->storage+index*ZDAT_RESOURCE_BYTES;
    uintptr_t source_address=(uintptr_t)v->entry,destination_address=(uintptr_t)destination;
    if(source_address<destination_address+v->entry_size&&
       destination_address<source_address+v->entry_size)return ZDAT_RESOURCE_INVALID;
    memcpy(destination,v->entry,v->entry_size);
    for(uint32_t i=0;i<=v->item_count;i++) {
        uint32_t offset=rd(v->entry+16+i*4);
        if((offset&3u)||offset>v->entry_size)return ZDAT_RESOURCE_INVALID;
        wr(destination+16+i*4,s->address+offset);
    }
    uint32_t size=v->entry_size,entry_type=v->entry_type;
    error=release_io(s);if(error)return error;
    if(entry_type==7)error=gool_domain_map_mutable(b->domain,s->address,destination,size,&s->mapping);
    else error=gool_domain_map_readonly(b->domain,s->address,destination,size,&s->mapping);
    if(error)return error;
    s->mapped=1;s->size=size;
    if(entry_type==7) {
        zdat_native native={.domain=b->domain,.chain_limit=ZDAT_NATIVE_CHAIN_MAX};
        error=zdat_native_link_zone(&native,s->address);if(error)return error;
    }
    s->state=ZDAT_RESOURCE_READY;return 0;
}
static int copy_entry(zdat_resources *b,uint32_t index) {
    zdat_resource_slot *s=&b->slots[index];
    int error=nsf_resource_acquire(s->ticket,&s->view);if(error)return error;
    return copy_view_entry(b,index,&s->view);
}
int zdat_resources_import(zdat_resources *b,const nsf_resource_view *view) {
    if(!b||!b->open||b->closing||!view||!view->page||!view->entry||
       !(view->eid&1u)||b->generation==UINT64_MAX)return ZDAT_RESOURCE_INVALID;
    nsf_resource_view validated={0};
    int error=nsf_resource_parse_page(view->page,NSF_PAGE_BYTES,rd(view->page+4),view->eid,&validated);
    if(error)return error;
    uint32_t index=ZDAT_RESOURCES_SLOTS;
    for(uint32_t i=0;i<ZDAT_RESOURCES_SLOTS;i++) {
        zdat_resource_slot *s=&b->slots[i];
        if(s->state&&s->eid==view->eid)
            return s->state==ZDAT_RESOURCE_READY?0:s->state==ZDAT_RESOURCE_FAILED?s->error:ZDAT_RESOURCE_BUSY;
        if(!s->state&&index==ZDAT_RESOURCES_SLOTS)index=i;
    }
    if(index==ZDAT_RESOURCES_SLOTS)return ZDAT_RESOURCE_FULL;
    zdat_resource_slot *s=&b->slots[index];
    *s=(zdat_resource_slot){.eid=view->eid,.address=b->base+index*0x20000u,
        .generation=++b->generation,.state=ZDAT_RESOURCE_LOADING};
    error=copy_view_entry(b,index,&validated);
    if(error) {
        int cleanup=remove_slot(b,s);return cleanup?cleanup:error;
    }
    return 0;
}
int zdat_resources_poll(zdat_resources *b) {
    if(!b||!b->open||b->closing)return ZDAT_RESOURCE_INVALID;
    int first=0,pending=0;
    for(uint32_t i=0;i<ZDAT_RESOURCES_SLOTS;i++) {
        zdat_resource_slot *s=&b->slots[i];
        if(s->state==ZDAT_RESOURCE_FAILED){if(!first)first=s->error;continue;}
        if(s->state!=ZDAT_RESOURCE_LOADING)continue;
        nsf_stream_state state;int worker_error;
        int error=nsf_resource_poll(s->ticket,&state,&worker_error);
        if(!error&&(state==NSF_STREAM_QUEUED||state==NSF_STREAM_READING)){pending++;continue;}
        if(!error&&state!=NSF_STREAM_READY)error=worker_error?worker_error:ZDAT_RESOURCE_INVALID;
        if(!error)error=copy_entry(b,i);
        if(error) {
            int cleanup=release_io(s);s->error=cleanup?cleanup:error;s->state=ZDAT_RESOURCE_FAILED;
            if(!first)first=s->error;
        }
    }
    return first?first:pending;
}
int zdat_resources_acquire(zdat_resources *b,uint32_t eid,uint32_t *address,zdat_resource_lease *lease) {
    if(!b||!b->open||b->closing||!address||!lease)return ZDAT_RESOURCE_INVALID;
    for(uint32_t i=0;i<ZDAT_RESOURCES_SLOTS;i++) {
        zdat_resource_slot *s=&b->slots[i];if(!s->state||s->eid!=eid)continue;
        if(s->state==ZDAT_RESOURCE_FAILED)return s->error;
        if(s->state!=ZDAT_RESOURCE_READY)return ZDAT_RESOURCE_BUSY;
        if(s->references==UINT32_MAX)return ZDAT_RESOURCE_INVALID;
        s->references++;*address=s->address;*lease=(zdat_resource_lease){s->generation,i};return 0;
    }
    return NSF_RESOURCE_MISSING;
}
int zdat_resources_release(zdat_resources *b,zdat_resource_lease lease) {
    if(!b||!b->open||lease.slot>=ZDAT_RESOURCES_SLOTS||!lease.generation)return ZDAT_RESOURCE_INVALID;
    zdat_resource_slot *s=&b->slots[lease.slot];
    if(s->generation!=lease.generation||!s->references)return ZDAT_RESOURCE_INVALID;
    s->references--;return 0;
}
int zdat_resources_evict(zdat_resources *b,uint32_t eid) {
    if(!b||!b->open||b->closing)return ZDAT_RESOURCE_INVALID;
    for(uint32_t i=0;i<ZDAT_RESOURCES_SLOTS;i++)
        if(b->slots[i].state&&b->slots[i].eid==eid)return remove_slot(b,&b->slots[i]);
    return NSF_RESOURCE_MISSING;
}
int zdat_resources_can_close(const zdat_resources *b) {
    if(!b)return ZDAT_RESOURCE_INVALID;
    if(!b->open)return 0;
    for(uint32_t i=0;i<ZDAT_RESOURCES_SLOTS;i++){int error=idle(b,&b->slots[i]);if(error)return error;}
    return 0;
}
int zdat_resources_close(zdat_resources *b) {
    int error=zdat_resources_can_close(b);if(error)return error;
    if(!b->open)return 0;
    b->closing=1;
    for(uint32_t i=0;i<ZDAT_RESOURCES_SLOTS;i++){error=remove_slot(b,&b->slots[i]);if(error)return error;}
    uint64_t generation=b->generation;*b=(zdat_resources){.generation=generation};return 0;
}

int zdat_resources_acquire_address(zdat_resources *b,uint32_t address,zdat_resource_lease *lease) {
    if(!b||!b->open||b->closing||!address||!lease)return ZDAT_RESOURCE_INVALID;
    for(uint32_t i=0;i<ZDAT_RESOURCES_SLOTS;i++) {
        zdat_resource_slot *s=&b->slots[i];
        if(s->state!=ZDAT_RESOURCE_READY||s->address!=address)continue;
        if(s->references==UINT32_MAX)return ZDAT_RESOURCE_INVALID;
        s->references++;*lease=(zdat_resource_lease){s->generation,i};return 0;
    }
    return NSF_RESOURCE_MISSING;
}

int zdat_zone_prepare_close(zdat_zone_prepare *p) {
    if(!p)return ZDAT_RESOURCE_INVALID;
    int error;
    if(p->previous_retained) {
        error=zdat_resources_release(p->bank,p->previous_lease);if(error)return error;
        p->previous_retained=0;
    }
    if(p->candidate_retained) {
        error=zdat_resources_release(p->bank,p->candidate_lease);if(error)return error;
        p->candidate_retained=0;
    }
    *p=(zdat_zone_prepare){0};return 0;
}
int zdat_zone_prepare_begin(zdat_zone_prepare *p,zdat_resources *bank,
                            uint32_t previous,uint32_t candidate,uint32_t limit) {
    if(!p||p->active||p->candidate_retained||p->previous_retained||!bank||
       !bank->open||bank->closing||!candidate||!limit)return ZDAT_RESOURCE_INVALID;
    zdat_resource_lease candidate_lease;
    int error=zdat_resources_acquire_address(bank,candidate,&candidate_lease);if(error)return error;
    *p=(zdat_zone_prepare){.bank=bank,.candidate=candidate,.previous=previous,
        .candidate_lease=candidate_lease,.candidate_retained=1,.active=1,.status=ZONE_TRANSITION_BUDGET};
    if(!previous)return p->status;
    error=zdat_resources_acquire_address(bank,previous,&p->previous_lease);
    if(error){p->active=0;p->status=error;return error;}
    p->previous_retained=1;
    zone_transition_services services={.context=bank->domain,.read=gool_domain_read,.write=gool_domain_write};
    error=zone_transition_contains_begin(&p->predicate,previous,candidate,limit,&services);
    if(error<0){p->active=0;p->status=error;}
    return error;
}
int zdat_zone_prepare_step(zdat_zone_prepare *p,uint32_t budget) {
    if(!p||!p->bank)return ZDAT_RESOURCE_INVALID;
    if(!p->active||!budget)return p->status;
    int error=0;
    if(p->previous) {
        error=zone_transition_contains_step(&p->predicate,budget);
        if(error==ZONE_TRANSITION_BUDGET)return error;
        if(!error&&p->predicate.contains) {
            zone_transition_services services={.context=p->bank->domain,.read=gool_domain_read,.write=gool_domain_write};
            error=zone_transition_enter(p->candidate,&services);
            if(!error)p->entered=1;
        }
    }
    p->active=0;p->status=error;return error;
}
