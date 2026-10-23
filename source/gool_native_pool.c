#include "gool_native_pool.h"

static void put32(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(value>>(i*8));
}
static int map(gool_native_pool *p,uint32_t address,void *bytes,uint32_t size) {
    if(p->mapped>=GOOL_NATIVE_POOL_REGIONS)return GOOL_DOMAIN_FULL;
    int error=gool_domain_map_mutable(p->domain,address,bytes,size,&p->regions[p->mapped]);
    if(!error)p->mapped++;
    return error;
}
int gool_native_pool_can_close(const gool_native_pool *p) {
    if(!p)return GOOL_DOMAIN_INVALID;
    if(p->mapped&&!p->domain)return GOOL_DOMAIN_INVALID;
    for(uint32_t i=0;i<p->mapped;i++) {
        uint32_t references;int error=gool_domain_references(p->domain,p->regions[i],&references);
        if(error)return error;
        if(references!=1)return GOOL_DOMAIN_BUSY;
    }
    return 0;
}
int gool_native_pool_close(gool_native_pool *p) {
    int ready=gool_native_pool_can_close(p);if(ready)return ready;
    while(p->mapped) {
        int error=gool_domain_release(p->domain,p->regions[p->mapped-1]);
        if(error)return error;
        p->mapped--;
    }
    p->domain=0;p->open=0;return 0;
}
int gool_native_pool_open(gool_native_pool *p,gool_domain *domain) {
    if(!p||!domain||p->open||p->mapped||p->domain)return GOOL_DOMAIN_INVALID;
    p->domain=domain;int error;
    for(uint32_t i=0;i<GOOL_NATIVE_POOL_COUNT;i++) {
        error=map(p,GOOL_NATIVE_POOL_BASE+i*GOOL_NATIVE_POOL_STRIDE,p->objects[i],GOOL_NATIVE_POOL_STRIDE);
        if(error)goto failed;
    }
    error=map(p,GOOL_NATIVE_PLAYER_BASE,p->player,sizeof(p->player));if(error)goto failed;
    error=map(p,0x80068e98u,p->pointers,sizeof(p->pointers));if(error)goto failed;
    error=map(p,0x80068ea0u,p->roots,sizeof(p->roots));if(error)goto failed;
    error=map(p,0x80060a90u,p->protected_object,sizeof(p->protected_object));if(error)goto failed;
    error=map(p,0x80069c48u,p->player_aux,sizeof(p->player_aux));if(error)goto failed;
    error=map(p,0x80069cb4u,p->object_aux,sizeof(p->object_aux));if(error)goto failed;
    /* Match explicit native bootstrap writes; untouched recycled storage stays. */
    put32(p->pointers,GOOL_NATIVE_PLAYER_BASE);
    put32(p->pointers+4,GOOL_NATIVE_POOL_BASE);
    put32(p->protected_object,0);
    for(uint32_t i=0;i<8;i++) {
        put32(p->roots+i*8,2);put32(p->roots+i*8+4,0);
    }
    put32(p->roots+64,2);put32(p->roots+68,GOOL_NATIVE_POOL_BASE);
    put32(p->roots+72,0);put32(p->roots+76,0);
    for(uint32_t i=0;i<GOOL_NATIVE_POOL_COUNT;i++) {
        uint8_t *slot=p->objects[i];
        put32(slot,0);put32(slot+8,0x80069cb4u+i*0x30u);
        put32(slot+0x44,0x80068ee0u);put32(slot+0x4c,0);
        put32(slot+0x48,i+1<GOOL_NATIVE_POOL_COUNT?
              GOOL_NATIVE_POOL_BASE+(i+1)*GOOL_NATIVE_POOL_STRIDE:0);
    }
    put32(p->player,0);put32(p->player+8,0x80069c48u);
    put32(p->player+0x44,0);put32(p->player+0x48,0);put32(p->player+0x4c,0);
    p->open=1;return 0;
failed: {
        int cleanup=gool_native_pool_close(p);return cleanup?cleanup:error;
    }
}
int gool_native_pool_object(gool_native_pool *p,uint32_t index,
                            uint32_t *address,gool_domain_lease *lease) {
    if(!p||!p->open||!address||!lease||index>GOOL_NATIVE_POOL_COUNT)
        return GOOL_DOMAIN_INVALID;
    *address=index==GOOL_NATIVE_POOL_COUNT?GOOL_NATIVE_PLAYER_BASE:
             GOOL_NATIVE_POOL_BASE+index*GOOL_NATIVE_POOL_STRIDE;
    *lease=p->regions[index];return 0;
}

