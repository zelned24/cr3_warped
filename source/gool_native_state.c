#include "gool_native_state.h"

static gool_native_actor *find_actor(gool_native_state *o,uint32_t node) {
    for(uint32_t i=0;i<o->registry->count;i++) {
        gool_native_actor *a=o->registry->actors[i];
        if(a&&a->attached&&a->node==node)return a;
    }
    return 0;
}
static int read_word(void *context,uint32_t address,uint32_t *value) {
    gool_native_state *o=context;return gool_native_registry_read(o->registry,address,value);
}
static int read_half(void *context,uint32_t address,uint16_t *value) {
    gool_native_state *o=context;return gool_domain_read16(o->registry->domain,address,value);
}
static int write_word(void *context,uint32_t address,uint32_t value) {
    gool_native_state *o=context;
    int error=gool_native_registry_write(o->registry,address,value);if(error)return error;
    if(address==o->state.node+0xbcu&&o->state.phase==4&&!o->state.resume) {
        gool_native_actor *a=find_actor(o,o->state.node);
        if(a) {
            if(value<a->stack_address+4u||value>a->stack_address+a->stack_words*4u||
               ((value-a->stack_address)&3u))return GOOL_VM_STACK;
            a->vm->frame_token_index=(value-a->stack_address)/4u-1u;
            a->vm->frame_yield=1;
        }
    }
    if(address==o->state.node+0xbcu&&o->state.phase==1) {
        /* Original1D454..45C replaces the old stack. A token from the previous
         * state no longer owns any slot in this new root frame. */
        gool_native_actor *a=find_actor(o,o->state.node);
        if(a){a->vm->frame_yield=0;a->vm->frame_token_index=0;a->vm->next_update=0;}
    }
    return 0;
}
static int load(void *context,uint32_t address,uint32_t *value) {
    gool_native_state *o=context;return gool_native_resources_load(o->registry->resources,address,value);
}
static int object_end(void *context,uint32_t node,uint32_t *end) {
    gool_native_state *o=context;return gool_native_pool_object_end(o->pool,node,end);
}
static int retain_actor(gool_native_state *o,gool_native_actor *a) {
    if(!a||!a->attached||a->registry!=o->registry||a->node!=o->state.node||a->enabled||a->executing||
       a->error||a->call_references==UINT32_MAX)return GOOL_NATIVE_ACTOR_BUSY;
    if(o->actor_retained)return o->actor==a?0:GOOL_NATIVE_ACTOR_BUSY;
    a->call_references++;o->actor=a;o->actor_retained=1;return 0;
}
static int enter_frame(void *context,uint32_t node,uint32_t argc,uint32_t flags) {
    gool_native_state *o=context;gool_native_actor *a=find_actor(o,node);
    if(a) {
        int error=retain_actor(o,a);if(error)return error;
        return gool_native_actor_enter_frame(a,argc,flags);
    }
    return gool_native_pool_enter_frame(o->pool,node,argc,flags);
}
static int dispatch(void *context,uint32_t node,uint32_t mode,int32_t *result) {
    gool_native_state *o=context;
    if(!o->dispatch_started) {
        gool_native_actor *a=find_actor(o,node);int error;
        if(!a) {
            error=o->engine.actor(o->engine.context,node,&a);if(error)return error;
            if(find_actor(o,node)!=a)return GOOL_NATIVE_ACTOR_INVALID;
        }
        error=retain_actor(o,a);if(error)return error;
        error=gool_native_dispatch_begin(&o->dispatch,a,mode);
        if(error!=GOOL_NATIVE_DISPATCH_BUDGET)return error<0?error:GOOL_NATIVE_DISPATCH_INVALID;
        o->dispatch_started=1;
    }
    int status=gool_native_dispatch_step(&o->dispatch,o->instruction_budget);
    if(status>0)return GOOL_STATE_NATIVE_PROVIDER_PENDING;
    if(status<0)return status;
    *result=o->dispatch.native_result;
    o->dispatch_started=0;o->dispatch=(gool_native_dispatch){0};return 0;
}
static int release_call(gool_native_state *o) {
    if(o->actor_retained) {
        if(!o->actor||!o->actor->attached||!o->actor->call_references||o->actor->executing)
            return GOOL_NATIVE_ACTOR_BUSY;
        o->actor->call_references--;o->actor_retained=0;o->actor=0;
    }
    if(o->resource_retained) {
        int error=gool_native_resources_release(o->registry->resources,o->resource_generation);if(error)return error;
        o->resource_retained=0;
    }
    return 0;
}
int gool_native_state_bind(gool_native_state *o,gool_native_registry *registry,
    gool_native_pool *pool,const gool_native_state_engine *engine,uint32_t budget) {
    if(!o||o->bound||!registry||!registry->actors||!registry->resources||!registry->resources->open||
       !pool||!pool->open||pool->domain!=registry->domain||!engine||!engine->actor||!budget)
        return GOOL_STATE_NATIVE_INVALID;
    gool_native_state_engine copy=*engine;
    *o=(gool_native_state){.registry=registry,.pool=pool,.engine=copy,.instruction_budget=budget,.bound=1};return 0;
}
int gool_native_state_enter(void *context,uint32_t node,uint32_t index,uint32_t argc,
                             uint32_t arguments,uint32_t *result) {
    gool_native_state *o=context;
    if(!o||!o->bound||!result||argc>INT32_MAX)return GOOL_STATE_NATIVE_INVALID;
    if(o->active) {
        if(o->state.node!=node||o->state.state!=index||o->state.argc!=argc||o->state.arguments!=arguments)
            return GOOL_STATE_NATIVE_INVALID;
        if(o->status<0)return o->status;
    } else {
        if(o->resource_retained||o->actor_retained||o->dispatch_started)return GOOL_NATIVE_ACTOR_BUSY;
        gool_native_actor *a=find_actor(o,node);
        if(a&&(a->enabled||a->executing||a->error))return GOOL_NATIVE_ACTOR_BUSY;
        uint64_t generation;int error=gool_native_resources_retain(o->registry->resources,&generation);if(error)return error;
        o->resource_generation=generation;o->resource_retained=1;
        gool_state_native_services services={.context=o,.read=read_word,.read16=read_half,.write=write_word,
            .load=load,.object_end=object_end,.enter_frame=enter_frame,.dispatch=dispatch};
        error=gool_state_native_begin(&o->state,&services,node,index,(int32_t)argc,arguments);
        if(error!=GOOL_STATE_NATIVE_BUDGET) {
            int cleanup=release_call(o);return cleanup?cleanup:error;
        }
        o->active=1;o->status=GOOL_STATE_NATIVE_BUDGET;
        if(a){error=retain_actor(o,a);if(error){o->status=error;return error;}}
    }
    int status=gool_state_native_step(&o->state,32);o->status=status;
    if(status>0)return GOOL_STATE_NATIVE_PROVIDER_PENDING;
    if(status<0)return status;
    int error=release_call(o);if(error){o->status=error;return error;}
    *result=(uint32_t)o->state.native_result;o->active=0;return 0;
}
gool_native_dispatch *gool_native_state_pending(gool_native_state *o) {
    if(!o||!o->bound||!o->active||!o->dispatch_started||!o->dispatch.active||
       (o->dispatch.status!=GOOL_NATIVE_DISPATCH_SEND&&o->dispatch.status!=GOOL_NATIVE_DISPATCH_STATE&&
        o->dispatch.status!=GOOL_NATIVE_DISPATCH_GEOMETRY&&o->dispatch.status!=GOOL_NATIVE_DISPATCH_SPAWN&&o->dispatch.status!=GOOL_NATIVE_DISPATCH_RESOURCE))return 0;
    return &o->dispatch;
}
int gool_native_state_close(gool_native_state *o) {
    if(!o)return GOOL_STATE_NATIVE_INVALID;
    if(o->dispatch.active) {
        int error=gool_native_dispatch_abort(&o->dispatch,GOOL_STATE_NATIVE_INVALID);
        if(o->dispatch.active)return error;
    }
    int error=release_call(o);if(error)return error;
    *o=(gool_native_state){0};return 0;
}

