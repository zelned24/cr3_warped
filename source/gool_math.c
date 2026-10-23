#include "gool_math.h"

static int32_t signed_word(uint32_t value) {
    return value <= INT32_MAX ? (int32_t)value :
           (int32_t)((int64_t)value - INT64_C(4294967296));
}
static int32_t arithmetic_shift(uint32_t value, unsigned count) {
    uint32_t shifted = value >> count;
    if (value & UINT32_C(0x80000000))
        shifted |= UINT32_MAX << (32 - count);
    return signed_word(shifted);
}
int32_t gool_math_approach(int32_t current, int32_t target, int32_t rate) {
    uint32_t c = (uint32_t)current, t = (uint32_t)target, r = (uint32_t)rate;
    if (rate < 0) r = 0u - r;
    uint32_t delta = t - c;
    if (signed_word(delta) < 0) {
        if (signed_word(delta + r) >= 0) return target;
        return signed_word(c - r);
    }
    if (signed_word(delta - r) <= 0) return target;
    return signed_word(c + r);
}
int32_t gool_math_approach_angle(int32_t current, int32_t target,
                                 int32_t rate, int32_t frame_factor) {
    uint32_t c = (uint32_t)current, t = (uint32_t)target;
    int32_t delta = arithmetic_shift((t << 20) - (c << 20), 20);
    uint32_t magnitude = delta < 0 ? 0u - (uint32_t)delta : (uint32_t)delta;
    /* MULT/MFLO discards the upper product before the arithmetic shift. */
    uint32_t step = (uint32_t)arithmetic_shift((uint32_t)frame_factor *
                                              (uint32_t)rate, 10);
    if (signed_word(magnitude - step) <= 0) return target;
    return (int32_t)((delta < 0 ? c - step : c + step) & 0xfffu);
}

int32_t gool_math_integrate(int32_t current,int32_t velocity,int32_t factor) {
    uint32_t step=(uint32_t)arithmetic_shift((uint32_t)velocity*(uint32_t)factor,10);
    return signed_word((uint32_t)current+step);
}
int32_t gool_math_oscillate(int32_t current,int32_t bound,int32_t rate,int32_t *next) {
    uint32_t c=(uint32_t)current,b=(uint32_t)bound,r=(uint32_t)rate,n;
    if(bound>=0) {
        n=signed_word(c-b+r)<0?c+r:r+r-b;
        *next=signed_word(n);
        return signed_word(n&0x80000000u?0u-n:n);
    }
    n=signed_word(c-b-r)>0?c-r:0u-b-r-r;
    *next=signed_word(n);
    return signed_word(n&0x80000000u?n:0u-n);
}
