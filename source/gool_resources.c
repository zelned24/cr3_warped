#include "gool_resources.h"
#include <string.h>

static int catalog_eid(const gool_resources *b,uint32_t eid,uint32_t *lookup) {
    for(uint32_t i=0;i<b->catalog_count;i++)if(b->catalog[i].eid==eid) {
        *lookup=b->catalog[i].lookup_index;return 0;
    }
    return GOOL_RESOURCES_MISSING;
}
static int relative_open(void *context,uint32_t eid,gool_data *out) {
    return gool_resources_get(context,eid,out);
}
static int relative_lookup(void *context,uint32_t lookup,gool_data *out) {
    gool_resources *b=context;
    if(!b||!b->open||b->closing||!out)return GOOL_RESOURCES_INVALID;
    for(uint32_t i=0;i<b->catalog_count;i++)if(b->catalog[i].lookup_index==lookup)
        return gool_resources_get(b,b->catalog[i].eid,out);
    return GOOL_RESOURCES_MISSING;
}
int gool_resources_open(gool_resources *b,const gool_resource_catalog *catalog,
                          uint32_t count,void *storage,uint32_t slots) {
    if(!b||b->open||!catalog||!storage||!count||count>1024||!slots||slots>GOOL_RESOURCES_SLOTS||
       b->generation==UINT64_MAX)return GOOL_RESOURCES_INVALID;
    uintptr_t start=(uintptr_t)storage,owner=(uintptr_t)b;
    uintptr_t catalog_start=(uintptr_t)catalog;
    size_t bytes=(size_t)slots*GOOL_RESOURCE_SLOT_BYTES;
    size_t catalog_bytes=(size_t)count*sizeof(*catalog);
    if(bytes>UINTPTR_MAX-start||sizeof(*b)>UINTPTR_MAX-owner||
       catalog_bytes>UINTPTR_MAX-catalog_start||
       (start<owner+sizeof(*b)&&owner<start+bytes)||
       (catalog_start<owner+sizeof(*b)&&owner<catalog_start+catalog_bytes)||
       (catalog_start<start+bytes&&start<catalog_start+catalog_bytes))return GOOL_RESOURCES_INVALID;
    for(uint32_t i=0;i<count;i++) {
        if(!(catalog[i].eid&1u)||catalog[i].lookup_index>=1024)return GOOL_RESOURCES_INVALID;
        for(uint32_t j=0;j<i;j++)if(catalog[j].eid==catalog[i].eid||
            catalog[j].lookup_index==catalog[i].lookup_index)return GOOL_RESOURCES_INVALID;
    }
    uint64_t generation=b->generation+1;
    *b=(gool_resources){.source={.context=b,.open=relative_open,.lookup=relative_lookup},
        .catalog=catalog,.storage=storage,.generation=generation,.catalog_count=count,
        .slot_count=slots,.references=1,.open=1};
    return 0;
}
int gool_resources_request(gool_resources *b,uint32_t eid) {
    if(!b||!b->open||b->closing)return GOOL_RESOURCES_INVALID;
    uint32_t lookup;int error=catalog_eid(b,eid,&lookup);if(error)return error;
    gool_resource_slot *empty=NULL;
    for(uint32_t i=0;i<b->slot_count;i++) {
        gool_resource_slot *s=&b->slots[i];
        if(s->state==GOOL_RESOURCE_EMPTY){if(!empty)empty=s;continue;}
        if(s->eid==eid)return s->state==GOOL_RESOURCE_FAILED?s->error:0;
    }
    if(!empty)return GOOL_RESOURCES_FULL;
    nsf_resource_ticket ticket;error=nsf_resource_request(eid,&ticket);if(error)return error;
    *empty=(gool_resource_slot){.ticket=ticket,.eid=eid,.lookup_index=lookup,
        .state=GOOL_RESOURCE_LOADING,.has_ticket=1};return 0;
}
static int release_slot(gool_resource_slot *s) {
    int first=0,error;
    if(s->view.leased){error=nsf_resource_release_view(&s->view);if(error)first=error;}
    if(s->has_ticket) {
        error=nsf_resource_release_ticket(s->ticket);
        if(error){if(!first)first=error;}else {s->has_ticket=0;s->ticket=(nsf_resource_ticket){0};}
    }
    return first;
}
static int fail(gool_resource_slot *s,int error) {
    int cleanup=release_slot(s);s->state=GOOL_RESOURCE_FAILED;s->error=cleanup?cleanup:error;
    return s->error;
}
static int copy_behavior(gool_resources *b,uint32_t index) {
    gool_resource_slot *s=&b->slots[index];
    int error=nsf_resource_acquire(s->ticket,&s->view);if(error)return error;
    if(s->view.entry_type!=11||!s->view.entry||
       (s->view.item_count!=3&&s->view.item_count!=6)||s->view.entry_size>GOOL_RESOURCE_SLOT_BYTES)
        return GOOL_RESOURCES_INVALID;
    uint8_t *destination=b->storage+(size_t)index*GOOL_RESOURCE_SLOT_BYTES;
    uintptr_t source_start=(uintptr_t)s->view.entry,destination_start=(uintptr_t)destination;
    if(source_start<destination_start+s->view.entry_size&&
       destination_start<source_start+s->view.entry_size)return GOOL_RESOURCES_INVALID;
    gool_data_item items[6];
    for(uint32_t i=0;i<s->view.item_count;i++) {
        const uint8_t *data;uint32_t size;
        error=nsf_resource_item(&s->view,i,&data,&size);if(error)return error;
        size_t offset=(size_t)(data-s->view.entry);
        if(offset>s->view.entry_size||size>s->view.entry_size-offset)return GOOL_RESOURCES_INVALID;
        items[i]=(gool_data_item){.data=destination+offset,.size=size};
    }
    memcpy(destination,s->view.entry,s->view.entry_size);
    /* Word-domain adapters can extract a final halfword without reading beyond
     * owned storage. These unused alignment bytes are not serialized items. */
    uint32_t aligned=(s->view.entry_size+3u)&~3u;
    if(aligned>s->view.entry_size)memset(destination+s->view.entry_size,0,aligned-s->view.entry_size);
    gool_data data;
    error=gool_data_bind_items(&data,s->eid,index,s->lookup_index,items,s->view.item_count,&b->source);
    if(error)return error;
    uint32_t size=s->view.entry_size;
    error=release_slot(s);if(error)return error;
    s->data=data;s->entry_size=size;s->state=GOOL_RESOURCE_READY;return 0;
}
int gool_resources_poll(gool_resources *b) {
    if(!b||!b->open||b->closing)return GOOL_RESOURCES_INVALID;
    int first=0,loading=0;
    for(uint32_t i=0;i<b->slot_count;i++) {
        gool_resource_slot *s=&b->slots[i];
        if(s->state==GOOL_RESOURCE_FAILED){if(!first)first=s->error;continue;}
        if(s->state!=GOOL_RESOURCE_LOADING)continue;
        nsf_stream_state state;int worker_error;
        int error=nsf_resource_poll(s->ticket,&state,&worker_error);
        if(!error&&(state==NSF_STREAM_QUEUED||state==NSF_STREAM_READING)){loading++;continue;}
        if(!error&&state!=NSF_STREAM_READY)error=worker_error?worker_error:GOOL_RESOURCES_UNAVAILABLE;
        if(!error)error=copy_behavior(b,i);
        if(error){error=fail(s,error);if(!first)first=error;}
    }
    return first?first:loading;
}
int gool_resources_get(gool_resources *b,uint32_t eid,gool_data *out) {
    if(!b||!b->open||b->closing||!out)return GOOL_RESOURCES_INVALID;
    for(uint32_t i=0;i<b->slot_count;i++) {
        gool_resource_slot *s=&b->slots[i];
        if(s->state!=GOOL_RESOURCE_EMPTY&&s->eid==eid) {
            if(s->state==GOOL_RESOURCE_FAILED)return s->error;
            if(s->state!=GOOL_RESOURCE_READY)return GOOL_RESOURCES_UNAVAILABLE;
            *out=s->data;return 0;
        }
    }
    return GOOL_RESOURCES_MISSING;
}
int gool_resources_retain(gool_resources *b,uint64_t *generation) {
    if(!b||!b->open||b->closing||!generation||b->references==UINT32_MAX)return GOOL_RESOURCES_INVALID;
    b->references++;*generation=b->generation;return 0;
}
int gool_resources_entry(gool_resources *b,uint32_t eid,const uint8_t **bytes,uint32_t *size) {
    if(!b||!b->open||b->closing||!bytes||!size)return GOOL_RESOURCES_INVALID;
    for(uint32_t i=0;i<b->slot_count;i++) {
        gool_resource_slot *s=&b->slots[i];
        if(s->state!=GOOL_RESOURCE_EMPTY&&s->eid==eid) {
            if(s->state==GOOL_RESOURCE_FAILED)return s->error;
            if(s->state!=GOOL_RESOURCE_READY)return GOOL_RESOURCES_UNAVAILABLE;
            *bytes=b->storage+(size_t)i*GOOL_RESOURCE_SLOT_BYTES;*size=s->entry_size;return 0;
        }
    }
    return GOOL_RESOURCES_MISSING;
}
int gool_resources_release(gool_resources *b,uint64_t generation) {
    if(!b||!b->open||generation!=b->generation||b->references<=1)return GOOL_RESOURCES_INVALID;
    b->references--;return 0;
}
int gool_resources_close(gool_resources *b) {
    if(!b)return GOOL_RESOURCES_INVALID;
    if(!b->open)return 0;
    if(b->references!=1)return GOOL_RESOURCES_BUSY;
    b->closing=1;int first=0;
    for(uint32_t i=0;i<b->slot_count;i++) {
        gool_resource_slot *s=&b->slots[i];
        if(s->has_ticket) {
            int error=nsf_resource_cancel(s->ticket);if(error&&!first)first=error;
        }
        int error=release_slot(s);if(error&&!first)first=error;
    }
    if(first)return first;
    uint64_t generation=b->generation;
    *b=(gool_resources){.generation=generation};return 0;
}