int gool_native_pool_enter_frame(void *context,uint32_t node,uint32_t argc,uint32_t flags) {
    gool_native_pool *p=context;
    if(!p||!p->open||argc>256)return GOOL_DOMAIN_INVALID;
    uint32_t index,size;
    if(node==GOOL_NATIVE_PLAYER_BASE){index=GOOL_NATIVE_POOL_COUNT;size=GOOL_NATIVE_PLAYER_BYTES;}
    else {
        if(node<GOOL_NATIVE_POOL_BASE)return GOOL_DOMAIN_INVALID;
        uint32_t offset=node-GOOL_NATIVE_POOL_BASE;
        if(offset%GOOL_NATIVE_POOL_STRIDE||offset/GOOL_NATIVE_POOL_STRIDE>=GOOL_NATIVE_POOL_COUNT)
            return GOOL_DOMAIN_INVALID;
        index=offset/GOOL_NATIVE_POOL_STRIDE;size=GOOL_NATIVE_POOL_STRIDE;
    }
    uint32_t references;int error=gool_domain_references(p->domain,p->regions[index],&references);
    if(error)return error;
    if(references!=1)return GOOL_DOMAIN_BUSY;
    uint32_t behavior,header,start,sp,fp,pc;
    error=gool_domain_read32(p->domain,node+0xc,&behavior);if(error)return error;
    if(behavior>UINT32_MAX-0x10)return GOOL_DOMAIN_INVALID;
    error=gool_domain_read32(p->domain,behavior+0x10,&header);if(error)return error;
    if(header>UINT32_MAX-0xc)return GOOL_DOMAIN_INVALID;
    error=gool_domain_read32(p->domain,header+0xc,&start);if(error)return error;
    if(start>(size-0x40)/4)return GOOL_DOMAIN_INVALID;
    uint32_t begin=node+0x40+start*4,end=node+size;
    error=gool_domain_read32(p->domain,node+0xbc,&sp);if(error)return error;
    error=gool_domain_read32(p->domain,node+0xc4,&fp);if(error)return error;
    error=gool_domain_read32(p->domain,node+0xc0,&pc);if(error)return error;
    if((sp&3u)||(fp&3u)||sp<begin||sp>end-12||argc>(sp-begin)/4||
       fp<node+0x40||fp>sp||(fp!=node+0x40&&(fp<begin||fp>end-12)))
        return GOOL_DOMAIN_INVALID;
    uint32_t links=((fp-node-0x40)<<16)|(sp-argc*4-node-0x40);
    error=gool_domain_write32(p->domain,node+0xc4,sp);if(error)return error;
    error=gool_domain_write32(p->domain,sp,flags);if(error)return error;
    error=gool_domain_write32(p->domain,node+0xbc,sp+4);if(error)return error;
    error=gool_domain_write32(p->domain,sp+4,pc);if(error)return error;
    error=gool_domain_write32(p->domain,node+0xbc,sp+8);if(error)return error;
    error=gool_domain_write32(p->domain,sp+8,links);if(error)return error;
    return gool_domain_write32(p->domain,node+0xbc,sp+12);
}

int gool_native_pool_object_end(void *context,uint32_t node,uint32_t *end) {
    gool_native_pool *p=context;
    if(!p||!p->open||!end)return GOOL_DOMAIN_INVALID;
    if(node==GOOL_NATIVE_PLAYER_BASE){*end=node+GOOL_NATIVE_PLAYER_BYTES;return 0;}
    if(node<GOOL_NATIVE_POOL_BASE)return GOOL_DOMAIN_INVALID;
    uint32_t offset=node-GOOL_NATIVE_POOL_BASE;
    if(offset%GOOL_NATIVE_POOL_STRIDE||offset/GOOL_NATIVE_POOL_STRIDE>=GOOL_NATIVE_POOL_COUNT)
        return GOOL_DOMAIN_INVALID;
    *end=node+GOOL_NATIVE_POOL_STRIDE;return 0;
}
