#include "model_visibility.h"

static int32_t signed_word(uint32_t v){return v<=INT32_MAX?(int32_t)v:-1-(int32_t)(UINT32_MAX-v);}
int model_visibility_native(uint32_t command,uint32_t packet_flags,const int16_t x[3],
    const int16_t y[3],const uint16_t depths[3],int16_t zsf3,model_visibility *output) {
    if(!x||!y||!depths||!output)return MODEL_PROJECT_INVALID;
    model_visibility result={0};
    if(command&0x10000000u) {
        int error=model_project_nclip(x,y,&result.area,&result.area_flags);if(error)return error;
        result.area_written=1;
        uint32_t oriented=result.area^packet_flags^(command<<2);
        result.backface=(oriented&0x80000000u)!=0;
        if(!result.area||signed_word(oriented)<=0){*output=result;return 0;}
    }
    uint32_t sxy[3];
    for(uint32_t i=0;i<3;i++)sxy[i]=(uint16_t)x[i]|((uint32_t)(uint16_t)y[i]<<16);
    uint32_t intersection=sxy[0]&sxy[1]&sxy[2];
    model_project_order_depth depth;
    int error=model_project_average_depth(depths,3,zsf3,&depth);if(error)return error;
    result.depth_written=1;result.depth_mac=depth.mac0;result.depth_flags=depth.flags;result.order_depth=depth.depth;
    uint32_t outside=~((sxy[0]-0x00d90200u)|(sxy[1]-0x00d90200u)|(sxy[2]-0x00d90200u));
    outside|=intersection;result.screen_reject=outside;
    result.admitted=(outside&0x80008000u)==0;
    *output=result;return 0;
}
