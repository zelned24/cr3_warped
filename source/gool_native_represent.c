#include "gool_native_represent.h"

/* SCUS_942.44 tables8005C1B0/8005C1F0. Values are native program identifiers,
 * not executable host addresses or substitutes for missing implementations. */
static const uint32_t matrix_functions[16]={
    0,0,0x8003e754u,0x8003f044u,0x8003d470u,0x8003d470u,0x8003d470u,0x8003d470u,
    0x8003f044u,0x8003f044u,0x8003f044u,0x8003d470u,0x8003d470u,0x80040c20u,0x80040c20u,0x8003f044u
};
static const uint32_t render_functions[16]={
    0,0,0x8003d030u,0x8003d054u,0x8003d078u,0x8003d09cu,0x8003e67cu,0x8003d0c0u,
    0x8003cfa8u,0x8003cffcu,0x8003ce70u,0x8003d3acu,0x8003ebe0u,0x8003e67cu,0x8003d3acu,0x8003ec34u
};
static int32_t signed_word(uint32_t v){return v<=INT32_MAX?(int32_t)v:-1-(int32_t)(UINT32_MAX-v);}
static uint32_t shift_right(uint32_t v,unsigned bits) {
    return (v>>bits)|((v&0x80000000u)?(~0u<<(32u-bits)):0u);
}
static uint32_t native_scale_word(uint32_t raw) {
    uint32_t absolute=(raw&0x80000000u)?0u-raw:raw;
    int64_t product=(int64_t)signed_word(absolute)*INT64_C(0x26705ebf);
    uint32_t high=(uint32_t)((uint64_t)product>>32);
    return shift_right(high,12)-((absolute&0x80000000u)?UINT32_MAX:0u);
}
static int read_word(gool_native_represent *o,uint32_t address,uint32_t *value) {
    return gool_native_registry_read(o->registry,address,value);
}
static int prepare(gool_native_represent *o) {
    uint32_t model,format;uint8_t type,byte;int error;
    error=read_word(o,o->node+0xe8,&model);if(error)return error;
    if(!model)return 0;
    error=gool_domain_read8(o->registry->domain,model,&type);if(error)return error;
    if(type!=1&&type!=2&&type!=5)return 0;
    if(type!=1) {
        uint32_t scale;error=read_word(o,o->node+0x78,&scale);if(error)return error;
        o->output[1]=native_scale_word(scale);*o->written|=2u;
    }
    error=gool_domain_read8(o->registry->domain,o->node+0x120,&byte);if(error)return error;
    format=byte;if(format>=16)return GOOL_NATIVE_REPRESENT_INVALID;
    o->output[0]=render_functions[format];*o->written|=1u;
    if(!o->output[0]) {
        error=read_word(o,type==1?0x80067a34u:0x80067a30u,&format);if(error)return error;
        if(format>=16)return GOOL_NATIVE_REPRESENT_INVALID;
        o->output[0]=render_functions[format];
    }
    o->function=matrix_functions[format];
    if(!o->function||!o->output[0])return GOOL_NATIVE_REPRESENT_INVALID;
    error=read_word(o,o->node+8,&o->destination);if(error)return error;
    o->angles=o->node+0x6c;o->scale=o->node+0x78;o->extra=0;
    if(type==1) {
        uint32_t entry;
        if(o->destination>UINT32_MAX-0x28u)return GOOL_NATIVE_REPRESENT_INVALID;
        error=read_word(o,o->destination+0x28,&entry);if(error)return error;
        if(entry>UINT32_MAX-0x10u)return GOOL_NATIVE_REPRESENT_INVALID;
        error=read_word(o,entry+0x10,&o->extra);if(error)return error;
    }
    return gool_native_matrix_build(o->registry->domain,o->function,o->destination,o->angles,o->scale,o->extra);
}
int gool_native_represent_bind(gool_native_represent *o,gool_native_registry *registry) {
    if(!o||o->bound||!registry||!registry->domain||!registry->actors)return GOOL_NATIVE_REPRESENT_INVALID;
    *o=(gool_native_represent){.registry=registry,.bound=1};return 0;
}
int gool_native_represent_run(gool_native_represent *o,uint32_t node,uint32_t output[2],uint32_t *written) {
    if(!o||!o->bound||!node||(node&3u)||(uint64_t)node+0x121u>UINT32_MAX||
       !output||!written||(*written&~3u))return GOOL_NATIVE_REPRESENT_INVALID;
    if(o->active) {
        if(o->node!=node||o->output!=output||o->written!=written)return GOOL_NATIVE_REPRESENT_INVALID;
        if(o->status<0)return o->status;
    } else {
        o->node=node;o->output=output;o->written=written;o->function=0;
        o->active=1;o->status=0;
    }
    int error=prepare(o);if(error){o->status=error;return error;}
    o->active=0;o->status=0;return 0;
}
int gool_native_represent_close(gool_native_represent *o) {
    if(!o)return GOOL_NATIVE_REPRESENT_INVALID;
    if(!o->bound)return 0;
    *o=(gool_native_represent){0};return 0;
}
