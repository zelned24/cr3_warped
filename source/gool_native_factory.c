#include "gool_native_factory.h"
#include <string.h>
static int geometry(void *context,uint32_t instruction) {
    gool_native_factory_slot *slot=context;
    if(!slot||!slot->owner||!slot->owner->open)return GOOL_NATIVE_ACTOR_INVALID;
    return gool_native_actor_geometry(&slot->actor,instruction,slot->owner->engine.geometry);
}
static int sound(void *context,uint32_t instruction) {
    gool_native_factory_slot *slot=context;
    if(!slot||!slot->owner||!slot->owner->open)return GOOL_NATIVE_ACTOR_INVALID;
    return gool_native_actor_sound(&slot->actor,instruction,slot->owner->engine.sound);
}
static int index_of(gool_native_factory *f,uint32_t node,uint32_t *index) {
    if(!f||!f->open)return GOOL_NATIVE_ACTOR_INVALID;
    if(node==GOOL_NATIVE_PLAYER_BASE)*index=GOOL_NATIVE_POOL_COUNT;
    else {
        if(node<GOOL_NATIVE_POOL_BASE)return GOOL_NATIVE_ACTOR_INVALID;
        uint32_t offset=node-GOOL_NATIVE_POOL_BASE;
        if(offset%GOOL_NATIVE_POOL_STRIDE||offset/GOOL_NATIVE_POOL_STRIDE>=GOOL_NATIVE_POOL_COUNT)
            return GOOL_NATIVE_ACTOR_INVALID;
        *index=offset/GOOL_NATIVE_POOL_STRIDE;
    }
    return *index<f->count?0:GOOL_NATIVE_ACTOR_INVALID;
}
static int change_state(void *context,uint32_t state,uint32_t argc,int meets) {
    gool_native_factory_slot *s=context;gool_native_factory *f=s->owner;
    gool_vm *vm=&s->vm;
    if(!f||!f->open||!s->actor.attached||argc>15||
       argc>gool_stack_height(&vm->stack))return GOOL_VM_STACK;
    if(!meets){vm->stack.top-=argc;return GOOL_VM_RUNNING;}
    int32_t arguments[15];
    if(argc)memcpy(arguments,vm->stack.words+vm->stack.top-argc,argc*sizeof(int32_t));
    vm->stack.top-=argc;
    gool_domain *d=f->registry->domain;uint32_t behavior,states,mode,flags,motion;
    int error=gool_domain_read32(d,s->node+0xc,&behavior);if(error)return error;
    gool_data base;error=gool_native_resources_behavior(f->registry->resources,behavior,&base);if(error)return error;
    if(state>=base.state_count)return GOOL_VM_INVALID;
    error=gool_domain_read32(d,behavior+0x20,&states);if(error)return error;
    if((uint64_t)states+state*16u>UINT32_MAX)return GOOL_VM_INVALID;
    error=gool_domain_read32(d,s->node+0x108,&mode);if(error)return error;
    error=gool_domain_read32(d,s->node+0xb0,&flags);if(error)return error;
    error=gool_domain_read32(d,states+state*16u,&motion);if(error)return error;
    if(mode-2u<3u)flags|=0x402u;
    if(flags&motion)return GOOL_VM_RUNNING;
    if(s->actor.call_references==UINT32_MAX)return GOOL_NATIVE_ACTOR_BUSY;
    if(f->request_generation==UINT64_MAX)return GOOL_NATIVE_ACTOR_BUSY;
    uint32_t i;
    for(i=0;i<GOOL_NATIVE_REQUEST_COUNT;i++)if(!f->requests[i].mapped)break;
    if(i==GOOL_NATIVE_REQUEST_COUNT)return GOOL_NATIVE_ACTOR_BUSY;
    gool_native_request *request=&f->requests[i];
    *request=(gool_native_request){.previous=s->request,.node=s->node,.state=state,.argc=argc,
        .arguments_address=f->request_base+i*256u,.request_mode=vm->mode};
    if(argc)memcpy(request->arguments,arguments,argc*sizeof(int32_t));
    error=gool_domain_map_readonly(d,request->arguments_address,request->arguments,
                                   sizeof(request->arguments),&request->request_lease);
    if(error){*request=(gool_native_request){0};return error;}
    request->mapped=1;request->generation=++f->request_generation;s->request=request;
    s->actor.call_references++;return GOOL_VM_STATE_REQUEST;
}
int gool_native_factory_open(gool_native_factory *f,gool_native_registry *registry,
    gool_native_pool *pool,gool_native_factory_slot *slots,uint32_t count,
    const gool_native_engine_data *engine,uint32_t request_base) {
    if(!f||f->open||!registry||!registry->actors||!pool||!pool->open||registry->domain!=pool->domain||
       !slots||count!=GOOL_NATIVE_POOL_COUNT+1||registry->count!=count||!engine||!engine->globals||
       !engine->global_count||!engine->random_state||!engine->phase_source||!engine->frame_factor||!engine->tick||
       (engine->send_enabled!=0&&engine->send_enabled!=1)||!request_base||(request_base&255u)||
       (uint64_t)request_base+(uint64_t)GOOL_NATIVE_REQUEST_COUNT*256>UINT32_MAX)return GOOL_NATIVE_ACTOR_INVALID;
    for(uint32_t i=0;i<count;i++)
        if(registry->actors[i]||slots[i].actor.attached||slots[i].request)
            return GOOL_NATIVE_ACTOR_BUSY;
    gool_native_engine_data copy=*engine;
    memset(f,0,sizeof(*f));f->registry=registry;f->pool=pool;f->slots=slots;
    f->count=count;f->request_base=request_base;f->engine=copy;f->open=1;
    for(uint32_t i=0;i<count;i++) {
        slots[i]=(gool_native_factory_slot){.owner=f};
    }
    return 0;
}
int gool_native_factory_actor(void *context,uint32_t node,gool_native_actor **out) {
    gool_native_factory *f=context;uint32_t index;
    if(!out)return GOOL_NATIVE_ACTOR_INVALID;
    int error=index_of(f,node,&index);if(error)return error;
    gool_native_factory_slot *s=&f->slots[index];
    if(s->actor.attached) {
        if(s->node!=node||s->actor.registry!=f->registry||s->actor.enabled||s->actor.executing||s->actor.error)
            return GOOL_NATIVE_ACTOR_BUSY;
        *out=&s->actor;return 0;
    }
    if(f->registry->actors[index]||s->request)return GOOL_NATIVE_ACTOR_BUSY;
    s->node=node;
    s->vm=(gool_vm){.globals=f->engine.globals,.global_count=f->engine.global_count,
        .random_state=f->engine.random_state,.phase_source=f->engine.phase_source,
        .frame_factor=f->engine.frame_factor,.tick=*f->engine.tick,.send_enabled=f->engine.send_enabled,
        .spawn_enabled=f->engine.creation!=0,.resource_enabled=f->engine.resource!=0,
        .change_state=change_state,.state_context=s};
    if(f->engine.geometry){s->vm.geometry=geometry;s->vm.geometry_context=s;}
    if(f->engine.sound){s->vm.sound=sound;s->vm.sound_context=s;}
    uint32_t address;gool_domain_lease object;
    error=gool_native_pool_object(f->pool,index,&address,&object);if(error)return error;
    if(address!=node)return GOOL_NATIVE_ACTOR_INVALID;
    error=gool_native_actor_attach_raw(&s->actor,f->registry,index,node,object,&s->vm);
    if(error)return error;
    s->actor.tick_source=f->engine.tick;
    error=gool_native_actor_stop(&s->actor);if(error)return error;
    *out=&s->actor;return 0;
}
gool_native_request *gool_native_factory_request(gool_native_factory *f,uint32_t node) {
    uint32_t index;if(index_of(f,node,&index))return 0;
    gool_native_factory_slot *s=&f->slots[index];
    return s->actor.attached&&s->request&&s->request->mapped&&s->request->node==node?s->request:0;
}
int gool_native_factory_complete(gool_native_factory *f,uint32_t node,int32_t result,int *finish) {
    gool_native_request *request=gool_native_factory_request(f,node);uint32_t index;
    if(!request||!finish||index_of(f,node,&index))return GOOL_NATIVE_ACTOR_BUSY;
    gool_native_factory_slot *s=&f->slots[index];
    if(s->actor.enabled||s->actor.executing||!s->actor.call_references)return GOOL_NATIVE_ACTOR_BUSY;
    uint32_t references;int error=gool_domain_references(f->registry->domain,request->request_lease,&references);if(error)return error;
    if(references!=1)return GOOL_NATIVE_ACTOR_BUSY;
    int complete=result<-254&&(request->request_mode&2u);
    if(!complete) {
        uint32_t sp;error=gool_domain_read32(f->registry->domain,node+0xbc,&sp);if(error)return error;
        if(sp<s->actor.stack_address+4u||sp>s->actor.stack_address+s->actor.stack_words*4u||(sp&3u))
            return GOOL_VM_STACK;
        error=gool_native_registry_write(f->registry,node+0xbc,sp-4);if(error)return error;
        s->vm.frame_yield=0;
    }
    error=gool_domain_release(f->registry->domain,request->request_lease);if(error)return error;
    s->request=request->previous;*request=(gool_native_request){0};s->actor.call_references--;
    *finish=complete;return 0;
}
int gool_native_factory_retired(gool_native_factory *f,uint32_t node) {
    uint32_t index;int error=index_of(f,node,&index);if(error)return error;
    gool_native_factory_slot *s=&f->slots[index];
    if(s->request)return GOOL_NATIVE_ACTOR_BUSY;
    if(!s->actor.attached)return GOOL_NATIVE_ACTOR_INVALID;
    error=gool_native_actor_detach(&s->actor);if(error)return error;
    *s=(gool_native_factory_slot){.owner=f};return 0;
}
int gool_native_factory_close(gool_native_factory *f) {
    if(!f)return GOOL_NATIVE_ACTOR_INVALID;
    if(!f->open)return 0;
    for(uint32_t i=0;i<f->count;i++)if(f->slots[i].actor.attached||f->slots[i].request)
        return GOOL_NATIVE_ACTOR_BUSY;
    for(uint32_t i=0;i<GOOL_NATIVE_REQUEST_COUNT;i++)if(f->requests[i].mapped)return GOOL_NATIVE_ACTOR_BUSY;
    for(uint32_t i=0;i<f->count;i++)f->slots[i]=(gool_native_factory_slot){0};
    memset(f,0,sizeof(*f));return 0;
}

int gool_native_factory_cancel_request(gool_native_factory *f,uint32_t node) {
    gool_native_request *request=gool_native_factory_request(f,node);uint32_t index;
    if(!request||index_of(f,node,&index))return GOOL_NATIVE_ACTOR_BUSY;
    gool_native_factory_slot *s=&f->slots[index];
    if(s->actor.enabled||s->actor.executing||!s->actor.call_references)return GOOL_NATIVE_ACTOR_BUSY;
    uint32_t references;int error=gool_domain_references(f->registry->domain,request->request_lease,&references);if(error)return error;
    if(references!=1)return GOOL_NATIVE_ACTOR_BUSY;
    error=gool_domain_release(f->registry->domain,request->request_lease);if(error)return error;
    s->request=request->previous;*request=(gool_native_request){0};s->actor.call_references--;return 0;
}
