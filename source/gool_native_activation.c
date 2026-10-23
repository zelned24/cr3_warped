#include "gool_native_activation.h"
#define TABLE_BASE 0x80067a38u
static int32_t signed_word(uint32_t value) {
    return value<=INT32_MAX?(int32_t)value:-1-(int32_t)(UINT32_MAX-value);
}
static int begin(gool_native_activation *o,gool_domain *d,uint32_t gp,uint32_t capacity) {
    if(!o||o->opened||!d||!gp||(gp&3u)||gp>UINT32_MAX-0x21fu||!capacity||
       (uint64_t)TABLE_BASE+(uint64_t)capacity*12u>UINT64_C(0x100000000))
        return GOOL_NATIVE_ACTIVATION_INVALID;
    *o=(gool_native_activation){.domain=d,.gp=gp,.capacity=capacity,.opened=1,
        .status=GOOL_NATIVE_ACTIVATION_PENDING};return o->status;
}
int gool_native_activation_reset_begin(gool_native_activation *o,gool_domain *d,uint32_t gp,uint32_t capacity) {
    int error=begin(o,d,gp,capacity);if(error<0)return error;
    o->reset=1;return error;
}
int gool_native_activation_register_begin(gool_native_activation *o,gool_domain *d,uint32_t gp,
    uint32_t capacity,uint32_t marker,uint32_t list,uint32_t count) {
    if(count>INT32_MAX||(count&&(!list||(list&3u))))return GOOL_NATIVE_ACTIVATION_INVALID;
    int error=begin(o,d,gp,capacity);if(error<0)return error;
    o->marker=marker;o->remaining=count;o->list=list+(count<<2)-4u;return error;
}
int gool_native_activation_step(gool_native_activation *o,uint32_t budget) {
    if(!o||!o->opened)return GOOL_NATIVE_ACTIVATION_INVALID;
    if(o->status!=GOOL_NATIVE_ACTIVATION_PENDING)return o->status;
    while(budget--) {
        int error;uint32_t row=TABLE_BASE+o->index*12u;
        if(o->phase==0) {
            if(!o->reset&&!o->remaining)return o->status=GOOL_NATIVE_ACTIVATION_DONE;
            error=gool_domain_read32(o->domain,o->gp+0x21cu,&o->raw_count);if(error)return o->status=error;
            o->count=signed_word(o->raw_count)>0?o->raw_count:0;o->index=0;
            if(o->count>o->capacity)return o->status=GOOL_NATIVE_ACTIVATION_CAPACITY;
            o->phase=1;
        } else if(o->reset) {
            if(o->index==o->count)return o->status=GOOL_NATIVE_ACTIVATION_DONE;
            error=gool_domain_write32(o->domain,row+8u,0x13bu);if(error)return o->status=error;
            o->index++;
        } else if(o->phase==1) {
            if(o->index==o->count) {
                /* Empty/signed-negative scans branch straight to1AD24 with
                 * the first count still in V0; only a nonempty scan reloads. */
                o->phase=o->count?3u:(o->raw_count?5u:4u);continue;
            }
            uint32_t key;
            error=gool_domain_read32(o->domain,row,&key);if(error)return o->status=error;
            error=gool_domain_read32(o->domain,o->list,&o->value);if(error)return o->status=error;
            if((key&0x00ffff00u)==(o->value&0x00ffff00u))o->phase=2;
            else o->index++;
        } else if(o->phase==2) {
            error=gool_domain_write32(o->domain,row,o->value);if(error)return o->status=error;
            error=gool_domain_write32(o->domain,row+8u,o->marker);if(error)return o->status=error;
            o->phase=3;
        } else if(o->phase==3) {
            uint32_t count;
            error=gool_domain_read32(o->domain,o->gp+0x21cu,&count);if(error)return o->status=error;
            if(o->index==count)o->phase=4;else o->phase=5;
        } else if(o->phase==4) {
            if(o->index>=o->capacity)return o->status=GOOL_NATIVE_ACTIVATION_CAPACITY;
            uint32_t value;
            error=gool_domain_read32(o->domain,o->list,&value);if(error)return o->status=error;
            error=gool_domain_write32(o->domain,row+8u,o->marker);if(error)return o->status=error;
            error=gool_domain_write32(o->domain,row+4u,0);if(error)return o->status=error;
            error=gool_domain_write32(o->domain,o->gp+0x21cu,o->index+1u);if(error)return o->status=error;
            error=gool_domain_write32(o->domain,row,value);if(error)return o->status=error;
            o->phase=5;
        } else if(o->phase==5) {
            o->remaining--;o->list-=4u;o->phase=0;
        } else return o->status=GOOL_NATIVE_ACTIVATION_INVALID;
    }
    return GOOL_NATIVE_ACTIVATION_PENDING;
}
int gool_native_activation_close(gool_native_activation *o) {
    if(!o)return GOOL_NATIVE_ACTIVATION_INVALID;
    *o=(gool_native_activation){0};return 0;
}
