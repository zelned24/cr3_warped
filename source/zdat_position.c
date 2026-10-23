#include "zdat_position.h"

static int32_t signed_word(uint32_t n) {
    return n<=INT32_MAX?(int32_t)n:-1-(int32_t)(UINT32_MAX-n);
}
int zdat_position_native(const int16_t point[3],const int32_t offset[3],
                          uint32_t shift,int32_t out[3]) {
    if(!point||!offset||!out)return ZDAT_INVALID;
    int32_t next[3];
    for(unsigned i=0;i<3;i++) {
        uint32_t scaled=(uint32_t)(int32_t)point[i]<<(shift&31u);
        next[i]=signed_word((scaled+(uint32_t)offset[i])<<8);
    }
    for(unsigned i=0;i<3;i++)out[i]=next[i];
    return 0;
}
int zdat_entity_position_native(const zdat_zone *zone,const zdat_entity *entity,
                                 uint32_t point,int32_t out[3]) {
    if(!zone||!entity||!out||!zone->has_offset||!entity->path.bytes.data)
        return ZDAT_INVALID;
    if((entity->path.format&31u)!=6u)return ZDAT_FORMAT;
    int32_t raw[3];uint32_t components;
    int error=zdat_series_value(&entity->path,0,point,raw,&components);
    if(error)return error;
    if(components!=3)return ZDAT_FORMAT;
    int16_t halfwords[3];
    for(unsigned i=0;i<3;i++) {
        if(raw[i]<INT16_MIN||raw[i]>INT16_MAX)return ZDAT_RANGE;
        halfwords[i]=(int16_t)raw[i];
    }
    return zdat_position_native(halfwords,zone->offset,(uint32_t)entity->shift,out);
}
