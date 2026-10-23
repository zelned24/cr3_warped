#include "gool_native_primitive.h"

static int32_t signed_word(uint32_t value) {
    return value<=INT32_MAX?(int32_t)value:-1-(int32_t)(UINT32_MAX-value);
}
int gool_native_primitive_bucket(uint32_t base,uint32_t depth_base,uint32_t depth_mac,uint32_t *bucket) {
    if(!base||(base&3u)||!bucket)return GOOL_NATIVE_PRIMITIVE_INVALID;
    uint32_t delta=depth_base-(depth_mac>>17),offset=delta<<2;
    if(signed_word(delta)<0)offset=0;
    /* The MIPS compares ADDIU(offset,-1FFC), including its wrapping sign,
     * rather than comparing an unbounded host depth against a clamp. */
    if(signed_word(offset-0x1ffcu)>0)offset=0x1ffcu;
    *bucket=base+offset;return 0;
}
static int extent(uint32_t primitive,uint32_t end,uint32_t bytes) {
    return primitive&&!(primitive&3u)&&!(end&3u)&&primitive<=end&&bytes<=end-primitive;
}
int gool_native_primitive_rgb(gool_domain *d,uint32_t primitive,uint32_t end,
    const uint32_t sxy[3],const uint32_t colors[3],uint32_t uv_end,gool_native_primitive_size *output) {
    uint32_t bytes=uv_end?40u:28u;
    if(!d||!sxy||!colors||!output||!extent(primitive,end,bytes)||
       (uv_end&&((uv_end&3u)||uv_end<12u)))return GOOL_NATIVE_PRIMITIVE_INVALID;
    uint32_t xy[3]={sxy[0],sxy[1],sxy[2]},rgb[3]={colors[0],colors[1],colors[2]};
    uint32_t uv[3]={0};
    if(uv_end)for(uint32_t i=0;i<3;i++) {
        int error=gool_domain_read32(d,uv_end-12u+i*4u,&uv[i]);if(error)return error;
    }
    for(uint32_t i=0;i<3;i++) {
        int error=gool_domain_write32(d,primitive+8u+i*(uv_end?12u:8u),xy[i]);if(error)return error;
    }
    if(uv_end)for(uint32_t i=0;i<3;i++) {
        int error=gool_domain_write32(d,primitive+12u+i*12u,uv[i]);if(error)return error;
    }
    uint32_t opcode=uv_end?((uv[1]&0x00600000u)==0x00600000u?0x34000000u:0x36000000u):0x30000000u;
    int error=gool_domain_write32(d,primitive+4u,opcode|rgb[0]);if(error)return error;
    for(uint32_t i=1;i<3;i++) {
        error=gool_domain_write32(d,primitive+(uv_end?16u:12u)+(i-1u)*(uv_end?12u:8u),rgb[i]);
        if(error)return error;
    }
    *output=(gool_native_primitive_size){bytes,uv_end?9u:6u};return 0;
}
int gool_native_primitive_rgb1(gool_domain *d,uint32_t primitive,uint32_t end,
    const uint32_t sxy[3],const uint32_t colors[3],uint32_t uv_end,gool_native_primitive_size *output) {
    if(!d||!sxy||!colors||!output||!extent(primitive,end,40u)||uv_end<12u||(uv_end&3u))
        return GOOL_NATIVE_PRIMITIVE_INVALID;
    uint32_t xy[3]={sxy[0],sxy[1],sxy[2]},rgb[3]={colors[0],colors[1],colors[2]},uv[3];
    for(uint32_t i=0;i<3;i++) {
        uint32_t color=i?rgb[i]:0x36000000u|(rgb[0]&0xffffffu);
        int error=gool_domain_write32(d,primitive+4u+i*12u,color);if(error)return error;
    }
    for(uint32_t i=0;i<3;i++) {
        int error=gool_domain_read32(d,uv_end-12u+i*4u,&uv[i]);if(error)return error;
    }
    for(uint32_t i=0;i<3;i++) {
        int error=gool_domain_write32(d,primitive+8u+i*12u,xy[i]);if(error)return error;
    }
    for(uint32_t i=0;i<3;i++) {
        int error=gool_domain_write32(d,primitive+12u+i*12u,uv[i]);if(error)return error;
    }
    *output=(gool_native_primitive_size){40u,9u};return 0;
}
int gool_native_primitive_constant(gool_domain *d,uint32_t primitive,uint32_t end,
    const uint32_t sxy[3],gool_native_primitive_size *output) {
    if(!d||!sxy||!output||!extent(primitive,end,20u))return GOOL_NATIVE_PRIMITIVE_INVALID;
    uint32_t xy[3]={sxy[0],sxy[1],sxy[2]};
    int error=gool_domain_write32(d,primitive+4u,0x20204088u);if(error)return error;
    for(uint32_t i=0;i<3;i++) {
        error=gool_domain_write32(d,primitive+8u+i*4u,xy[i]);if(error)return error;
    }
    *output=(gool_native_primitive_size){20u,4u};return 0;
}
int gool_native_primitive_link(gool_domain *d,uint32_t primitive,uint32_t end,
    uint32_t bucket,uint32_t table_begin,uint32_t table_end,gool_native_primitive_size size,uint32_t *next) {
    if(!d||!next||!size.packet_words||size.packet_words>255u||size.bytes!=(size.packet_words+1u)*4u||
       !extent(primitive,end,size.bytes)||!table_begin||((bucket|table_begin|table_end)&3u)||
       table_begin>table_end||bucket<table_begin||(uint64_t)bucket+0x24u>table_end)
        return GOOL_NATIVE_PRIMITIVE_INVALID;
    if((uint64_t)primitive<table_end&&(uint64_t)table_begin<(uint64_t)primitive+size.bytes)
        return GOOL_NATIVE_PRIMITIVE_INVALID;
    uint32_t tag;int error=gool_domain_read32(d,bucket+0x20u,&tag);if(error)return error;
    error=gool_domain_write32(d,primitive,tag|(size.packet_words<<24));if(error)return error;
    error=gool_domain_write32(d,bucket+0x20u,primitive&0xffffffu);if(error)return error;
    *next=primitive+size.bytes;return 0;
}
int gool_native_primitive_rgb2(gool_domain *d,uint32_t primitive,uint32_t end,
    uint32_t bucket,uint32_t table_begin,uint32_t table_end,const uint32_t sxy[3],
    const uint32_t colors[3],uint32_t uv_end,uint32_t *next) {
    if(!d||!sxy||!colors||!next||!extent(primitive,end,40u)||!table_begin||
       ((bucket|table_begin|table_end)&3u)||table_begin>table_end||bucket<table_begin||
       (uint64_t)bucket+0x24u>table_end||
       ((uint64_t)primitive<table_end&&(uint64_t)table_begin<(uint64_t)primitive+40u)||
       (uv_end&&((uv_end&3u)||uv_end<12u)))return GOOL_NATIVE_PRIMITIVE_INVALID;
    uint32_t xy[3]={sxy[0],sxy[1],sxy[2]},rgb[3]={colors[0],colors[1],colors[2]},uv[3]={0};
    int error;
    if(uv_end) {
        /* Native reads page/CLUT word first, then UV0/UV2. */
        error=gool_domain_read32(d,uv_end-8u,&uv[1]);if(error)return error;
        uv[1]=(uv[1]&~0x00600000u)|0x00200000u;
        error=gool_domain_read32(d,uv_end-12u,&uv[0]);if(error)return error;
        error=gool_domain_read32(d,uv_end-4u,&uv[2]);if(error)return error;
    }
    for(uint32_t i=0;i<3;i++) {
        error=gool_domain_write32(d,primitive+8u+i*(uv_end?12u:8u),xy[i]);if(error)return error;
    }
    if(uv_end) {
        for(uint32_t i=0;i<3;i++) {
            error=gool_domain_write32(d,primitive+12u+i*12u,uv[i]);if(error)return error;
        }
        error=gool_domain_write32(d,primitive+4u,0x36000000u|rgb[0]);if(error)return error;
        error=gool_domain_write32(d,primitive+16u,rgb[1]);if(error)return error;
        error=gool_domain_write32(d,primitive+28u,rgb[2]);if(error)return error;
    } else {
        error=gool_domain_write32(d,primitive+12u,rgb[1]);if(error)return error;
        error=gool_domain_write32(d,primitive+20u,rgb[2]);if(error)return error;
        error=gool_domain_write32(d,primitive+4u,0x32000000u|rgb[0]);if(error)return error;
    }
    uint32_t old;error=gool_domain_read32(d,bucket+0x20u,&old);if(error)return error;
    error=gool_domain_write32(d,primitive,old|(uv_end?0x09000000u:0x06000000u));if(error)return error;
    uint32_t head=primitive&0xffffffu;
    if(!uv_end) {
        uint32_t state=primitive+28u;
        error=gool_domain_write32(d,state+4u,0xe1000a20u);if(error)return error;
        error=gool_domain_write32(d,state+8u,0);if(error)return error;
        error=gool_domain_write32(d,state,head|0x02000000u);if(error)return error;
        head=state&0xffffffu;
    }
    error=gool_domain_write32(d,bucket+0x20u,head);if(error)return error;
    *next=primitive+40u;return 0;
}
int gool_native_primitive_normal(gool_domain *d,uint32_t primitive,uint32_t end,
    uint32_t mode,uint32_t command,int backface,uint32_t uv_end,const uint32_t sxy[3],
    uint32_t rgb,gool_native_normal_primitive *output) {
    if(!d||!sxy||!output||(mode!=3u&&mode!=4u)||(backface!=0&&backface!=1)||
       !extent(primitive,end,32u)||uv_end<12u||(uv_end&3u))return GOOL_NATIVE_PRIMITIVE_INVALID;
    uint32_t xy[3]={sxy[0],sxy[1],sxy[2]};int error;
    for(uint32_t i=0;i<3;i++) {
        error=gool_domain_write32(d,primitive+8u+i*8u,xy[i]);if(error)return error;
    }
    uint32_t page;error=gool_domain_read32(d,uv_end-8u,&page);if(error)return error;
    uint32_t page_or=0x00600000u,opcode=0x24000000u;
    if(mode==3u) {
        if(!(page&0x00600000u)&&!backface){page_or=0x00200000u;opcode=0x26000000u;}
    } else if(page&0x00600000u) {
        if((command&0x10000000u)&&backface) {
            *output=(gool_native_normal_primitive){.skipped=1};return 0;
        }
    } else {
        page_or=backface?0x00400000u:0x00200000u;opcode=0x26000000u;
    }
    error=gool_domain_write32(d,primitive+4u,opcode|rgb);if(error)return error;
    uint32_t uv0,uv2;
    error=gool_domain_read32(d,uv_end-12u,&uv0);if(error)return error;
    error=gool_domain_read32(d,uv_end-4u,&uv2);if(error)return error;
    error=gool_domain_write32(d,primitive+12u,uv0);if(error)return error;
    error=gool_domain_write32(d,primitive+20u,page|page_or);if(error)return error;
    error=gool_domain_write32(d,primitive+28u,uv2);if(error)return error;
    *output=(gool_native_normal_primitive){.size={32u,7u}};return 0;
}
int gool_native_primitive_rgb7(gool_domain *d,uint32_t primitive,uint32_t end,
    const uint32_t sxy[3],const uint32_t colors[3],uint32_t uv_end,uint16_t coefficient,
    gool_native_primitive_size *output) {
    uint32_t bytes=uv_end?64u:60u;
    if(!d||!sxy||!colors||!output||!coefficient||!extent(primitive,end,bytes)||
       (uv_end&&((uv_end&3u)||uv_end<12u)))return GOOL_NATIVE_PRIMITIVE_INVALID;
    uint32_t xy[3]={sxy[0],sxy[1],sxy[2]},rgb[3]={colors[0],colors[1],colors[2]};
    int error=gool_domain_write32(d,primitive+4u,0xe1000a40u);if(error)return error;
    error=gool_domain_write32(d,primitive+8u,0);if(error)return error;
    uint32_t gray=(4096u-coefficient)>>5,green=gray<<8;
    gray|=green;gray|=green<<8;
    error=gool_domain_write32(d,primitive+12u,0x22000000u|gray);if(error)return error;
    for(uint32_t i=0;i<3;i++) {
        error=gool_domain_write32(d,primitive+16u+i*4u,xy[i]);if(error)return error;
    }
    if(!uv_end) {
        error=gool_domain_write32(d,primitive+28u,0xe1000a20u);if(error)return error;
        error=gool_domain_write32(d,primitive+32u,0);if(error)return error;
        error=gool_domain_write32(d,primitive+36u,0x32000000u|rgb[0]);if(error)return error;
        error=gool_domain_write32(d,primitive+40u,xy[0]);if(error)return error;
        error=gool_domain_write32(d,primitive+44u,rgb[1]);if(error)return error;
        error=gool_domain_write32(d,primitive+48u,xy[1]);if(error)return error;
        error=gool_domain_write32(d,primitive+52u,rgb[2]);if(error)return error;
        error=gool_domain_write32(d,primitive+56u,xy[2]);if(error)return error;
    } else {
        uint32_t uv[3];
        error=gool_domain_read32(d,uv_end-8u,&uv[1]);if(error)return error;
        uv[1]=(uv[1]&~0x00600000u)|0x00200000u;
        error=gool_domain_read32(d,uv_end-12u,&uv[0]);if(error)return error;
        error=gool_domain_read32(d,uv_end-4u,&uv[2]);if(error)return error;
        for(uint32_t i=0;i<3;i++) {
            error=gool_domain_write32(d,primitive+32u+i*12u,xy[i]);if(error)return error;
        }
        for(uint32_t i=0;i<3;i++) {
            error=gool_domain_write32(d,primitive+36u+i*12u,uv[i]);if(error)return error;
        }
        error=gool_domain_write32(d,primitive+28u,0x36000000u|rgb[0]);if(error)return error;
        error=gool_domain_write32(d,primitive+40u,rgb[1]);if(error)return error;
        error=gool_domain_write32(d,primitive+52u,rgb[2]);if(error)return error;
    }
    *output=(gool_native_primitive_size){bytes,uv_end?15u:14u};return 0;
}
