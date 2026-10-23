#include "gool_native_shading.h"

/* SCUS_942.44 SHA2561b93cc563c90966e1d79b750f7d19bc16628559cceeb991775e9e9a22656d71d1,
 * original address8005D268,128 bytes. Native3DB94 clamps lookup to127. */
static const uint8_t highlight_table[128]={
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x03,0x03,0x03,0x03,0x03,0x04,0x04,0x04,0x04,
    0x05,0x05,0x05,0x06,0x06,0x07,0x07,0x08,0x08,0x09,0x09,0x0a,0x0b,0x0b,0x0c,0x0d,
    0x0e,0x0f,0x10,0x11,0x12,0x13,0x14,0x16,0x17,0x19,0x1a,0x1c,0x1e,0x20,0x22,0x24,
    0x27,0x29,0x2c,0x2f,0x32,0x35,0x38,0x3c,0x40,0x44,0x48,0x4c,0x51,0x56,0x5c,0x62,
    0x68,0x6e,0x75,0x7c,0x84,0x8c,0x95,0x9e,0xa8,0xb3,0xbe,0xc9,0xd5,0xe3,0xf0,0xff
};
static int32_t signed_word(uint32_t v){return v<=INT32_MAX?(int32_t)v:-1-(int32_t)(UINT32_MAX-v);}
static int32_t half(uint32_t v){v&=0xffffu;return v<=INT16_MAX?(int32_t)v:(int32_t)v-65536;}
static int32_t byte(uint32_t v){v&=255u;return v<128u?(int32_t)v:(int32_t)v-256;}
static int64_t shift(int64_t v,uint32_t n) {
    int64_t d=INT64_C(1)<<n;return v<0?-1-((-1-v)/d):v/d;
}
static int32_t clamp_ir(int32_t value,uint32_t axis,uint32_t *flags) {
    if(value<-32768){*flags|=1u<<(24u-axis);return -32768;}
    if(value>32767){*flags|=1u<<(24u-axis);return 32767;}
    return value;
}
static uint32_t finish_flags(uint32_t flags){return flags|((flags&0x7f87e000u)?0x80000000u:0u);}
int gool_native_shading_map(gool_domain *d,gool_domain_lease *lease) {
    return gool_domain_map_readonly(d,0x8005d268u,highlight_table,sizeof(highlight_table),lease);
}
int gool_native_shading_depthcue(uint32_t rgba,const uint32_t far[3],uint16_t coefficient,
    gool_native_shading_color *output) {
    if(!far||!output)return GOOL_NATIVE_SHADING_INVALID;
    gool_native_shading_color result={.rgba=rgba&0xff000000u};
    int32_t factor=half(coefficient);
    for(uint32_t axis=0;axis<3;axis++) {
        int64_t base=(int64_t)((rgba>>(axis*8u))&255u)*65536;
        int64_t difference=(int64_t)signed_word(far[axis])*4096-base;
        if(difference>=(INT64_C(1)<<43))result.flags|=1u<<(30u-axis);
        if(difference< -(INT64_C(1)<<43))result.flags|=1u<<(27u-axis);
        int32_t intermediate=clamp_ir(signed_word((uint32_t)shift(difference,12)),axis,&result.flags);
        int64_t combined=base+(int64_t)intermediate*factor;
        result.mac[axis]=signed_word((uint32_t)shift(combined,12));
        result.ir[axis]=(int16_t)clamp_ir(result.mac[axis],axis,&result.flags);
        int64_t color=shift(result.mac[axis],4);
        if(color<0){color=0;result.flags|=1u<<(21u-axis);}
        if(color>255){color=255;result.flags|=1u<<(21u-axis);}
        result.rgba|=(uint32_t)color<<(axis*8u);
    }
    result.flags=finish_flags(result.flags);*output=result;return 0;
}
int gool_native_shading_normal(gool_domain *d,const gool_native_shading_controls *c,
    uint32_t normal,gool_native_shading_result *output) {
    if(!d||!c||!output||(c->backface!=0&&c->backface!=1))return GOOL_NATIVE_SHADING_INVALID;
    int32_t v[3]={byte(normal)*32,byte(normal>>8)*32,byte(normal>>16)*32};
    gool_native_shading_result result={0};
    for(uint32_t row=0;row<3;row++) {
        int64_t dot=0;
        for(uint32_t col=0;col<3;col++) {
            uint32_t index=row*3u+col;
            dot+=(int64_t)half(c->light_matrix[index/2u]>>((index&1u)*16u))*v[col];
        }
        result.normal_ir[row]=clamp_ir((int32_t)shift(dot,12),row,&result.normal_flags);
    }
    result.normal_flags=finish_flags(result.normal_flags);
    uint32_t factor=(uint32_t)result.normal_ir[0]+4096u;
    factor=(factor+(factor<<1)+(factor<<2)+8192u)>>4;
    if(c->backface)factor>>=1;
    factor-=c->packet_flags&0xffffu;if(signed_word(factor)<0)factor=0;
    result.coefficient=factor;
    int error=gool_native_shading_depthcue(c->rgba,c->far_color,(uint16_t)factor,&result.depth_cued);
    if(error)return error;
    int32_t highlight=(int32_t)shift(result.normal_ir[2],3);
    if(highlight<0)highlight=-highlight;
    highlight-=384;
    if(highlight>=0) {
        uint8_t value;
        error=gool_domain_read8(d,0x8005d268u+(highlight>=128?127u:(uint32_t)highlight),&value);
        if(error)return error;
        result.highlight=c->backface?value>>3:value;
    }
    for(uint32_t axis=0;axis<3;axis++) {
        uint32_t color=((result.depth_cued.rgba>>(axis*8u))&255u)+result.highlight;
        if(color>255u)color=255u;
        result.rgb|=color<<(axis*8u);
    }
    *output=result;return 0;
}
