#include "gool_signal.h"

int gool_signal_lookup(const gool_data *base,uint32_t raw_signal,gool_signal_route *out) {
    if(!base||!out)return -1;
    gool_signal_route next={.signal=raw_signal>>8};
    if(next.signal>=base->signal_count) {
        next.kind=GOOL_SIGNAL_OUTSIDE_TABLE;*out=next;return 0;
    }
    int32_t value;
    if(gool_data_signal(base,next.signal,&value))return -1;
    next.raw=(uint32_t)value&0xffffu;
    if(next.raw==255)next.kind=GOOL_SIGNAL_NONE;
    else if(next.raw&0x8000u) {
        next.kind=GOOL_SIGNAL_SCRIPT;next.value=next.raw&0x3fffu;
    } else {
        if(!base->states||next.raw>=base->state_count)return -1;
        next.kind=GOOL_SIGNAL_STATE;next.value=next.raw;
    }
    *out=next;return 0;
}
int gool_signal_script(const gool_data *base,const gool_signal_route *route,
                       gool_data *owner,uint32_t *pc) {
    if(!base||!route||!owner||!pc||route->kind!=GOOL_SIGNAL_SCRIPT||
       route->raw>65535||!(route->raw&0x8000u)||route->value!=(route->raw&0x3fffu))return -1;
    if(!base->code||route->value>=base->code_words)return -1;
    *owner=*base;*pc=route->value;return 0;
}
