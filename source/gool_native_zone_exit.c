#include "gool_native_zone_exit.h"

static int player_stable(const gool_native_zone_exit *o) {
    gool_native_actor *a=o->player;
    return a&&a->attached&&a->registry==o->registry&&a->node==o->player_node&&
        o->player_index<o->registry->count&&o->registry->actors[o->player_index]==a&&
        !a->enabled&&!a->executing&&a->call_references;
}
static int memory_read(void *context,uint32_t address,uint32_t *value) {
    gool_native_zone_exit *o=context;
    return gool_native_registry_read(o->registry,address,value);
}
static int memory_write(void *context,uint32_t address,uint32_t value) {
    gool_native_zone_exit *o=context;
    return gool_native_registry_write(o->registry,address,value);
}
static int retain(void *context,uint32_t address,uint64_t *token) {
    gool_native_zone_exit *o=context;
    if(address==o->entry&&!o->zone_retained) {
        int error=zdat_resources_acquire_address(o->bank,address,&o->zone_lease);if(error)return error;
        o->zone_retained=1;*token=1;return 0;
    }
    if(o->player_retained)return GOOL_NATIVE_ZONE_EXIT_INVALID;
    for(uint32_t i=0;i<o->registry->count;i++) {
        gool_native_actor *a=o->registry->actors[i];
        if(!a||!a->attached||a->node!=address)continue;
        if(a->registry!=o->registry||a->executing||a->enabled||a->error||
           a->call_references==UINT32_MAX)return GOOL_NATIVE_ACTOR_BUSY;
        a->call_references++;o->player=a;o->player_index=i;o->player_node=address;
        o->player_retained=1;*token=2;return 0;
    }
    return GOOL_NATIVE_ZONE_EXIT_INVALID;
}
static int release(void *context,uint64_t token) {
    gool_native_zone_exit *o=context;
    if(token==2&&o->player_retained) {
        if(!player_stable(o))return GOOL_NATIVE_ACTOR_BUSY;
        o->player->call_references--;o->player_retained=0;return 0;
    }
    if(token==1&&o->zone_retained) {
        int error=zdat_resources_release(o->bank,o->zone_lease);if(error)return error;
        o->zone_retained=0;return 0;
    }
    return GOOL_NATIVE_ZONE_EXIT_INVALID;
}
static int retire_begin(void *context,uint32_t entry) {
    gool_native_zone_exit *o=context;
    return gool_zone_retire_begin(&o->retirement,entry,&o->retire_services);
}
static int retire_step(void *context,uint32_t budget) {
    gool_native_zone_exit *o=context;
    return gool_zone_retire_step(&o->retirement,budget);
}
static int retire_cancel(void *context) {
    gool_native_zone_exit *o=context;
    int error=gool_zone_retire_close(&o->retirement);
    return error>0?GOOL_NATIVE_ACTOR_BUSY:error;
}
int gool_native_zone_exit_begin(gool_native_zone_exit *o,gool_native_registry *r,
    uint32_t entry,const gool_zone_retire_services *s) {
    if(!o||o->opened||o->zone_retained||o->player_retained||!r||!r->domain||!r->zones||
       r->zones->domain!=r->domain||!r->zones->open||r->zones->closing||!r->actors||
       !entry||(entry&3u)||!s||s->domain!=r->domain||!s->max_nodes||
       !s->signal||!s->retire||!s->cancel)return GOOL_NATIVE_ZONE_EXIT_INVALID;
    gool_zone_retire_services captured=*s;
    *o=(gool_native_zone_exit){.registry=r,.bank=r->zones,.domain=r->domain,
        .entry=entry,.retire_services=captured,.opened=1};
    zone_exit_services services={.memory={o,memory_read,memory_write},
        .retain=retain,.release=release,.retire_begin=retire_begin,
        .retire_step=retire_step,.retire_cancel=retire_cancel};
    int error=zone_exit_begin(&o->exit,entry,&services);o->status=error;
    if(error<0&&!o->zone_retained)*o=(gool_native_zone_exit){0};
    return error;
}
int gool_native_zone_exit_step(gool_native_zone_exit *o,uint32_t budget) {
    if(!o||!o->opened)return GOOL_NATIVE_ZONE_EXIT_INVALID;
    if(o->status!=ZONE_TRANSITION_BUDGET)return o->status;
    if(o->registry->domain!=o->domain||o->registry->zones!=o->bank||
       o->bank->domain!=o->domain||!o->bank->open||o->bank->closing||
       (o->player_retained&&!player_stable(o)))
        return o->status=GOOL_NATIVE_ZONE_EXIT_INVALID;
    return o->status=zone_exit_step(&o->exit,budget);
}
int gool_native_zone_exit_close(gool_native_zone_exit *o) {
    if(!o)return GOOL_NATIVE_ZONE_EXIT_INVALID;
    if(!o->opened)return 0;
    int error=zone_exit_close(&o->exit);if(error)return error;
    *o=(gool_native_zone_exit){0};return 0;
}
