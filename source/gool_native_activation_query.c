#include "gool_native_activation_query.h"
#include <limits.h>

int gool_native_activation_query_begin(void *context,uint32_t database,uint32_t key,
    uint32_t position,uint32_t *list,uint32_t *count) {
    gool_native_activation_query *o=context;
    if(!o||o->active||!list||!count||list==count||key>INT16_MAX||!o->bank||
       !o->bank->open||o->bank->closing||o->source.domain!=o->bank->domain||
       !o->source.chain_limit||o->source.chain_limit>ZDAT_NATIVE_CHAIN_MAX)
        return GOOL_NATIVE_ACTIVATION_INVALID;
    o->active=1;o->status=GOOL_NATIVE_ACTIVATION_PENDING;
    uint32_t visited[ZDAT_NATIVE_CHAIN_MAX],used=0,db=database;
    while(db) {
        if((db&3u)||db>UINT32_MAX-12u)return o->status=ZDAT_NATIVE_RANGE;
        if(used==o->source.chain_limit)return o->status=ZDAT_NATIVE_CYCLE;
        for(uint32_t i=0;i<used;i++)if(visited[i]==db)return o->status=ZDAT_NATIVE_CYCLE;
        visited[used++]=db;
        uint32_t zone;int error=gool_domain_read32(o->source.domain,db+4u,&zone);
        if(error)return o->status=error;
        uint32_t index=0;
        while(index<o->count&&o->zones[index]!=zone)index++;
        if(index==o->count) {
            if(o->count==ZDAT_RESOURCES_SLOTS)return o->status=ZDAT_RESOURCE_FULL;
            error=zdat_resources_acquire_address(o->bank,zone,&o->leases[index]);
            if(error)return o->status=error;
            o->zones[o->count++]=zone;
        }
        /* A database may only borrow the zone that actually contains it. */
        zdat_resource_slot *slot=&o->bank->slots[o->leases[index].slot];
        if(db<slot->address||db-slot->address>=slot->size||
           slot->size-(db-slot->address)<16u)return o->status=ZDAT_NATIVE_RANGE;
        /* Stop on the selected key, as32710 does: an unused fallback may be
         * unloaded, cyclic or invalid without affecting a local property. */
        uint16_t raw_count;
        error=gool_domain_read16(o->source.domain,db+12u,&raw_count);if(error)return o->status=error;
        int32_t low=0,high=(raw_count<32768u?(int32_t)raw_count:(int32_t)raw_count-65536)-1;
        int found=0;
        while(low<=high) {
            int32_t middle=low+(high-low)/2;uint16_t raw_key;
            uint64_t at=(uint64_t)db+16u+(uint32_t)middle*8u;
            if(at>UINT32_MAX||at-slot->address+2u>slot->size)return o->status=ZDAT_NATIVE_RANGE;
            error=gool_domain_read16(o->source.domain,(uint32_t)at,&raw_key);if(error)return o->status=error;
            int32_t candidate=raw_key<32768u?(int32_t)raw_key:(int32_t)raw_key-65536;
            if(candidate==(int32_t)key){found=1;break;}
            if(candidate<(int32_t)key)low=middle+1;else high=middle-1;
        }
        if(found)break;
        error=gool_domain_read32(o->source.domain,db+8u,&db);if(error)return o->status=error;
    }
    zdat_native_value result;
    int error=zdat_native_select_exact(&o->source,database,(int32_t)key,position,&result);
    if(error)return o->status=error;
    *list=result.address;*count=result.address?result.length:0;
    return o->status=GOOL_NATIVE_ACTIVATION_DONE;
}
int gool_native_activation_query_step(void *context,uint32_t budget) {
    gool_native_activation_query *o=context;(void)budget;
    return o&&o->active?o->status:GOOL_NATIVE_ACTIVATION_INVALID;
}
int gool_native_activation_query_cancel(void *context) {
    gool_native_activation_query *o=context;
    if(!o)return GOOL_NATIVE_ACTIVATION_INVALID;
    while(o->count) {
        int error=zdat_resources_release(o->bank,o->leases[o->count-1u]);if(error)return error;
        o->count--;
    }
    o->active=0;o->status=0;return 0;
}
