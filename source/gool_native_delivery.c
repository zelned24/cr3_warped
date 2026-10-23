#include "gool_native_delivery.h"
#include "gool_send.h"
#include <string.h>
static gool_native_registry *registry(gool_native_delivery *o){return o->factory->registry;}
static gool_native_actor *find(gool_native_delivery *o,uint32_t node) {
    gool_native_registry *r=registry(o);
    for(uint32_t i=0;i<r->count;i++)if(r->actors[i]&&r->actors[i]->attached&&r->actors[i]->node==node)return r->actors[i];
    return 0;
}
static int capture(gool_native_delivery *o,gool_native_actor *a) {
    if(!a||!a->attached||a->registry!=registry(o)||a->enabled||a->executing||a->error)
        return GOOL_NATIVE_ACTOR_BUSY;
    if(o->target_context)return o->target==a?0:GOOL_NATIVE_ACTOR_BUSY;
    if(a->call_references==UINT32_MAX)return GOOL_NATIVE_ACTOR_BUSY;
    o->target=a;a->call_references++;o->target_retained=1;
    o->previous_receiver=a->vm->receiver;o->previous_send=a->vm->send;
    o->previous_send_pending=a->vm->send_pending;
    a->vm->send_pending=0;o->target_context=1;return 0;
}
static int read_word(void *ctx,uint32_t address,uint32_t *value) {
    return gool_native_registry_read(registry(ctx),address,value);
}
static int write_word(void *ctx,uint32_t address,uint32_t value) {
    return gool_native_registry_write(registry(ctx),address,value);
}
static int read_half(void *ctx,uint32_t address,uint16_t *value) {
    return gool_domain_read16(registry(ctx)->domain,address,value);
}
static int object_end(void *ctx,uint32_t node,uint32_t *end) {
    gool_native_delivery *o=ctx;return gool_native_pool_object_end(o->factory->pool,node,end);
}
static int enter_frame(void *ctx,uint32_t node,uint32_t argc,uint32_t flags) {
    gool_native_delivery *o=ctx;gool_native_actor *a=find(o,node);
    if(!a)return gool_native_pool_enter_frame(o->factory->pool,node,argc,flags);
    int error=capture(o,a);if(error)return error;
    return gool_native_actor_enter_frame(a,argc,flags);
}
static int dispatch(void *ctx,uint32_t node,uint32_t mode,gool_signal_receiver *receiver,int32_t *result) {
    gool_native_delivery *o=ctx;int error;
    if(!o->dispatch_started) {
        gool_native_actor *a=find(o,node);
        if(!a){error=gool_native_factory_actor(o->factory,node,&a);if(error)return error;}
        error=capture(o,a);if(error)return error;
        a->vm->receiver=receiver;
        error=gool_native_dispatch_begin(&o->dispatch,a,mode);
        if(error!=GOOL_NATIVE_DISPATCH_BUDGET)return error<0?error:GOOL_NATIVE_DISPATCH_INVALID;
        o->dispatch_started=1;
    }
    error=gool_native_dispatch_step(&o->dispatch,o->instruction_budget);
    if(error>0)return GOOL_SEND_NATIVE_PROVIDER_PENDING;
    if(error<0)return error;
    *result=o->dispatch.native_result;
    o->target->vm->receiver=o->previous_receiver;
    o->dispatch_started=0;o->dispatch=(gool_native_dispatch){0};return 0;
}
static int enter_state(void *ctx,uint32_t node,uint32_t state,uint32_t argc,uint32_t arguments,int32_t *result) {
    gool_native_delivery *o=ctx;gool_native_actor *a=find(o,node);int error;
    if(a){error=capture(o,a);if(error)return error;}
    uint32_t value;error=gool_native_state_enter(o->state,node,state,argc,arguments,&value);if(error)return error;
    *result=value<=INT32_MAX?(int32_t)value:-1-(int32_t)(UINT32_MAX-value);return 0;
}
static int restore_target(gool_native_delivery *o,int cancel) {
    if(!o->target_context)return 0;
    gool_native_actor *a=o->target;
    if(!a||!a->attached||a->enabled||a->executing||(!cancel&&a->vm->send_pending))return GOOL_NATIVE_ACTOR_BUSY;
    a->vm->receiver=o->previous_receiver;
    a->vm->send=o->previous_send;a->vm->send_pending=o->previous_send_pending;
    o->target_context=0;return 0;
}
static int release(gool_native_delivery *o,int cancel) {
    int error=restore_target(o,cancel);if(error)return error;
    if(o->arguments_mapped) {
        uint32_t references;error=gool_domain_references(registry(o)->domain,o->arguments_lease,&references);if(error)return error;
        if(references!=1)return GOOL_NATIVE_ACTOR_BUSY;
        error=gool_domain_release(registry(o)->domain,o->arguments_lease);if(error)return error;o->arguments_mapped=0;
    }
    if(o->target_retained) {
        if(!o->target->attached||!o->target->call_references)return GOOL_NATIVE_ACTOR_INVALID;
        o->target->call_references--;o->target_retained=0;
    }
    if(o->sender_retained) {
        gool_native_actor *sender=o->native_entry?o->native_sender:o->outer->actor;
        if(!sender||!sender->attached||!sender->call_references)return GOOL_NATIVE_ACTOR_INVALID;
        sender->call_references--;o->sender_retained=0;
    }
    if(o->resource_retained) {
        error=gool_native_resources_release(registry(o)->resources,o->resource_generation);if(error)return error;
        o->resource_retained=0;
    }
    return 0;
}
int gool_native_delivery_bind(gool_native_delivery *o,gool_native_factory *factory,
    gool_native_state *state,uint32_t address,uint32_t budget) {
    if(!o||o->bound||!factory||!factory->open||!state||!state->bound||state->registry!=factory->registry||
       state->pool!=factory->pool||state->active||!address||(address&3u)||address>UINT32_MAX-28u||!budget)
        return GOOL_SEND_NATIVE_INVALID;
    *o=(gool_native_delivery){.factory=factory,.state=state,.arguments_address=address,.instruction_budget=budget,.bound=1};return 0;
}
int gool_native_delivery_begin(gool_native_delivery *o,gool_native_dispatch *outer) {
    if(!o||!o->bound||o->active||o->arguments_mapped||o->resource_retained||o->sender_retained||o->target_retained||
       !outer||!outer->active||outer->status!=GOOL_NATIVE_DISPATCH_SEND||!outer->actor||
       outer->actor->registry!=registry(o)||outer->actor->enabled||outer->actor->executing||
       !outer->actor->vm->send_pending||outer->actor->vm->send.argument_count>7||
       outer->actor->call_references==UINT32_MAX||o->state->active)return GOOL_SEND_NATIVE_INVALID;
    o->native_entry=0;o->native_sender=0;o->outer=outer;o->request=outer->actor->vm->send;
    memcpy(o->arguments,o->request.arguments,sizeof(o->arguments));
    int error=gool_native_resources_retain(registry(o)->resources,&o->resource_generation);if(error)return error;
    o->resource_retained=1;outer->actor->call_references++;o->sender_retained=1;
    error=gool_domain_map_readonly(registry(o)->domain,o->arguments_address,o->arguments,sizeof(o->arguments),&o->arguments_lease);
    if(error){int cleanup=release(o,0);return cleanup?cleanup:error;}
    o->arguments_mapped=1;
    gool_send_native_services services={.context=o,.read=read_word,.read16=read_half,.write=write_word,
        .object_end=object_end,.enter_frame=enter_frame,.dispatch=dispatch,.enter_state=enter_state};
    error=gool_send_native_begin(&o->send,&services,outer->actor->node,(uint32_t)o->request.target,
                                 o->request.raw_signal,o->request.argument_count,o->arguments_address);
    if(error!=GOOL_SEND_NATIVE_BUDGET){int cleanup=release(o,0);return cleanup?cleanup:error;}
    o->active=1;o->phase=0;o->status=GOOL_SEND_NATIVE_BUDGET;o->wrapper_finished=0;return o->status;
}
int gool_native_delivery_begin_native(gool_native_delivery *o,uint32_t sender,uint32_t target,
    int32_t signal,uint32_t argc,uint32_t arguments) {
    if(!o||!o->bound||o->active||o->arguments_mapped||o->resource_retained||o->sender_retained||
       o->target_retained||o->target_context||o->dispatch_started||o->state->active||
       !o->factory->open||!o->factory->engine.tick||(sender&3u)||(target&3u)||argc>15u||
       (argc&&(!arguments||(arguments&3u)||(uint64_t)arguments+argc*4u>UINT32_MAX)))
        return GOOL_SEND_NATIVE_INVALID;
    gool_native_actor *a=sender?find(o,sender):0;
    if(sender&&(!a||a->registry!=registry(o)||a->enabled||a->executing||a->error||
                a->call_references==UINT32_MAX))return GOOL_NATIVE_ACTOR_BUSY;
    uint32_t raw_tick;int error=gool_domain_read32(registry(o)->domain,0x80068eecu,&raw_tick);
    if(error)return error;
    if(raw_tick!=(uint32_t)*o->factory->engine.tick)return GOOL_SEND_NATIVE_INVALID;
    o->native_tick=*o->factory->engine.tick;
    o->native_entry=1;o->native_sender=a;o->outer=0;
    error=gool_native_resources_retain(registry(o)->resources,&o->resource_generation);
    if(error)return error;
    o->resource_retained=1;
    if(a){a->call_references++;o->sender_retained=1;}
    gool_send_native_services services={.context=o,.read=read_word,.read16=read_half,.write=write_word,
        .object_end=object_end,.enter_frame=enter_frame,.dispatch=dispatch,.enter_state=enter_state};
    error=gool_send_native_begin(&o->send,&services,sender,target,signal,argc,arguments);
    if(error!=GOOL_SEND_NATIVE_BUDGET){int cleanup=release(o,1);return cleanup?cleanup:error;}
    o->active=1;o->phase=0;o->status=GOOL_SEND_NATIVE_BUDGET;o->wrapper_finished=0;
    return o->status;
}
int gool_native_delivery_step(gool_native_delivery *o,uint32_t budget) {
    if(!o||!o->bound)return GOOL_SEND_NATIVE_INVALID;
    if(o->native_entry&&o->status<0)return o->status;
    if(!o->active||!budget)return o->status;
    int error;
    if(o->native_entry) {
        uint32_t raw_tick;
        if(!o->factory->open||!o->factory->engine.tick||*o->factory->engine.tick!=o->native_tick)
            return o->status=GOOL_SEND_NATIVE_INVALID;
        error=gool_domain_read32(registry(o)->domain,0x80068eecu,&raw_tick);
        if(error)return o->status=error;
        if(raw_tick!=(uint32_t)o->native_tick)return o->status=GOOL_SEND_NATIVE_INVALID;
    }
    if(o->phase==0) {
        error=gool_send_native_step(&o->send,budget);o->status=error;
        if(error)return error;
        o->phase=1;
    }
    if(o->phase==1) {
        if(o->native_entry) {
            o->native_result=o->send.native_result;
            error=release(o,0);if(error){o->status=error;return error;}
            o->active=0;o->status=0;return 0;
        }
        gool_native_actor *a=o->outer->actor;
        if(!o->wrapper_finished) {
            error=restore_target(o,0);if(error){o->status=error;return error;}
            a->vm->send=o->request;a->vm->send_pending=1;
            if(a->flow_dirty){error=gool_native_actor_pull_flow(a);if(error){o->status=error;return error;}}
            error=gool_send_finish(a->vm);
            if(error!=GOOL_VM_RUNNING&&error!=GOOL_VM_SEND_REPLACED){o->status=error;return error;}
            o->finish_outer=error==GOOL_VM_SEND_REPLACED&&(o->request.caller_mode&2u);
            o->wrapper_finished=1;
        }
        error=gool_native_actor_publish(a);if(error){o->status=error;return error;}
        o->phase=2;
    }
    error=release(o,0);if(error){o->status=error;return error;}
    error=gool_native_dispatch_resume(o->outer,o->finish_outer,-255);
    if(error<0){o->status=error;return error;}
    o->active=0;o->status=0;return 0;
}
gool_native_dispatch *gool_native_delivery_pending(gool_native_delivery *o) {
    if(!o||!o->active)return 0;
    if(o->dispatch_started&&o->dispatch.active&&
       (o->dispatch.status==GOOL_NATIVE_DISPATCH_SEND||o->dispatch.status==GOOL_NATIVE_DISPATCH_STATE||
        o->dispatch.status==GOOL_NATIVE_DISPATCH_GEOMETRY||o->dispatch.status==GOOL_NATIVE_DISPATCH_SPAWN||o->dispatch.status==GOOL_NATIVE_DISPATCH_RESOURCE))return &o->dispatch;
    return gool_native_state_pending(o->state);
}
int gool_native_delivery_close(gool_native_delivery *o) {
    if(!o)return GOOL_SEND_NATIVE_INVALID;
    if(!o->bound)return 0;
    if(o->dispatch.active) {
        int error=gool_native_dispatch_abort(&o->dispatch,GOOL_SEND_NATIVE_INVALID);
        if(o->dispatch.active)return error;
    }
    if(o->state->active||o->state->resource_retained||o->state->actor_retained)return GOOL_NATIVE_ACTOR_BUSY;
    int error=release(o,1);if(error)return error;
    *o=(gool_native_delivery){0};return 0;
}
