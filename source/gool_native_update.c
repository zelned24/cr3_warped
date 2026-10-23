#include "gool_native_update.h"

static int fail(gool_native_update *o,int error) {o->status=error;return error;}
static int timeline(gool_native_update *o) {
    if(!o->factory->open||!o->factory->engine.tick||*o->factory->engine.tick!=o->tick)
        return GOOL_NATIVE_UPDATE_TIMELINE;
    uint32_t tick;int error=gool_domain_read32(o->factory->registry->domain,0x80068eecu,&tick);
    if(error)return error;
    return tick==(uint32_t)o->tick?0:GOOL_NATIVE_UPDATE_TIMELINE;
}
static int release_actor(gool_native_update *o) {
    if(!o->actor_retained)return 0;
    if(!o->actor||!o->actor->attached||o->actor->enabled||o->actor->executing||
       !o->actor->call_references)return GOOL_NATIVE_ACTOR_BUSY;
    o->actor->call_references--;o->actor_retained=0;return 0;
}
static int done(gool_native_update *o) {
    int error=timeline(o);if(error)return fail(o,error);
    error=release_actor(o);if(error)return fail(o,error);
    o->active=0;o->status=0;return 0;
}
static int start(gool_native_update *o,uint32_t mode) {
    if(!o->router.bound) {
        int error=gool_native_router_bind(&o->router,o->factory,o->frames,o->capacity,o->arguments_base);
        if(error)return error;
    }
    int error=gool_native_dispatch_begin(&o->dispatch,o->actor,mode);
    if(error!=GOOL_NATIVE_DISPATCH_BUDGET)return error<0?error:GOOL_NATIVE_UPDATE_INVALID;
    error=gool_native_router_begin(&o->router,&o->dispatch);
    if(error!=GOOL_NATIVE_ROUTER_BUDGET)return error<0?error:GOOL_NATIVE_UPDATE_INVALID;
    return 0;
}
int gool_native_update_bind(gool_native_update *o,gool_native_factory *factory,
    gool_native_route_frame *frames,uint32_t capacity,uint32_t base) {
    if(!o||o->bound||!factory||!factory->open)return GOOL_NATIVE_UPDATE_INVALID;
    /* Validate in the final stable owner: router stores these exact pointers. */
    *o=(gool_native_update){.factory=factory,.frames=frames,.capacity=capacity,.arguments_base=base};
    int error=gool_native_router_bind(&o->router,factory,frames,capacity,base);
    if(error){*o=(gool_native_update){0};return error;}
    o->bound=1;return 0;
}
static int begin_update(gool_native_update *o,gool_native_actor *a,int taking) {
    if(!o||!o->bound||o->active||o->actor_retained||o->dispatch.active||o->router.active||
       o->router.depth||o->router.overflow||!o->factory->open||!a||!a->attached||a->enabled||
       a->executing||a->error||!a->vm||a->vm->status||a->vm->send_pending||
       a->registry!=o->factory->registry||a->index>=o->factory->count||
       a->call_references!=(taking?1u:0u)||!o->factory->engine.tick||
       a->tick_source!=o->factory->engine.tick)
        return GOOL_NATIVE_UPDATE_INVALID;
    gool_native_factory_slot *slot=&o->factory->slots[a->index];
    if(&slot->actor!=a||slot->owner!=o->factory||slot->node!=a->node||slot->request||
       o->factory->registry->actors[a->index]!=a)return GOOL_NATIVE_UPDATE_INVALID;
    uint64_t tick=*o->factory->engine.tick;
    uint32_t raw;int error=gool_domain_read32(a->registry->domain,0x80068eecu,&raw);if(error)return error;
    if(raw!=(uint32_t)tick)return GOOL_NATIVE_UPDATE_TIMELINE;
    o->actor=a;o->tick=tick;o->phase=0;o->pre_ran=o->main_ran=o->waiting=0;
    o->has_result=o->early_return=0;o->native_result=0;
    if(!taking)a->call_references++;
    o->actor_retained=1;o->active=1;o->status=GOOL_NATIVE_UPDATE_BUDGET;
    return o->status;
}
int gool_native_update_begin(gool_native_update *o,gool_native_actor *a) {
    return begin_update(o,a,0);
}
int gool_native_update_take(gool_native_update *o,gool_native_actor *a) {
    return begin_update(o,a,1);
}
int gool_native_update_step(gool_native_update *o,uint32_t budget) {
    if(!o||!o->bound)return GOOL_NATIVE_UPDATE_INVALID;
    if(!o->active||o->status<0||!budget)return o->status;
    int error=timeline(o);if(error)return fail(o,error);
    while(budget--) {
        error=timeline(o);if(error)return fail(o,error);
        if(o->phase==0) {
            uint32_t pre;
            error=gool_domain_read32(o->factory->registry->domain,o->actor->node+0xc8u,&pre);
            if(error)return fail(o,error);
            if(!pre){o->phase=2;continue;}
            error=gool_native_actor_enter_frame(o->actor,0,0xffff);if(error)return fail(o,error);
            /* Native rereads C8 after frame formation, then writes the entry PC. */
            error=gool_domain_read32(o->factory->registry->domain,o->actor->node+0xc8u,&pre);
            if(error)return fail(o,error);
            error=gool_native_registry_write(o->factory->registry,o->actor->node+0xc0u,pre);
            if(error)return fail(o,error);
            error=start(o,3);if(error)return fail(o,error);
            o->pre_ran=1;o->phase=1;continue;
        }
        if(o->phase==1||o->phase==3) {
            error=gool_native_router_step(&o->router,1);
            if(error<0)return fail(o,error);
            int clock_error=timeline(o);if(clock_error)return fail(o,clock_error);
            if(error)continue;
            o->native_result=o->router.native_result;o->has_result=1;
            error=gool_native_router_close(&o->router);if(error)return fail(o,error);
            o->dispatch=(gool_native_dispatch){0};
            if(o->native_result>=-254){o->early_return=1;return done(o);}
            if(o->phase==3)return done(o);
            o->phase=2;continue;
        }
        if(o->phase==2) {
            gool_native_actor *a=o->actor;gool_domain *domain=o->factory->registry->domain;
            uint32_t sp,token,tick;
            error=gool_domain_read32(domain,a->node+0xbcu,&sp);if(error)return fail(o,error);
            if(sp<a->stack_address+4u||(uint64_t)sp>(uint64_t)a->stack_address+a->stack_words*4u||(sp&3u))
                return fail(o,GOOL_VM_STACK);
            error=gool_domain_read32(domain,sp-4u,&token);if(error)return fail(o,error);
            error=gool_domain_read32(domain,0x80068eecu,&tick);if(error)return fail(o,error);
            if(tick!=(uint32_t)o->tick)return fail(o,GOOL_NATIVE_UPDATE_TIMELINE);
            uint32_t duration=token>>24,elapsed=tick-(token&0x00ffffffu);
            if(elapsed<duration&&duration>=2u){o->waiting=1;return done(o);}
            error=gool_native_registry_write(o->factory->registry,a->node+0xbcu,sp-4u);
            if(error)return fail(o,error);
            a->vm->frame_yield=0;a->vm->frame_token_index=0;a->vm->next_update=0;
            /* Advance before starting main: a failed start must never repop. */
            o->phase=4;
            error=start(o,4);if(error)return fail(o,error);
            o->main_ran=1;o->phase=3;continue;
        }
        return fail(o,GOOL_NATIVE_UPDATE_INVALID);
    }
    return GOOL_NATIVE_UPDATE_BUDGET;
}
int gool_native_update_close(gool_native_update *o) {
    if(!o)return GOOL_NATIVE_UPDATE_INVALID;
    if(!o->bound)return 0;
    int error=gool_native_router_close(&o->router);if(error)return error;
    if(o->dispatch.active) {
        error=gool_native_dispatch_abort(&o->dispatch,GOOL_NATIVE_UPDATE_INVALID);
        if(o->dispatch.active)return error;
    }
    error=release_actor(o);if(error)return error;
    *o=(gool_native_update){0};return 0;
}