int gool_native_state_apply(gool_native_state *o,gool_native_factory *factory,gool_native_dispatch *outer) {
    if(!o||!o->bound||!factory||!factory->open||factory->registry!=o->registry||!outer||
       !outer->actor||outer->actor->registry!=o->registry)return GOOL_STATE_NATIVE_INVALID;
    if(!o->apply_phase) {
        if(o->active||!outer->active||outer->status!=GOOL_NATIVE_DISPATCH_STATE||
           outer->actor->enabled||outer->actor->executing)return GOOL_NATIVE_ACTOR_BUSY;
        gool_native_request *request=gool_native_factory_request(factory,outer->actor->node);
        if(!request)return GOOL_STATE_NATIVE_INVALID;
        o->apply_request=request;o->apply_generation=request->generation;
        o->request_factory=factory;o->outer_dispatch=outer;o->apply_node=outer->actor->node;o->apply_phase=1;
    } else if(o->request_factory!=factory||o->outer_dispatch!=outer||o->apply_node!=outer->actor->node)
        return GOOL_STATE_NATIVE_INVALID;
    if(o->apply_phase==1) {
        gool_native_request *request=gool_native_factory_request(factory,o->apply_node);
        if(!request||request!=o->apply_request||request->generation!=o->apply_generation)
            return GOOL_STATE_NATIVE_INVALID;
        uint32_t result;int error=gool_native_state_enter(o,o->apply_node,request->state,request->argc,
                                                        request->arguments_address,&result);
        if(error)return error;
        o->apply_result=result<=INT32_MAX?(int32_t)result:-1-(int32_t)(UINT32_MAX-result);
        o->apply_phase=2;
    }
    if(o->apply_phase==2) {
        gool_native_request *request=gool_native_factory_request(factory,o->apply_node);
        if(!request||request!=o->apply_request||request->generation!=o->apply_generation)
            return GOOL_STATE_NATIVE_INVALID;
        int error=gool_native_factory_complete(factory,o->apply_node,o->apply_result,&o->apply_finish);
        if(error)return error;
        o->apply_phase=3;
    }
    int error=gool_native_dispatch_resume(outer,o->apply_finish,o->apply_result);
    if(error<0)return error;
    o->apply_phase=0;o->request_factory=0;o->outer_dispatch=0;
    o->apply_request=0;o->apply_generation=0;return 0;
}
