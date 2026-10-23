#include "gool_native_movement.h"
#include "gool_native_motion.h"
#include "gool_native_matrix.h"
#include "gool_trig.h"

static int32_t sign(uint32_t v){return v<=INT32_MAX?(int32_t)v:-1-(int32_t)(UINT32_MAX-v);}
static uint32_t asr(uint32_t v,unsigned n){return (v>>n)|((v&0x80000000u)?UINT32_MAX<<(32u-n):0);}
static uint32_t divide1020(uint32_t v) {
    int64_t wide=(int64_t)sign(v)*sign(0x80808081u);
    return asr((uint32_t)((uint64_t)wide>>32)+v,9)-asr(v,31);
}
static uint32_t clamp(uint32_t value,uint32_t bound) {
    if(sign(value)>=sign(bound))return bound;
    return sign(value)<sign(0u-bound)?0u-bound:value;
}
static uint32_t root(uint32_t value) {
    uint32_t result=0,remainder=0;
    for(unsigned i=0;i<16;i++) {
        remainder|=value>>30;value<<=2;
        uint32_t trial=remainder-((result<<2)+1u);result<<=1;
        if(sign(trial)>=0){result++;remainder=trial;}
        remainder<<=2;
    }
    return result;
}
static void pack(const int32_t matrix[3][3],uint32_t words[5]) {
    for(unsigned i=0;i<4;i++) {
        unsigned a=i*2,b=a+1;
        words[i]=((uint32_t)matrix[a/3][a%3]&65535u)|((uint32_t)matrix[b/3][b%3]<<16);
    }
    words[4]=(uint32_t)matrix[2][2];
}
int gool_native_movement_camera_vector(gool_domain *d,const uint32_t input[3],uint32_t output[3]) {
    if(!d||!input||!output)return GOOL_DOMAIN_INVALID;
    uint32_t first,second;
    int e=gool_domain_read32(d,0x80065d04u,&first);if(e)return e;
    int32_t s=gool_trig_sin(sign(0u-first)),c=gool_trig_cos(sign(0u-first));
    int32_t left[3][3]={{c,-s,0},{s,c,0},{0,0,4096}};
    e=gool_domain_read32(d,0x80065da4u,&second);if(e)return e;
    s=gool_trig_sin(sign(0u-second));c=gool_trig_cos(sign(0u-second));
    int32_t right[3][3]={{c,0,s},{0,4096,0},{-s,0,c}};
    uint32_t a[5],b[5],matrix[5],next[3];pack(left,a);pack(right,b);
    e=gool_native_matrix_41ef0(b,a,matrix);if(e)return e;
    for(unsigned row=0;row<3;row++) {
        uint32_t sum=0;
        for(unsigned col=0;col<3;col++) {
            unsigned index=col*3+row;
            uint32_t half=(matrix[index/2]>>((index&1u)*16))&65535u;
            int32_t coefficient=half<32768u?(int32_t)half:(int32_t)half-65536;
            sum+=(uint32_t)coefficient*input[col];
        }
        next[row]=asr(sum,12);
    }
    for(unsigned i=0;i<3;i++)output[i]=next[i];
    return 0;
}
int gool_native_movement_control(gool_domain *d,uint32_t node,uint32_t parameters,uint32_t previous) {
    if(!d||!node||!parameters||!previous||((node|parameters|previous)&3u))return GOOL_DOMAIN_INVALID;
    uint32_t strength,direction,angle,flags,v,rate,current,owner,factor;
    int e=gool_domain_read32(d,0x80065c08u,&strength);if(e)return e;
    e=gool_domain_read32(d,0x80065c0cu,&direction);if(e)return e;
    e=gool_domain_read32(d,0x80065c04u,&angle);if(e)return e;
    if(sign(strength)<153) {strength=0;direction=8;}
    else {
        e=gool_domain_read32(d,node+0xacu,&flags);if(e)return e;
        if(!(flags&0x80u)){strength=0;direction=8;}
        else {
            e=gool_domain_read32(d,0x80065da4u,&v);if(e)return e;
            e=gool_domain_read32(d,node+0x94u,&current);if(e)return e;
            e=gool_domain_read32(d,parameters+0x14u,&rate);if(e)return e;
            e=gool_native_motion_angle(d,current,(v+angle)&4095u,rate,0,&v);if(e)return e;
            e=gool_domain_write32(d,node+0x94u,v);if(e)return e;
        }
    }
    e=gool_domain_read32(d,0x80061a7cu,&owner);if(e)return e;
    e=gool_domain_read32(d,owner+0x18u,&factor);if(e)return e;
    if(factor>102u)factor=102u;
    e=gool_domain_read32(d,parameters,&v);if(e)return e;
    strength=asr(strength*divide1020(v*factor),8);
    uint32_t input[3]={asr((uint32_t)sign(asr((uint32_t)gool_trig_sin(sign(angle)),4))*strength,8),0,
                       asr((uint32_t)sign(asr((uint32_t)gool_trig_cos(sign(angle)),4))*strength,8)};
    uint32_t delta[3];e=gool_native_movement_camera_vector(d,input,delta);if(e)return e;
    e=gool_domain_read32(d,node+0x84u,&v);if(e)return e;
    e=gool_domain_write32(d,node+0x84u,v+delta[0]);if(e)return e;
    e=gool_domain_read32(d,node+0x8cu,&v);if(e)return e;
    e=gool_domain_write32(d,node+0x8cu,v+delta[2]);if(e)return e;
    if(direction==8u)flags=0;
    else {e=gool_domain_read32(d,node+0xa8u,&flags);if(e)return e;}
    if(direction==8u||!(flags&0x02000000u)) {
        e=gool_domain_read32(d,node+0xa8u,&flags);if(e)return e;
        e=gool_domain_read32(d,parameters+8u,&rate);if(e)return e;
        if(!(flags&0x02000000u))rate=199u;
        e=gool_domain_read32(d,node+0x84u,&v);if(e)return e;
        uint32_t x=asr(v*rate,8);
        e=gool_domain_read32(d,node+0x8cu,&v);if(e)return e;
        uint32_t z=asr(v*rate,8);
        e=gool_domain_write32(d,node+0x84u,x);if(e)return e;
        e=gool_domain_write32(d,node+0x8cu,z);if(e)return e;
    }
    e=gool_domain_read32(d,node+0x84u,&v);if(e)return e;
    e=gool_domain_read32(d,parameters+4u,&rate);if(e)return e;
    uint32_t x=clamp(v,rate);
    e=gool_domain_read32(d,node+0x8cu,&v);if(e)return e;
    e=gool_domain_write32(d,node+0x84u,x);if(e)return e;
    e=gool_domain_read32(d,parameters+4u,&rate);if(e)return e;
    e=gool_domain_write32(d,node+0x8cu,clamp(v,rate));if(e)return e;
    e=gool_domain_read32(d,node+0x60u,&v);if(e)return e;
    e=gool_domain_read32(d,previous,&current);if(e)return e;
    x=asr(v-current,6);
    e=gool_domain_read32(d,node+0x68u,&v);if(e)return e;
    e=gool_domain_read32(d,previous+8u,&current);if(e)return e;
    uint32_t z=asr(v-current,6),speed=root(x*x+z*z)*7680u;
    e=gool_domain_write32(d,node+0x104u,speed);if(e)return e;
    if(sign(speed)<25600) {
        if(direction!=8u)return gool_domain_write32(d,node+0x104u,25600u);
    } else {
        e=gool_domain_read32(d,parameters+4u,&rate);if(e)return e;
        if(sign(rate)<sign(speed))return gool_domain_write32(d,node+0x104u,rate);
    }
    return 0;
}
