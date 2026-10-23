#include "gool_native_call.h"
int gool_native_call_prepare(gool_native_actor *a,uint32_t script,uint32_t argc,uint32_t arguments) {
    if(!a||!a->attached||a->enabled||a->executing||a->error||!a->call_references||
       argc>15u||(argc&&(!arguments||(arguments&3u))))return GOOL_NATIVE_CALL_INVALID;
    gool_domain *d=a->registry->domain;uint32_t value,sp,behavior,code;
    for(uint32_t i=0;i<argc;i++) {
        int e=gool_domain_read32(d,arguments+i*4u,&value);if(e)return e;
        e=gool_domain_read32(d,a->node+0xbcu,&sp);if(e)return e;
        if((sp&3u)||sp<a->stack_address||(uint64_t)sp+4u>(uint64_t)a->stack_address+a->stack_words*4u)
            return GOOL_VM_STACK;
        e=gool_domain_write32(d,sp,value);if(e)return e;
        e=gool_native_registry_write(a->registry,a->node+0xbcu,sp+4u);if(e)return e;
    }
    int e=gool_native_actor_enter_frame(a,argc,0xffffu);if(e)return e;
    e=gool_domain_read32(d,a->node+0xcu,&behavior);if(e)return e;
    e=gool_domain_read32(d,behavior+0x14u,&code);if(e)return e;
    return gool_native_registry_write(a->registry,a->node+0xc0u,code+(script&0x3fffu)*4u);
}
static int fail(gool_native_call *o,int error){o->status=error;return error;}
static int timeline(gool_native_call *o) {
    if(!o->factory->open||!o->factory->engine.tick||*o->factory->engine.tick!=o->tick)
        return GOOL_NATIVE_CALL_INVALID;
    uint32_t raw;int error=gool_domain_read32(o->factory->registry->domain,0x80068eecu,&raw);
    if(error)return error;
    return raw==(uint32_t)o->tick?0:GOOL_NATIVE_CALL_INVALID;
}
static int release(gool_native_call *o) {
    if(!o->actor_retained)return 0;
    if(!o->actor||!o->actor->attached||o->actor->enabled||o->actor->executing||
       !o->actor->call_references)return GOOL_NATIVE_ACTOR_BUSY;
    o->actor->call_references--;o->actor_retained=0;return 0;
}
int gool_native_call_bind(gool_native_call *o,gool_native_factory *factory,
    gool_native_route_frame *frames,uint32_t capacity,uint32_t base) {
    if(!o||o->bound||!factory||!factory->open)return GOOL_NATIVE_CALL_INVALID;
    *o=(gool_native_call){.factory=factory};
    int error=gool_native_router_bind(&o->router,factory,frames,capacity,base);
    if(error){*o=(gool_native_call){0};return error;}
    o->bound=1;return 0;
}
int gool_native_call_begin(gool_native_call *o,uint32_t node,uint32_t script,
                           uint32_t argc,uint32_t arguments) {
    if(!o||!o->bound||o->active||o->status<0||o->actor_retained||o->dispatch.active||o->router.active||
       o->router.depth||o->router.overflow||!o->factory->engine.tick||!o->factory->open||
       argc>15u||(argc&&(!arguments||(arguments&3u)||(uint64_t)arguments+argc*4u>UINT32_MAX)))
        return GOOL_NATIVE_CALL_INVALID;
    gool_native_actor *actor;int error=gool_native_factory_actor(o->factory,node,&actor);if(error)return error;
    if(!actor||!actor->attached||actor->enabled||actor->executing||actor->error||
       !actor->vm||actor->vm->status||actor->vm->send_pending||actor->call_references==UINT32_MAX)
        return GOOL_NATIVE_ACTOR_BUSY;
    uint64_t tick=*o->factory->engine.tick;uint32_t raw;
    error=gool_domain_read32(o->factory->registry->domain,0x80068eecu,&raw);if(error)return error;
    if(raw!=(uint32_t)tick)return GOOL_NATIVE_CALL_INVALID;
    o->actor=actor;o->node=node;o->script=script;o->argc=argc;o->arguments=arguments;
    o->index=0;o->phase=0;o->tick=tick;o->active=1;o->status=GOOL_NATIVE_CALL_PENDING;
    actor->call_references++;o->actor_retained=1;return o->status;
}
int gool_native_call_step(gool_native_call *o,uint32_t budget) {
    if(!o||!o->bound)return GOOL_NATIVE_CALL_INVALID;
    if(!o->active||o->status<0||!budget)return o->status;
    gool_native_registry *registry=o->factory->registry;gool_domain *domain=registry->domain;
    while(budget--) {
        int error=timeline(o);if(error)return fail(o,error);
        switch(o->phase) {
        case 0:
            if(o->index<o->argc) {
                uint32_t value,sp;
                error=gool_domain_read32(domain,o->arguments+o->index*4u,&value);if(error)return fail(o,error);
                error=gool_domain_read32(domain,o->node+0xbc,&sp);if(error)return fail(o,error);
                if(sp<o->actor->stack_address||(sp&3u)||
                   (uint64_t)sp+4u>(uint64_t)o->actor->stack_address+o->actor->stack_words*4u)
                    return fail(o,GOOL_VM_STACK);
                error=gool_domain_write32(domain,sp,value);if(error)return fail(o,error);
                error=gool_native_registry_write(registry,o->node+0xbc,sp+4u);if(error)return fail(o,error);
                o->index++;break;
            }
            error=gool_native_actor_enter_frame(o->actor,o->argc,0xffffu);if(error)return fail(o,error);
            o->phase=1;break;
        case 1: {
            uint32_t behavior,code;
            error=gool_domain_read32(domain,o->node+0xc,&behavior);if(error)return fail(o,error);
            error=gool_domain_read32(domain,behavior+0x14,&code);if(error)return fail(o,error);
            uint64_t pc=(uint64_t)code+(o->script&0x3fffu)*4u;
            if(pc>UINT32_MAX)return fail(o,GOOL_NATIVE_CALL_INVALID);
            error=gool_native_registry_write(registry,o->node+0xc0,(uint32_t)pc);if(error)return fail(o,error);
            o->phase=2;break;
        }
        case 2:
            error=gool_native_dispatch_begin(&o->dispatch,o->actor,3);
            if(error!=GOOL_NATIVE_DISPATCH_BUDGET)return fail(o,error<0?error:GOOL_NATIVE_CALL_INVALID);
            o->phase=3;break;
        case 3:
            error=gool_native_router_begin(&o->router,&o->dispatch);
            if(error!=GOOL_NATIVE_ROUTER_BUDGET)return fail(o,error<0?error:GOOL_NATIVE_CALL_INVALID);
            o->phase=4;break;
        case 4:
            error=gool_native_router_step(&o->router,1);if(error<0)return fail(o,error);
            if(error)break;
            o->native_result=o->router.native_result;o->phase=5;break;
        case 5:
            error=timeline(o);if(error)return fail(o,error);
            error=release(o);if(error)return fail(o,error);
            o->active=0;o->status=0;return 0;
        default:return fail(o,GOOL_NATIVE_CALL_INVALID);
        }
    }
    return GOOL_NATIVE_CALL_PENDING;
}
int gool_native_call_close(gool_native_call *o) {
    if(!o)return GOOL_NATIVE_CALL_INVALID;
    if(!o->bound)return 0;
    int error=gool_native_router_close(&o->router);if(error)return error;
    if(o->dispatch.active) {
        error=gool_native_dispatch_abort(&o->dispatch,GOOL_NATIVE_CALL_INVALID);
        if(o->dispatch.active)return error;
    }
    error=release(o);if(error)return error;
    *o=(gool_native_call){0};return 0;
}
