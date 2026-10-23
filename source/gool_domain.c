#include "gool_domain.h"
#include <stddef.h>

static int map(gool_domain *d,uint32_t address,const uint8_t *bytes,uint8_t *mutable_bytes,
                 uint32_t size,gool_domain_lease *lease) {
    if(!d||!bytes||!lease||!address||!size||(uint64_t)address+size>UINT64_C(0x100000000)||
       size>UINTPTR_MAX-(uintptr_t)bytes||d->generation==UINT64_MAX)return GOOL_DOMAIN_INVALID;
    uintptr_t domain_start=(uintptr_t)d,storage_start=(uintptr_t)bytes;
    if(sizeof(*d)>UINTPTR_MAX-domain_start)return GOOL_DOMAIN_INVALID;
    if(storage_start<domain_start+sizeof(*d)&&domain_start<storage_start+size)
        return GOOL_DOMAIN_INVALID;
    uint32_t free_index=GOOL_DOMAIN_REGIONS;
    for(uint32_t i=0;i<GOOL_DOMAIN_REGIONS;i++) {
        const gool_domain_region *r=&d->regions[i];
        if(!r->references){if(free_index==GOOL_DOMAIN_REGIONS)free_index=i;continue;}
        if((uint64_t)address<(uint64_t)r->address+r->size&&
           (uint64_t)r->address<(uint64_t)address+size)return GOOL_DOMAIN_BUSY;
    }
    if(free_index==GOOL_DOMAIN_REGIONS)return GOOL_DOMAIN_FULL;
    uint64_t generation=++d->generation;
    d->regions[free_index]=(gool_domain_region){.bytes=bytes,.mutable_bytes=mutable_bytes,
        .generation=generation,.address=address,.size=size,.references=1};
    *lease=(gool_domain_lease){.generation=generation,.index=free_index};return 0;
}
int gool_domain_map_readonly(gool_domain *d,uint32_t address,const void *bytes,uint32_t size,gool_domain_lease *lease) {
    return map(d,address,bytes,NULL,size,lease);
}
int gool_domain_map_mutable(gool_domain *d,uint32_t address,void *bytes,uint32_t size,gool_domain_lease *lease) {
    return map(d,address,bytes,bytes,size,lease);
}
static gool_domain_region *lookup(gool_domain *d,gool_domain_lease lease) {
    if(!d||lease.index>=GOOL_DOMAIN_REGIONS||!lease.generation)return NULL;
    gool_domain_region *r=&d->regions[lease.index];
    return r->references&&r->generation==lease.generation?r:NULL;
}
int gool_domain_map_overlay(gool_domain *d,gool_domain_lease parent,uint32_t address,
                              void *bytes,uint32_t size,gool_domain_lease *lease) {
    gool_domain_region *p=lookup(d,parent);
    if(!p||!p->mutable_bytes||p->overlay||!bytes||!lease||!size||(address&3u)||(size&3u)||
       address<p->address||address-p->address>p->size||size>p->size-(address-p->address)||
       p->references==UINT32_MAX||d->generation==UINT64_MAX)return GOOL_DOMAIN_INVALID;
    uintptr_t storage=(uintptr_t)bytes,owner=(uintptr_t)d;
    if(size>UINTPTR_MAX-storage||sizeof(*d)>UINTPTR_MAX-owner||
       (storage<owner+sizeof(*d)&&owner<storage+size))return GOOL_DOMAIN_INVALID;
    uint32_t free_index=GOOL_DOMAIN_REGIONS;
    for(uint32_t i=0;i<GOOL_DOMAIN_REGIONS;i++) {
        const gool_domain_region *r=&d->regions[i];
        if(!r->references){if(free_index==GOOL_DOMAIN_REGIONS)free_index=i;continue;}
        if(i!=parent.index&&(uint64_t)address<(uint64_t)r->address+r->size&&
           (uint64_t)r->address<(uint64_t)address+size)return GOOL_DOMAIN_BUSY;
    }
    if(free_index==GOOL_DOMAIN_REGIONS)return GOOL_DOMAIN_FULL;
    uint64_t generation=++d->generation;p->references++;
    d->regions[free_index]=(gool_domain_region){.bytes=bytes,.mutable_bytes=bytes,.generation=generation,
        .address=address,.size=size,.references=1,.parent=parent,.overlay=1};
    *lease=(gool_domain_lease){.generation=generation,.index=free_index};return 0;
}
int gool_domain_retain(gool_domain *d,gool_domain_lease lease) {
    gool_domain_region *r=lookup(d,lease);
    if(!r||r->references==UINT32_MAX)return GOOL_DOMAIN_INVALID;
    r->references++;return 0;
}
int gool_domain_release(gool_domain *d,gool_domain_lease lease) {
    gool_domain_region *r=lookup(d,lease);if(!r)return GOOL_DOMAIN_INVALID;
    if(!--r->references) {
        gool_domain_lease parent=r->parent;int overlay=r->overlay;
        *r=(gool_domain_region){0};
        if(overlay)return gool_domain_release(d,parent);
    }
    return 0;
}
int gool_domain_empty(const gool_domain *d) {
    if(!d)return GOOL_DOMAIN_INVALID;
    for(uint32_t i=0;i<GOOL_DOMAIN_REGIONS;i++)if(d->regions[i].references)return 0;
    return 1;
}
int gool_domain_references(const gool_domain *d,gool_domain_lease lease,uint32_t *count) {
    if(!d||!count||lease.index>=GOOL_DOMAIN_REGIONS||!lease.generation)return GOOL_DOMAIN_INVALID;
    const gool_domain_region *r=&d->regions[lease.index];
    if(!r->references||r->generation!=lease.generation)return GOOL_DOMAIN_INVALID;
    *count=r->references;return 0;
}
static int region(const gool_domain *d,uint32_t address,uint32_t size,uint32_t *index,uint32_t *offset) {
    if(!d||!address||(address&(size-1u))||(uint64_t)address+size>UINT64_C(0x100000000))
        return GOOL_DOMAIN_INVALID;
    uint64_t newest=0;
    for(uint32_t i=0;i<GOOL_DOMAIN_REGIONS;i++) {
        const gool_domain_region *r=&d->regions[i];
        if(r->references&&address>=r->address) {
            uint32_t delta=address-r->address;
            if(delta<=r->size&&size<=r->size-delta&&r->generation>newest) {
                *index=i;*offset=delta;newest=r->generation;
            }
        }
    }
    return newest?0:GOOL_DOMAIN_UNBOUND;
}
static int read_value(const gool_domain *d,uint32_t address,uint32_t size,uint32_t *value) {
    if(!value)return GOOL_DOMAIN_INVALID;
    uint32_t index,offset;int error=region(d,address,size,&index,&offset);if(error)return error;
    const uint8_t *p=d->regions[index].bytes+offset;uint32_t next=0;
    for(uint32_t i=0;i<size;i++)next|=(uint32_t)p[i]<<(i*8);
    *value=next;return 0;
}
int gool_domain_read8(const gool_domain *d,uint32_t address,uint8_t *value) {
    if(!value)return GOOL_DOMAIN_INVALID;
    uint32_t word;int error=read_value(d,address,1,&word);if(error)return error;
    *value=(uint8_t)word;return 0;
}
int gool_domain_read16(const gool_domain *d,uint32_t address,uint16_t *value) {
    if(!value)return GOOL_DOMAIN_INVALID;
    uint32_t word;int error=read_value(d,address,2,&word);if(error)return error;
    *value=(uint16_t)word;return 0;
}
int gool_domain_read32(const gool_domain *d,uint32_t address,uint32_t *value) {
    return read_value(d,address,4,value);
}
static int write_value(gool_domain *d,uint32_t address,uint32_t size,uint32_t value) {
    uint32_t index,offset;int error=region(d,address,size,&index,&offset);if(error)return error;
    uint8_t *p=d->regions[index].mutable_bytes;
    if(!p)return GOOL_DOMAIN_READ_ONLY;
    for(uint32_t i=0;i<size;i++)p[offset+i]=(uint8_t)(value>>(i*8));
    return 0;
}
int gool_domain_write8(gool_domain *d,uint32_t address,uint8_t value){return write_value(d,address,1,value);}
int gool_domain_write16(gool_domain *d,uint32_t address,uint16_t value){return write_value(d,address,2,value);}
int gool_domain_write32(gool_domain *d,uint32_t address,uint32_t value){return write_value(d,address,4,value);}
int gool_domain_read(void *context,uint32_t address,uint32_t *value){return gool_domain_read32(context,address,value);}
int gool_domain_write(void *context,uint32_t address,uint32_t value){return gool_domain_write32(context,address,value);}
