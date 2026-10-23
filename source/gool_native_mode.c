#include "gool_native_mode.h"

static int validate(gool_native_registry *r,uint32_t node) {
    if(!r||!r->domain||!r->actors||!node||(node&3u)||
       (uint64_t)node+0x110u>UINT32_MAX)return GOOL_NATIVE_ACTOR_INVALID;
    for(uint32_t i=0;i<r->count;i++) {
        gool_native_actor *a=r->actors[i];
        if(a&&a->attached&&a->node==node&&(a->enabled||a->executing||a->error))
            return GOOL_NATIVE_ACTOR_BUSY;
    }
    return 0;
}
static int copy_color(gool_domain *domain,uint32_t source,uint32_t destination) {
    uint8_t bytes[6];
    for(uint32_t i=0;i<6;i++) {
        int error=gool_domain_read8(domain,source+i,&bytes[i]);if(error)return error;
    }
    for(uint32_t i=0;i<6;i++) {
        int error=gool_domain_write8(domain,destination+i,bytes[i]);if(error)return error;
    }
    return 0;
}
int gool_native_light_compose(gool_native_registry *r,uint32_t node) {
    int error=validate(r,node);if(error)return error;
    gool_domain *domain=r->domain;uint32_t model;uint8_t type;uint16_t value;
    error=gool_native_registry_read(r,node+0xe8,&model);if(error)return error;
    if(!model)return 0;
    error=gool_domain_read8(domain,model,&type);if(error)return error;
    if(type==4)return 0;
    error=gool_domain_read16(domain,node+0x22,&value);if(error)return error;
    error=gool_domain_write16(domain,node+0x26,value);if(error)return error;
    error=copy_color(domain,node+0x2e,node+0x3a);if(error)return error;
    error=gool_domain_read16(domain,node+0x20,&value);if(error)return error;
    if(value) {
        error=gool_domain_write16(domain,node+0x26,value);if(error)return error;
        error=copy_color(domain,node+0x28,node+0x3a);if(error)return error;
    }
    error=gool_domain_read16(domain,node+0x24,&value);if(error)return error;
    if(value) {
        error=gool_domain_write16(domain,node+0x26,value);if(error)return error;
        error=copy_color(domain,node+0x34,node+0x3a);if(error)return error;
    }
    return 0;
}
int gool_native_mode_update(gool_native_registry *r,uint32_t node) {
    int error=validate(r,node);if(error)return error;
    gool_domain *domain=r->domain;uint32_t mode,tick,started,flags;int shade=0;
    error=gool_native_registry_read(r,node+0x108,&mode);if(error)return error;
    switch(mode) {
        case 1:break;
        case 2:shade=1;break;
        case 3:case 4:case 5:case 6:case 7: {
            error=gool_domain_read32(domain,0x80068eecu,&tick);if(error)return error;
            error=gool_native_registry_read(r,node+0x10c,&started);if(error)return error;
            uint32_t elapsed=tick-started;
            if(mode==3||mode==4) {
                uint32_t duration=mode==3?0x1c3u:0x3du;
                shade=mode==3;
                if(elapsed>=duration) {
                    error=gool_native_registry_write(r,node+0x108,0);if(error)return error;
                }
                /* No callback occurs between the native mode3 tests at1CB4C
                 * and1CBE0. With stable single-thread domain storage, this path
                 * cannot turn into the intervening mode4 notification branch. */
            } else if(mode==5) {
                int clear=elapsed>=0x259u;
                if(!clear) {
                    error=gool_native_registry_read(r,node+0x100,&flags);if(error)return error;
                    clear=(flags&0x02000000u)!=0;
                }
                if(clear){error=gool_native_registry_write(r,node+0x108,0);if(error)return error;}
                shade=1;
            } else {
                int clear=elapsed>=0x10u;
                if(mode==7&&!clear) {
                    error=gool_native_registry_read(r,node+0xa8,&flags);if(error)return error;
                    clear=(flags&1u)!=0;
                }
                if(clear) {
                    error=gool_native_registry_read(r,node+0xac,&flags);if(error)return error;
                    error=gool_native_registry_write(r,node+0x108,0);if(error)return error;
                    error=gool_native_registry_write(r,node+0xac,flags|0x80u);if(error)return error;
                }
            }
            break;
        }
        default:
            error=gool_domain_write16(domain,node+0x24,0);if(error)return error;
            break;
    }
    if(shade) {
        uint32_t counter;
        error=gool_domain_read32(domain,0x80069034u,&counter);if(error)return error;
        for(uint32_t offset=0x34;offset<=0x38;offset+=2) {
            error=gool_domain_write16(domain,node+offset,0x3c);if(error)return error;
        }
        /* Native signed remainder by4: negative counters keep negative phase,
         * and SH retains only the low16 bits after the wrapping left shift. */
        uint32_t remainder=counter&3u;
        if((counter&0x80000000u)&&remainder)remainder-=4u;
        error=gool_domain_write16(domain,node+0x24,(uint16_t)(remainder<<10));if(error)return error;
    }
    return gool_native_light_compose(r,node);
}
