#include "zdat_lookup.h"
static int32_t sign(uint32_t value){return value<=INT32_MAX?(int32_t)value:-1-(int32_t)(UINT32_MAX-value);}
int zdat_lookup_contains(gool_domain *domain,uint32_t entry,uint32_t point,int *contains) {
    if(!domain||!entry||(entry&3u)||!point||(point&3u)||!contains||
       entry>UINT32_MAX-0x14u||point>UINT32_MAX-8u)return ZDAT_RESOURCE_INVALID;
    uint32_t box,origin[3],position[3];
    int error=gool_domain_read32(domain,entry+0x14,&box);if(error)return error;
    if(!box||(box&3u)||box>UINT32_MAX-0x14u)return ZDAT_RESOURCE_INVALID;
    for(uint32_t i=0;i<3;i++) {
        error=gool_domain_read32(domain,box+i*4,&origin[i]);if(error)return error;
        error=gool_domain_read32(domain,point+i*4,&position[i]);if(error)return error;
        if(sign(position[i])<sign(origin[i]<<8)){*contains=0;return 0;}
    }
    for(uint32_t i=0;i<3;i++) {
        uint32_t extent;error=gool_domain_read32(domain,box+0xc+i*4,&extent);if(error)return error;
        if(sign(position[i])>=sign((origin[i]+extent)<<8)){*contains=0;return 0;}
    }
    *contains=1;return 0;
}
static int search(zdat_resources *bank,uint32_t source,uint32_t point,uint32_t limit,
                    uint32_t *found,zdat_resource_lease *lease) {
    gool_domain *domain=bank->domain;uint32_t header,count;
    int error=gool_domain_read32(domain,source+0x10,&header);if(error)return error;
    if(!header||(header&3u)||header>UINT32_MAX-0x1d4u)return ZDAT_RESOURCE_INVALID;
    error=gool_domain_read32(domain,header+0x190,&count);if(error)return error;
    if(sign(count-1u)<0)return 0;
    if(count>limit||(uint64_t)header+0x1d4u+(count-1u)*4u>UINT32_MAX)return ZDAT_RESOURCE_INVALID;
    while(count--) {
        uint32_t flags,reference,address;zdat_resource_lease candidate={0};
        error=gool_domain_read32(domain,header+0x1d4+count*4u,&flags);if(error)return error;
        if(!(flags&4u))continue;
        error=gool_domain_read32(domain,header+0x194+count*4u,&reference);if(error)return error;
        if(reference&1u)error=zdat_resources_acquire(bank,reference,&address,&candidate);
        else {address=reference;error=zdat_resources_acquire_address(bank,address,&candidate);}
        if(error)return error;
        int contains;error=zdat_lookup_contains(domain,address,point,&contains);
        if(!error&&contains){*found=address;*lease=candidate;return 0;}
        int cleanup=zdat_resources_release(bank,candidate);
        if(cleanup)return cleanup;
        if(error)return error;
    }
    return 0;
}
int zdat_lookup_find(zdat_resources *bank,uint32_t initial,uint32_t point,
    uint32_t limit,int32_t *result,zdat_resource_lease *lease) {
    if(!bank||!bank->open||bank->closing||!initial||(initial&3u)||initial>UINT32_MAX-0x10u||
       !point||(point&3u)||!limit||!result||!lease||lease->generation)return ZDAT_RESOURCE_INVALID;
    uint32_t found=0;zdat_resource_lease selected={0};
    int error=search(bank,initial,point,limit,&found,&selected);if(error)return error;
    if(!found) {
        uint32_t current;error=gool_domain_read32(bank->domain,0x80065d5cu,&current);if(error)return error;
        if(current!=initial) {
            if(!current||(current&3u)||current>UINT32_MAX-0x10u)return ZDAT_RESOURCE_INVALID;
            error=search(bank,current,point,limit,&found,&selected);if(error)return error;
        }
    }
    *result=found?sign(found):-23;*lease=selected;return 0;
}
