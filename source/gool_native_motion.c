#include "gool_native_motion.h"

static int32_t signed_word(uint32_t value) {
    return value<=INT32_MAX?(int32_t)value:-1-(int32_t)(UINT32_MAX-value);
}
static uint32_t asr(uint32_t value,unsigned count) {
    return (value>>count)|((value&0x80000000u)?UINT32_MAX<<(32u-count):0);
}
static uint32_t trunc_shift(uint32_t value,unsigned count) {
    if(value&0x80000000u)value+=(1u<<count)-1u;
    return asr(value,count);
}
int gool_native_motion_angle(gool_domain *domain,uint32_t current,uint32_t target,
                             uint32_t rate,uint32_t actor,uint32_t *result) {
    if(!domain||!result||(actor&&(actor&3u)))return GOOL_DOMAIN_INVALID;
    uint32_t owner,factor,flags;
    int error=gool_domain_read32(domain,0x80061a7cu,&owner);if(error)return error;
    error=gool_domain_read32(domain,owner+0x18u,&factor);if(error)return error;
    if(factor>102u)factor=102u;
    uint32_t product=rate*factor;
    int64_t wide=(int64_t)signed_word(product)*signed_word(0x80808081u);
    uint32_t high=(uint32_t)((uint64_t)wide>>32);
    uint32_t step=asr(high+product,9)-asr(product,31);
    uint32_t delta=asr((target-current)<<20,20);
    uint32_t magnitude=(delta&0x80000000u)?0u-delta:delta;
    if(actor) {
        error=gool_domain_read32(domain,actor+0xacu,&flags);if(error)return error;
        if((flags&0x20000000u)&&signed_word(magnitude)<signed_word(step<<2)) {
            if(signed_word(magnitude)<signed_word(step<<1)) {
                step=trunc_shift(step,signed_word(magnitude)<signed_word(step)?3u:2u);
            } else if(signed_word(magnitude)<signed_word((step<<1)+step)) {
                step=trunc_shift(step,1);
            } else step=trunc_shift((step<<1)+step,2);
        }
    }
    uint32_t output;
    if(signed_word(magnitude)<signed_word(step)) {
        output=target;
        if(actor) {
            error=gool_domain_read32(domain,actor+0xa8u,&flags);if(error)return error;
            error=gool_domain_write32(domain,actor+0xa8u,flags|0x800u);if(error)return error;
        }
    } else {
        if(magnitude==0x800u&&signed_word(target)>=0x800)delta=0u-delta;
        output=((delta&0x80000000u)?current-step:current+step)&0xfffu;
        if(actor) {
            error=gool_domain_read32(domain,actor+0xa8u,&flags);if(error)return error;
            flags=(delta&0x80000000u)?flags|8u:flags&~8u;
            error=gool_domain_write32(domain,actor+0xa8u,flags);if(error)return error;
        }
    }
    *result=output;return 0;
}
