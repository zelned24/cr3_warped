#include "gool_native_geometry_search.h"
#include "gool_native_geometry.h"
#include "gool_native_call.h"

static int32_t sign(uint32_t v){return v<=INT32_MAX?(int32_t)v:-1-(int32_t)(UINT32_MAX-v);}
static int fail(gool_native_geometry_search *o,int e){o->status=e;return e;}
static int rd(gool_native_geometry_search *o,uint32_t a,uint32_t *v) {
    return gool_native_registry_read(o->factory->registry,a,v);
}
static int release_candidate(gool_native_geometry_search *o) {
    if(!o->candidate_retained)return 0;
    gool_native_actor *a=o->candidate_actor;
    if(!a||!a->attached||a->enabled||a->executing||!a->call_references)return GOOL_NATIVE_ACTOR_BUSY;
    a->vm->send=o->previous_send;a->vm->send_pending=o->previous_send_pending;
    a->call_references--;o->candidate_retained=0;o->candidate_actor=0;return 0;
}
static int predicate(void *context,uint32_t node,int *matched) {
    gool_native_geometry_search *o=context;uint32_t behavior,header,category,delta[3],route_address;
    *matched=0;
    if(o->phase) {
        if(node!=o->candidate)return GOOL_NATIVE_GEOMETRY_INVALID;
        if(o->dispatch.active)return 2;
        uint32_t accepted;int e=rd(o,o->excluded+0xd8u,&accepted);if(e)return e;
        e=release_candidate(o);if(e)return e;
        if(accepted){o->best=node;o->distance=o->candidate_distance;}
        o->phase=0;return 0;
    }
    if(node==o->excluded)return 0;
    int e=rd(o,node+0xcu,&behavior);if(e)return e;
    e=rd(o,behavior+0x10u,&header);if(e)return e;
    e=rd(o,header+4u,&category);if(e)return e;
    if(!(o->mask&(1u<<((category>>8)&31u))))return 0;
    for(uint32_t i=0;i<3;i++) {
        uint32_t a,b;e=rd(o,node+0x60u+i*4u,&a);if(e)return e;
        e=rd(o,o->excluded+0x60u+i*4u,&b);if(e)return e;
        delta[i]=a-b;if(delta[i]&0x80000000u)delta[i]=0u-delta[i];
    }
    uint32_t largest=0;
    if(sign(delta[1])>sign(delta[0]))largest=1;
    if(sign(delta[2])>sign(delta[largest]))largest=2;
    uint32_t other=0;for(uint32_t i=0;i<3;i++)if(i!=largest)other+=delta[i];
    if(other&0x80000000u)other+=3u;
    other=(other>>2)|((other&0x80000000u)?0xc0000000u:0);
    uint32_t distance=delta[largest]+other;
    if(sign(distance)>=sign(o->distance))return 0;
    e=rd(o,behavior+0x1cu,&route_address);if(e)return e;
    uint16_t route;e=gool_domain_read16(o->factory->registry->domain,route_address+30u,&route);if(e)return e;
    if(route==255u)return 0;
    if(!(route&0x8000u)) {
        uint32_t states,mode,flags,motion;
        e=rd(o,behavior+0x20u,&states);if(e)return e;
        e=rd(o,node+0x108u,&mode);if(e)return e;
        e=rd(o,node+0xb0u,&flags);if(e)return e;
        e=rd(o,states+(uint32_t)route*16u,&motion);if(e)return e;
        if(mode-2u<3u)flags|=0x402u;
        if(flags&motion)return 0;
        o->best=node;o->distance=distance;return 0;
    }
    gool_native_actor *actor;e=gool_native_factory_actor(o->factory,node,&actor);if(e)return e;
    if(!actor||!actor->attached||actor->enabled||actor->executing||actor->error||actor->call_references==UINT32_MAX)
        return GOOL_NATIVE_ACTOR_BUSY;
    actor->call_references++;o->candidate_actor=actor;o->candidate_retained=1;
    o->previous_send=actor->vm->send;o->previous_send_pending=actor->vm->send_pending;
    actor->vm->send_pending=0;
    o->candidate=node;o->candidate_distance=distance;o->route=route;
    e=gool_native_registry_write(o->factory->registry,node+0x5cu,o->excluded);if(e)return e;
    e=gool_native_call_prepare(actor,route,1,o->argument_address);if(e)return e;
    e=gool_native_dispatch_begin(&o->dispatch,actor,3);
    if(e!=GOOL_NATIVE_DISPATCH_BUDGET)return e<0?e:GOOL_NATIVE_GEOMETRY_INVALID;
    o->phase=1;return 2;
}
int gool_native_geometry_search_begin(gool_native_geometry_search *o,gool_native_factory *f,
    gool_native_dispatch *outer,uint32_t argument_address) {
    if(!o||o->factory||o->active||!f||!f->open||!outer||!outer->active||
       outer->status!=GOOL_NATIVE_DISPATCH_GEOMETRY||!outer->actor||outer->actor->enabled||
       outer->actor->executing||outer->actor->registry!=f->registry||
       !outer->actor->vm->geometry_pending||!f->engine.tick||!f->engine.geometry||
       !f->engine.geometry->hierarchy_scan_limit)return GOOL_NATIVE_GEOMETRY_INVALID;
    gool_vm *vm=outer->actor->vm;uint32_t instruction=vm->geometry_instruction;
    if((instruction>>24)!=0x1cu||((instruction>>20)&15u)!=13u)return GOOL_NATIVE_GEOMETRY_INVALID;
    *o=(gool_native_geometry_search){.factory=f,.outer=outer,.argument=0x100u,
        .argument_address=argument_address,.tick=*f->engine.tick,.mask=(instruction>>15)&31u,
        .distance=INT32_MAX,.active=1,.status=1};
    int e=rd(o,outer->actor->node+0x40u+((instruction>>10)&0x1cu),&o->excluded);if(e)return fail(o,e);
    e=rd(o,outer->actor->node+0xc0u,&o->saved_pc);if(e)return fail(o,e);
    e=rd(o,outer->actor->node+0xc4u,&o->saved_fp);if(e)return fail(o,e);
    e=gool_domain_map_readonly(f->registry->domain,argument_address,&o->argument,4,&o->argument_lease);
    if(e)return fail(o,e);
    o->argument_mapped=1;
    e=gool_search_begin(&o->search,0x80068ec0u,0,f->engine.geometry->hierarchy_scan_limit,
                        gool_native_registry_read,f->registry);
    if(e!=GOOL_SEARCH_BUDGET)return fail(o,e<0?e:GOOL_NATIVE_GEOMETRY_INVALID);
    o->search.predicate=predicate;o->search.predicate_context=o;o->search.predicate_resumable=1;return 1;
}
gool_native_dispatch *gool_native_geometry_search_pending(gool_native_geometry_search *o) {
    if(o&&o->dispatch.active&&(o->dispatch.status==GOOL_NATIVE_DISPATCH_SEND||
       o->dispatch.status==GOOL_NATIVE_DISPATCH_STATE||o->dispatch.status==GOOL_NATIVE_DISPATCH_GEOMETRY||o->dispatch.status==GOOL_NATIVE_DISPATCH_SPAWN||o->dispatch.status==GOOL_NATIVE_DISPATCH_RESOURCE))return &o->dispatch;
    return 0;
}
int gool_native_geometry_search_step(gool_native_geometry_search *o,uint32_t budget) {
    if(!o||!o->factory)return GOOL_NATIVE_GEOMETRY_INVALID;
    if(!o->active||o->status<0||!budget)return o->status;
    while(budget--) {
        uint32_t tick;int e=rd(o,0x80068eecu,&tick);if(e)return fail(o,e);
        if(*o->factory->engine.tick!=o->tick||tick!=(uint32_t)o->tick)return fail(o,GOOL_NATIVE_GEOMETRY_INVALID);
        if(o->phase&&o->dispatch.active) {
            if(gool_native_geometry_search_pending(o))return 1;
            e=gool_native_dispatch_step(&o->dispatch,1);if(e<0)return fail(o,e);
            continue;
        }
        e=gool_search_step(&o->search,1);if(e<0)return fail(o,e);
        if(e)continue;
        if(!o->root&&(o->mask&0x17u)) {
            o->root=1;e=gool_search_begin(&o->search,0x80068eb8u,0,
                o->factory->engine.geometry->hierarchy_scan_limit,gool_native_registry_read,o->factory->registry);
            if(e!=GOOL_SEARCH_BUDGET)return fail(o,e<0?e:GOOL_NATIVE_GEOMETRY_INVALID);
            o->search.predicate=predicate;o->search.predicate_context=o;o->search.predicate_resumable=1;continue;
        }
        gool_native_actor *a=o->outer->actor;gool_domain *d=o->factory->registry->domain;uint32_t sp;
        e=gool_domain_read32(d,a->node+0xbcu,&sp);if(e)return fail(o,e);
        if((sp&3u)||sp<a->stack_address||(uint64_t)sp+4u>(uint64_t)a->stack_address+a->stack_words*4u)
            return fail(o,GOOL_VM_STACK);
        e=gool_domain_write32(d,sp,o->best);if(e)return fail(o,e);
        e=gool_domain_write32(d,a->node+0xbcu,sp+4u);if(e)return fail(o,e);
        e=gool_domain_write32(d,a->node+0xc0u,o->saved_pc);if(e)return fail(o,e);
        e=gool_domain_write32(d,a->node+0xc4u,o->saved_fp);if(e)return fail(o,e);
        a->vm->geometry_pending=0;
        e=gool_native_dispatch_resume(o->outer,0,0);
        if(e!=GOOL_NATIVE_DISPATCH_BUDGET)return fail(o,e<0?e:GOOL_NATIVE_GEOMETRY_INVALID);
        o->active=0;o->status=0;return 0;
    }
    return 1;
}
int gool_native_geometry_search_close(gool_native_geometry_search *o) {
    if(!o)return GOOL_NATIVE_GEOMETRY_INVALID;
    if(!o->factory)return 0;
    if(o->dispatch.active) {
        int e=gool_native_dispatch_abort(&o->dispatch,GOOL_NATIVE_GEOMETRY_INVALID);
        if(o->dispatch.active)return e;
    }
    int e=release_candidate(o);if(e)return e;
    if(o->argument_mapped){e=gool_domain_release(o->factory->registry->domain,o->argument_lease);if(e)return e;}
    *o=(gool_native_geometry_search){0};return 0;
}
