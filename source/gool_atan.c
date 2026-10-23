#include "gool_atan.h"
#include "gool_atan_table.h"

static int32_t signed_bits(uint32_t v) {
    return v<=INT32_MAX?(int32_t)v:(int32_t)((int64_t)v-INT64_C(4294967296));
}
static int32_t shift10(uint32_t v) {
    uint32_t bits=v>>10;
    if(v&0x80000000u)bits|=0xffc00000u;
    return signed_bits(bits);
}
int gool_atan2(int32_t y,int32_t x,int32_t *result) {
    if(!result)return -1;
    int negative_x=x<0,negative_y=y<0;
    uint32_t a=negative_y?0u-(uint32_t)y:(uint32_t)y;
    uint32_t b=negative_x?0u-(uint32_t)x:(uint32_t)x;
    if(!a&&!b){*result=0;return 0;}
    int swapped=signed_bits(a)>=signed_bits(b);
    uint32_t smaller=swapped?b:a,larger=swapped?a:b;
    int32_t numerator,denominator;
    if(smaller&0x7fe00000u) {
        numerator=signed_bits(smaller);denominator=shift10(larger);
    } else {
        numerator=signed_bits(smaller<<10);denominator=signed_bits(larger);
    }
    if(!denominator||(numerator==INT32_MIN&&denominator==-1))return -1;
    int32_t index=(int32_t)((int64_t)numerator/denominator);
    if(index<0||index>1024)return -1;
    int32_t angle=gool_atan_ratio[index];
    if(swapped)angle=1024-angle;
    if(negative_x)angle=2048-angle;
    if(negative_y)angle=-angle;
    *result=angle;return 0;
}
