#include "gool_native_resources.h"
#include "gool_vm.h"
#include <string.h>

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void wr32(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(value>>(i*8));
}
static gool_native_behavior *by_handle(gool_native_resources *r,uint32_t handle) {
    for(uint32_t i=0;i<r->count;i++)if(r->behaviors[i].handle==handle)return &r->behaviors[i];
    return NULL;
}
static int source_open(void *context,uint32_t eid,gool_data *out) {
    gool_native_resources *r=context;
    if(!r||!r->open||r->closing||!out)return GOOL_NATIVE_RESOURCE_INVALID;
    for(uint32_t i=0;i<r->count;i++)if(r->behaviors[i].eid==eid) {
        *out=r->behaviors[i].data;return 0;
    }
    return GOOL_RESOURCES_MISSING;
}
static int source_lookup(void *context,uint32_t lookup,gool_data *out) {
    gool_native_resources *r=context;
    if(!r||!r->open||r->closing||!out)return GOOL_NATIVE_RESOURCE_INVALID;
    for(uint32_t i=0;i<r->count;i++)if(r->behaviors[i].data.lookup_index==lookup) {
        *out=r->behaviors[i].data;return 0;
    }
    return GOOL_RESOURCES_MISSING;
}
static int source_resolve(void *context,uint32_t address,gool_data *out,uint32_t *pc) {
    gool_native_resources *r=context;
    if(!r||!r->open||r->closing||!out||!pc||!address)return GOOL_NATIVE_RESOURCE_INVALID;
    for(uint32_t i=0;i<r->count;i++) {
        gool_native_behavior *b=&r->behaviors[i];
        if(address>=b->code_address) {
            uint32_t delta=address-b->code_address;
            if(!(delta&3u)&&delta<0x10000u){*out=b->data;*pc=delta/4;return 0;}
        }
    }
    return GOOL_RESOURCES_MISSING;
}
int gool_native_resources_open(gool_native_resources *r,gool_resources *bank,gool_domain *domain,uint32_t base,void *constant_storage,uint32_t constant_capacity) {
    if(!r||r->open||!constant_storage||!constant_capacity||!bank||!bank->open||bank->closing||!domain||!bank->catalog_count||
       bank->catalog_count>GOOL_RESOURCES_SLOTS||base<0x90000000u||(base&3u)||
       (uint64_t)base+(uint64_t)bank->catalog_count*0x20000u>0xff000000u||
       r->generation==UINT64_MAX)return GOOL_NATIVE_RESOURCE_INVALID;
    for(uint32_t i=0;i<bank->catalog_count;i++) {
        gool_data data;int error=gool_resources_get(bank,bank->catalog[i].eid,&data);if(error)return error;
    }
    uint64_t generation=r->generation+1,bank_generation;
    int error=gool_resources_retain(bank,&bank_generation);if(error)return error;
    *r=(gool_native_resources){.bank=bank,.domain=domain,.bank_generation=bank_generation,
        .source={.context=r,.open=source_open,.lookup=source_lookup,.resolve=source_resolve},
        .constant_storage=constant_storage,.constant_capacity=constant_capacity,
        .generation=generation,.count=bank->catalog_count,.references=1,.open=1,.bank_retained=1};
    for(uint32_t i=0;i<r->count;i++) {
        gool_native_behavior *b=&r->behaviors[i];const uint8_t *raw;uint32_t size;
        b->eid=bank->catalog[i].eid;b->handle=base+i*0x20000u;
        error=gool_resources_get(bank,b->eid,&b->data);if(error)goto failed;
        error=gool_resources_entry(bank,b->eid,&raw,&size);if(error)goto failed;
        uint32_t count=rd32(raw+12);
        if(size<20||(count!=3&&count!=6)||16+(count+1)*4>size){error=GOOL_NATIVE_RESOURCE_INVALID;goto failed;}
        memcpy(b->entry,raw,16);
        for(uint32_t item=0;item<=count;item++) {
            uint32_t offset=rd32(raw+16+item*4);
            if(offset>size){error=GOOL_NATIVE_RESOURCE_INVALID;goto failed;}
            uint32_t address=b->handle+0x10000u+offset;
            wr32(b->entry+16+item*4,address);
            if(item==1)b->code_address=address;
            if(item==5&&count==6)b->model_address=address;
        }
        error=gool_domain_map_readonly(domain,b->handle,b->entry,16+(count+1)*4,&b->leases[0]);
        if(error)goto failed;
        b->mapped|=1u;
        uint32_t begin=rd32(raw+24),end=rd32(raw+28),aligned=(size+3u)&~3u;
        if(begin>end||end>size||((begin|end)&3u)||
           end-begin>r->constant_capacity-r->constant_used) {
            error=GOOL_NATIVE_RESOURCE_INVALID;goto failed;
        }
        uint8_t *constants=r->constant_storage+r->constant_used;
        if(end>begin)memcpy(constants,raw+begin,end-begin);
        error=gool_domain_map_readonly(domain,b->handle+0x10000u,raw,begin,&b->leases[1]);
        if(error)goto failed;
        b->mapped|=2u;
        if(end>begin) {
            error=gool_domain_map_mutable(domain,b->handle+0x10000u+begin,constants,end-begin,&b->leases[2]);
            if(error)goto failed;
            b->mapped|=4u;
        }
        if(aligned>end) {
            error=gool_domain_map_readonly(domain,b->handle+0x10000u+end,raw+end,aligned-end,&b->leases[3]);
            if(error)goto failed;
            b->mapped|=8u;
        }
        r->constant_used+=end-begin;
        b->data.native_constant_address=b->handle+0x10000u+begin;
        b->data.original_constants=b->data.constants;b->data.constants=constants;
        b->data.native_code_address=b->code_address;b->data.source=&r->source;
    }
    return 0;
failed: {
        r->closing=1;
        int cleanup=gool_native_resources_close(r);return cleanup?cleanup:error;
    }
}
int gool_native_resources_retain(gool_native_resources *r,uint64_t *generation) {
    if(!r||!r->open||r->closing||!generation||r->references==UINT32_MAX)return GOOL_NATIVE_RESOURCE_INVALID;
    r->references++;*generation=r->generation;return 0;
}
int gool_native_resources_release(gool_native_resources *r,uint64_t generation) {
    if(!r||!r->open||generation!=r->generation||r->references<=1)return GOOL_NATIVE_RESOURCE_INVALID;
    r->references--;return 0;
}
int gool_native_resources_can_close(const gool_native_resources *r) {
    if(!r)return GOOL_NATIVE_RESOURCE_INVALID;
    if(!r->open)return 0;
    if(r->references!=1)return GOOL_NATIVE_RESOURCE_BUSY;
    for(uint32_t i=0;i<r->count;i++) {
        const gool_native_behavior *b=&r->behaviors[i];uint32_t count;int error;
        for(unsigned part=0;part<4;part++)if(b->mapped&(1u<<part)) {
            error=gool_domain_references(r->domain,b->leases[part],&count);if(error)return error;
            if(count!=1)return GOOL_NATIVE_RESOURCE_BUSY;
        }
    }
    return 0;
}
int gool_native_resources_close(gool_native_resources *r) {
    int ready=gool_native_resources_can_close(r);if(ready)return ready;
    if(!r->open)return 0;
    r->closing=1;
    for(uint32_t i=0;i<r->count;i++) {
        gool_native_behavior *b=&r->behaviors[i];int error;
        for(unsigned part=0;part<4;part++)if(b->mapped&(1u<<part)) {
            error=gool_domain_release(r->domain,b->leases[part]);if(error)return error;
            b->mapped&=~(1u<<part);
        }
    }
    if(r->bank_retained) {
        int error=gool_resources_release(r->bank,r->bank_generation);if(error)return error;r->bank_retained=0;
    }
    uint64_t generation=r->generation;*r=(gool_native_resources){.generation=generation};return 0;
}
int gool_native_resources_load(void *context,uint32_t class_word,uint32_t *native_result) {
    gool_native_resources *r=context;
    if(!r||!r->open||r->closing||!native_result)return GOOL_NATIVE_RESOURCE_INVALID;
    uint32_t value;int error=gool_domain_read32(r->domain,class_word,&value);if(error)return error;
    gool_native_behavior *b=NULL;
    if(value&1u) {
        for(uint32_t i=0;i<r->count;i++)if(r->behaviors[i].eid==value){b=&r->behaviors[i];break;}
        if(!b)return GOOL_RESOURCES_MISSING;
        error=gool_domain_write32(r->domain,class_word,b->handle);if(error)return error;
    } else b=by_handle(r,value);
    if(!b)return GOOL_RESOURCES_MISSING;
    *native_result=b->handle;return 0;
}
int gool_native_resources_behavior(gool_native_resources *r,uint32_t handle,gool_data *out) {
    if(!r||!r->open||r->closing||!out)return GOOL_NATIVE_RESOURCE_INVALID;
    gool_native_behavior *b=by_handle(r,handle);if(!b)return GOOL_RESOURCES_MISSING;
    *out=b->data;return 0;
}
int gool_native_resources_code_to_vm(gool_native_resources *r,uint32_t address,int32_t *pointer) {
    if(!r||!r->open||r->closing||!pointer)return GOOL_NATIVE_RESOURCE_INVALID;
    if(!address){*pointer=0;return 0;}
    for(uint32_t i=0;i<r->count;i++) {
        gool_native_behavior *b=&r->behaviors[i];
        if(address>=b->code_address) {
            uint32_t delta=address-b->code_address;
            if(!(delta&3u)&&delta<0x10000u)return gool_vm_address(&b->data,delta/4,pointer);
        }
    }
    return GOOL_RESOURCES_MISSING;
}
int gool_native_resources_code_from_vm(gool_native_resources *r,int32_t pointer,uint32_t *address) {
    if(!r||!r->open||r->closing||!address)return GOOL_NATIVE_RESOURCE_INVALID;
    uint32_t bits=(uint32_t)pointer;
    if(!bits){*address=0;return 0;}
    if((bits>>28)!=5) {
        gool_data data;uint32_t pc;
        int error=source_resolve(r,bits,&data,&pc);if(error)return error;
        *address=bits;return 0;
    }
    if((bits>>28)!=5||(bits&3u))return GOOL_NATIVE_RESOURCE_INVALID;
    uint32_t lookup=(bits&0x0fffffffu)>>18,offset=(bits>>2)&0xffffu;
    if(offset>=0x4000u)return GOOL_NATIVE_RESOURCE_INVALID;
    for(uint32_t i=0;i<r->count;i++)if(r->behaviors[i].data.lookup_index==lookup) {
        *address=r->behaviors[i].code_address+offset*4;return 0;
    }
    return GOOL_RESOURCES_MISSING;
}
int gool_native_resources_model(gool_native_resources *r,uint32_t handle,uint32_t offset,uint32_t *address) {
    if(!r||!r->open||r->closing||!address)return GOOL_NATIVE_RESOURCE_INVALID;
    gool_native_behavior *b=by_handle(r,handle);
    if(!b||!b->model_address||offset>=b->data.model_bytes)return GOOL_RESOURCES_MISSING;
    *address=b->model_address+offset;return 0;
}
