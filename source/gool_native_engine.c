#include "gool_native_engine.h"
#include "gool_native_dispatch_table.h"

static int stores(gool_domain *d,const uint32_t (*pairs)[2],uint32_t count) {
    if(!d)return GOOL_DOMAIN_INVALID;
    for(uint32_t i=0;i<count;i++) {
        int error=gool_domain_write32(d,pairs[i][0],pairs[i][1]);if(error)return error;
    }
    return 0;
}
int gool_native_engine_defaults(gool_domain *d) {
    static const uint32_t pairs[][2]={
        {0x80068f80,0x100},{0x80068f84,0xc0},{0x80068f74,0x400},
        {0x80069158,0x80},{0x8006915c,0x80},{0x80068f78,0},
        {0x80068f7c,0},{0x80068f4c,0},{0x80069160,0},
        {0x80069164,0},{0x80069168,0xff},{0x8006916c,0xff}
    };
    return stores(d,pairs,sizeof(pairs)/sizeof(pairs[0]));
}
int gool_native_engine_random_init(gool_domain *d) {
    static const uint32_t pairs[][2]={
        {0x80060a2c,0x3039},{0x80060a30,0xbaec2},{0x80068f0c,0x100},
        {0x80069178,0x1f},{0x800690a8,0},{0x80068f88,0},
        {0x80068f78,0},{0x8006913c,UINT32_MAX},{0x80069140,0}
    };
    return stores(d,pairs,sizeof(pairs)/sizeof(pairs[0]));
}
int gool_native_engine_level_begin(gool_domain *d) {
    static const uint32_t pairs[][2]={
        {0x8006909c,0},{0x80060ab0,0},{0x80060ab4,0},
        {0x80068f08,0x0c01ffff},{0x80068f1c,0x0c01ffff}
    };
    return stores(d,pairs,sizeof(pairs)/sizeof(pairs[0]));
}
int gool_native_engine_frame_flags(gool_domain *d) {
    uint32_t flags;int error=gool_domain_read32(d,0x80068f08u,&flags);
    if(error)return error;
    return gool_domain_write32(d,0x80068f1cu,flags);
}
int gool_native_engine_camera_reset(gool_domain *d) {
    static const uint32_t pairs[][2]={
        {0x80065c8c,0},{0x80065c88,0},{0x80065c84,0},
        {0x80065d54,0x120},
        /*18DA8 initializes the complete camera transform before188EC's tail.*/
        {0x80065cfc,0},{0x80065d00,0},{0x80065d04,0},
        {0x80065cf0,0},{0x80065cf4,0},{0x80065cf8,0},
        {0x80065d08,0x1000},{0x80065d0c,0x1000},{0x80065d10,0x1000},
        {0x80065cf0,0},{0x80065cf8,0x1f400},{0x80065cf4,0},
        {0x80065cfc,0},{0x80065d00,0},{0x80065d04,0},
        {0x80065d38,0},{0x80068fa8,0},{0x80068fac,0}
    };
    return stores(d,pairs,sizeof(pairs)/sizeof(pairs[0]));
}
int gool_native_engine_new_game(gool_domain *d,uint32_t mode) {
    int error;
    if(mode) {
        error=gool_native_engine_defaults(d);if(error)return error;
        uint32_t saved;
        error=gool_domain_read32(d,0x80068f74u,&saved);if(error)return error;
        static const uint16_t zero[]={0x26c,0x210,0x74,0xf0,0x230,0xc4,0xc8,0xcc,0xd0,
            0x118,0x11c,0x294,0x298,0x31c,0x320,0x324,0x328,0x2b8,0x2e0,
            0x140,0x144,0x120,0x124,0x32c,0x330,0x334,0x338,0x1c8,
            0x88,0x15c,0x160,0x178,0x17c,0x2a8,0x244,0x248,0x24c,0xbc};
        error=gool_domain_write32(d,0x80068f48u,99);if(error)return error;
        error=gool_domain_write32(d,0x8006905cu,99);if(error)return error;
        for(unsigned i=0;i<sizeof(zero)/sizeof(zero[0]);i++) {
            error=gool_domain_write32(d,0x80068e98u+zero[i],0);if(error)return error;
        }
        error=gool_domain_write32(d,0x8006900cu,UINT32_MAX);if(error)return error;
        error=gool_domain_write32(d,0x80068f58u,saved);if(error)return error;
        error=gool_domain_write32(d,0x8006913cu,UINT32_MAX);if(error)return error;
        if(mode!=2u)for(unsigned i=0;i<0x200u;i+=4u) {
            error=gool_domain_write32(d,0x8006c2d8u+i,0);if(error)return error;
        }
    }
    for(unsigned i=0;i<0x200u;i+=4u) {
        error=gool_domain_write32(d,0x8006aee8u+i,0);if(error)return error;
    }
    return 0;
}
int gool_native_engine_map_dispatch(gool_domain *d,gool_domain_lease *lease) {
    return gool_domain_map_readonly(d,0x8005d120u,gool_native_dispatch_table,
        sizeof(gool_native_dispatch_table),lease);
}
int gool_native_engine_prepare(gool_domain *d) {
    if(!d)return GOOL_DOMAIN_INVALID;
    uint32_t timer,factor,phase;
    int error=gool_domain_read32(d,0x80061a7cu,&timer);if(error)return error;
    if(timer>UINT32_MAX-0x18u)return GOOL_DOMAIN_INVALID;
    error=gool_domain_read32(d,timer+0x18u,&factor);if(error)return error;
    error=gool_domain_read32(d,0x80069034u,&phase);if(error)return error;
    error=gool_domain_write32(d,0x1f800050u,phase);if(error)return error;
    if(factor>=0x67u)factor=0x66u;
    error=gool_domain_write32(d,0x1f800054u,factor);if(error)return error;
    error=gool_domain_write32(d,0x1f800058u,0x80068ef8u);if(error)return error;
    for(uint32_t i=0;i<0x148u;i+=2u) {
        uint16_t value;error=gool_domain_read16(d,0x8005d120u+i,&value);if(error)return error;
        error=gool_domain_write16(d,0x1f800060u+i,value);if(error)return error;
    }
    return gool_domain_write32(d,0x1f80005cu,0x1f800060u);
}
