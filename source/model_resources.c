#include "model_resources.h"
int model_resources_open(model_resources *b,gool_domain *d,void *storage,uint32_t base) {
    return zdat_resources_open_models(b,d,storage,base);
}
int model_resources_request(model_resources *b,uint32_t eid){return zdat_resources_request(b,eid);}
int model_resources_poll(model_resources *b){return zdat_resources_poll(b);}
int model_resources_acquire(model_resources *b,uint32_t eid,uint32_t *address,model_resource_lease *lease) {
    return zdat_resources_acquire(b,eid,address,lease);
}
int model_resources_release(model_resources *b,model_resource_lease lease){return zdat_resources_release(b,lease);}
int model_resources_evict(model_resources *b,uint32_t eid){return zdat_resources_evict(b,eid);}
int model_resources_can_close(const model_resources *b){return zdat_resources_can_close(b);}
int model_resources_close(model_resources *b){return zdat_resources_close(b);}
int model_resources_item(model_resources *b,model_resource_lease lease,uint32_t index,uint32_t *address,uint32_t *bytes) {
    if(!b||!b->open||b->closing||!address||!bytes||lease.slot>=ZDAT_RESOURCES_SLOTS||!lease.generation)
        return ZDAT_RESOURCE_INVALID;
    zdat_resource_slot *slot=&b->slots[lease.slot];
    if(slot->state!=ZDAT_RESOURCE_READY||!slot->mapped||!slot->references||slot->generation!=lease.generation)
        return ZDAT_RESOURCE_INVALID;
    uint32_t count,type,first,last;int error=gool_domain_read32(b->domain,slot->address+8u,&type);if(error)return error;
    if(type!=1&&type!=2)return ZDAT_RESOURCE_INVALID;
    error=gool_domain_read32(b->domain,slot->address+12u,&count);if(error)return error;
    if(index>=count||(uint64_t)16u+(index+2ull)*4u>slot->size)return ZDAT_RESOURCE_INVALID;
    error=gool_domain_read32(b->domain,slot->address+16u+index*4u,&first);if(error)return error;
    error=gool_domain_read32(b->domain,slot->address+20u+index*4u,&last);if(error)return error;
    if(first<slot->address||last<first||(uint64_t)last>(uint64_t)slot->address+slot->size)
        return ZDAT_RESOURCE_INVALID;
    *address=first;*bytes=last-first;return 0;
}
int model_resources_select_frames(model_resources *b,uint32_t descriptor,uint32_t word,
                                   int paired,model_frame_selection *selection) {
    if(!b||!b->open||b->closing||!descriptor||(descriptor&3u)||descriptor>UINT32_MAX-0x10u||
       !selection||selection->lease.generation||(paired!=0&&paired!=1))return ZDAT_RESOURCE_INVALID;
    uint8_t kind;uint16_t limit;uint32_t flags,reference;
    int error=gool_domain_read8(b->domain,descriptor,&kind);if(error)return error;
    if(kind!=1)return ZDAT_RESOURCE_INVALID;
    error=gool_domain_read32(b->domain,descriptor+4u,&flags);if(error)return error;
    error=gool_domain_read16(b->domain,descriptor+2u,&limit);if(error)return error;
    error=gool_domain_read32(b->domain,descriptor+0x10u,&reference);if(error)return error;
    uint32_t entry=reference;model_frame_selection next={.index=word>>8};
    if(reference&1u)error=model_resources_acquire(b,reference,&entry,&next.lease);
    else error=zdat_resources_acquire_address(b,reference,&next.lease);
    if(error)return error;
    uint32_t type;error=gool_domain_read32(b->domain,entry+8u,&type);
    if(!error&&type!=1)error=ZDAT_RESOURCE_INVALID;
    uint32_t first=next.index,second=first;
    if(flags&1u) {
        if(!paired)first=second=first>>1;
        else if((first&1u)&&first+1u<limit){first>>=1;second=first+1u;}
        else first=second=(first+1u)>>1;
    }
    uint32_t bytes;
    if(!error)error=model_resources_item(b,next.lease,first,&next.first,&bytes);
    if(!error&&paired)error=model_resources_item(b,next.lease,second,&next.second,&bytes);
    if(!error&&!paired)next.second=next.first;
    if(error){int cleanup=model_resources_release(b,next.lease);return cleanup?cleanup:error;}
    *selection=next;return 0;
}
