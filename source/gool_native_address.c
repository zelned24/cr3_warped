#include "gool_native_address.h"
static int32_t extend(uint32_t word,unsigned bits) {
    uint32_t value=word&((1u<<bits)-1u);
    return value&(1u<<(bits-1))?(int32_t)value-(int32_t)(1u<<bits):(int32_t)value;
}
int gool_native_address_resolve(void *context,uint32_t node,uint32_t raw,uint32_t *address) {
    gool_native_address *o=context;
    if(!o||!o->registry||!o->registry->domain||!node||(node&3u)||raw>=4096||!address||
       node>UINT32_MAX-0x60u)return GOOL_ARG_INVALID;
    gool_domain *domain=o->registry->domain;uint32_t base,offset,result;int error;
    if(!(raw&0x800u)) {
        error=gool_domain_read32(domain,node+((raw&0x400u)?0x14u:0x10u),&base);if(error)return error;
        error=gool_domain_read32(domain,base+0x18u,&base);if(error)return error;
        result=base+(raw&0x3ffu)*4u;
    } else if(raw&0x400u) {
        error=gool_domain_read32(domain,node+0x40u+((raw>>4)&0x1cu),&base);if(error)return error;
        result=base?base+0x40u+(raw&0x3fu)*4u:0;
    } else if((raw&0x380u)==0x380u) {
        if(raw!=0xbe0u&&raw!=0xbf0u)return GOOL_ARG_INVALID;
        result=raw==0xbf0u;
    } else if((raw&0x380u)==0x300u) {
        error=gool_domain_read32(domain,node+0xc4u,&base);if(error)return error;
        result=base+(uint32_t)(extend(raw,7)*4);
    } else {
        if(!o->toggle_address||(o->toggle_address&3u))return GOOL_ARG_UNBOUND;
        error=gool_domain_read32(domain,o->toggle_address,&base);if(error)return error;
        uint32_t next=base==0;result=0x1f800040u+next*4u;
        error=gool_domain_write32(domain,o->toggle_address,next);if(error)return error;
        offset=(uint32_t)((raw&0x200u)?extend(raw,8)*16:extend(raw,9)*256);
        error=gool_domain_write32(domain,result,offset);if(error)return error;
    }
    *address=result;return 0;
}
