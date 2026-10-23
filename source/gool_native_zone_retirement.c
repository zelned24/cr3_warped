#include "gool_native_zone_retirement.h"

enum {OP_IDLE=0,OP_SIGNAL=1,OP_RETIRE=2};
static int timeline(gool_native_zone_retirement *o) {
    if(!o->factory->open||!o->factory->engine.tick||*o->factory->engine.tick!=o->tick)
        return GOOL_NATIVE_ZONE_RETIREMENT_TIMELINE;
    uint32_t tick;int error=gool_native_registry_read(o->factory->registry,0x80068eecu,&tick);
    if(error)return error;
    return tick==(uint32_t)o->tick?0:GOOL_NATIVE_ZONE_RETIREMENT_TIMELINE;
}
static int read_node(void *context,uint32_t node,uint32_t offset,uint32_t *value) {
    gool_native_zone_retirement *o=context;
    if(node>UINT32_MAX-offset)return GOOL_ZONE_RETIRE_INVALID;
    return gool_native_registry_read(o->factory->registry,node+offset,value);
}
static int write_node(void *context,uint32_t node,uint32_t offset,uint32_t value) {
    gool_native_zone_retirement *o=context;
    if(node>UINT32_MAX-offset)return GOOL_ZONE_RETIRE_INVALID;
    int error=gool_native_registry_write(o->factory->registry,node+offset,value);if(error)return error;
    if(!offset&&!value)for(uint32_t i=0;i<o->factory->count;i++) {
        gool_native_factory_slot *s=&o->factory->slots[i];
        if(s->node==node&&s->actor.attached)return gool_native_factory_retired(o->factory,node);
    }
    return 0;
}
static int close_signal(gool_native_zone_retirement *o) {
    int error=gool_native_router_close(&o->router);if(error)return error;
    error=gool_native_state_close(&o->state);if(error)return error;
    error=gool_native_delivery_close(&o->delivery);if(error)return error;
    o->signal_owned=0;o->signal_node=o->signal_word=0;return 0;
}
static int poll_signal(gool_native_zone_retirement *o,uint32_t node,uint32_t signal) {
    int error;
    if(!o->signal_owned) {
        o->signal_owned=1;o->signal_node=node;o->signal_word=signal;
        gool_native_state_engine engine={.context=o->factory,.actor=gool_native_factory_actor};
        error=gool_native_state_bind(&o->state,o->factory->registry,o->factory->pool,
            &engine,o->instruction_budget);if(error)return error;
        error=gool_native_delivery_bind(&o->delivery,o->factory,&o->state,
            o->arguments_base,o->instruction_budget);if(error)return error;
        error=gool_native_router_bind(&o->router,o->factory,o->frames,o->capacity,
            o->arguments_base+256u);if(error)return error;
        error=gool_native_delivery_begin_native(&o->delivery,0,node,(int32_t)signal,0,0);
        if(error<0)return error;
        if(error!=0&&error!=1)return GOOL_ZONE_RETIRE_INVALID;
    } else if(o->signal_node!=node||o->signal_word!=signal)return GOOL_ZONE_RETIRE_INVALID;
    error=gool_native_router_engine_delivery_step(&o->router,&o->delivery,1);
    if(error)return error;
    return close_signal(o);
}
static int notify_node(void *context,uint32_t node,uint32_t signal) {
    gool_native_zone_retirement *o=context;int error=poll_signal(o,node,signal);
    return error==1?GOOL_DESTROY_PROVIDER_PENDING:error;
}
static int audio_node(void *context,uint32_t node) {
    gool_native_zone_retirement *o=context;
    if(!o->audio_owned){o->audio_owned=1;o->audio_node=node;}
    else if(o->audio_node!=node)return GOOL_ZONE_RETIRE_INVALID;
    int error=o->audio.stop_audio(o->audio.context,node);
    if(!error){o->audio_owned=0;o->audio_node=0;}
    return error==1?GOOL_DESTROY_PROVIDER_PENDING:error;
}
static int start_operation(gool_native_zone_retirement *o,int operation,uint32_t node) {
    if(!o->bound||o->status<0)return o->status<0?o->status:GOOL_ZONE_RETIRE_INVALID;
    if(o->operation)return o->operation==operation&&o->node==node?timeline(o):GOOL_ZONE_RETIRE_INVALID;
    if(!node||!o->factory->open||!o->factory->engine.tick)return GOOL_ZONE_RETIRE_INVALID;
    o->tick=*o->factory->engine.tick;
    int error=timeline(o);if(error)return error;
    o->operation=operation;o->node=node;return 0;
}
static int signal_entry(void *context,uint32_t node,uint32_t signal) {
    gool_native_zone_retirement *o=context;int error=start_operation(o,OP_SIGNAL,node);
    if(!error)error=poll_signal(o,node,signal);
    if(!error)error=timeline(o);
    if(error<0)o->status=error;
    if(!error){o->operation=OP_IDLE;o->node=0;}
    return error;
}
static int retire_entry(void *context,uint32_t node,uint32_t notify,int32_t *native_result) {
    gool_native_zone_retirement *o=context;
    if(!native_result)return GOOL_ZONE_RETIRE_INVALID;
    int fresh=o->operation==OP_IDLE,error=start_operation(o,OP_RETIRE,node);
    if(error)return o->status=error;
    if(fresh) {
        o->notify=notify;
        error=gool_destroy_begin(&o->destroy,node,notify,&o->destruction);
        if(error!=GOOL_DESTROY_BUDGET)return o->status=error?error:GOOL_ZONE_RETIRE_INVALID;
    } else if(o->notify!=notify)return o->status=GOOL_ZONE_RETIRE_INVALID;
    error=gool_destroy_step(&o->destroy,1);
    if(error<0)return o->status=error;
    if(error)return 1;
    error=timeline(o);if(error)return o->status=error;
    *native_result=-255;o->operation=OP_IDLE;o->node=o->notify=0;return 0;
}
static int cancel_entry(void *context,uint32_t node,int retiring) {
    gool_native_zone_retirement *o=context;
    if(o->operation&&(o->node!=node||o->operation!=(retiring?OP_RETIRE:OP_SIGNAL)))
        return GOOL_ZONE_RETIRE_INVALID;
    int error=close_signal(o);if(error)return error;
    if(o->audio_owned) {
        error=o->audio.cancel_audio(o->audio.context,o->audio_node);if(error)return error;
        o->audio_owned=0;o->audio_node=0;
    }
    /* Cursors own no other external jobs. Preserve already committed graph
     * effects; successful cancellation releases continuation ownership only. */
    o->destroy=(gool_destroy){0};o->operation=OP_IDLE;o->node=o->notify=0;o->status=0;
    return 0;
}
int gool_native_zone_retirement_bind(gool_native_zone_retirement *o,
    gool_native_factory *factory,gool_native_route_frame *frames,uint32_t capacity,
    uint32_t arguments_base,uint32_t instruction_budget,uint32_t max_nodes,
    const gool_destroy_services *destruction,const gool_native_retirement_audio *audio) {
    if(!o||o->bound||!factory||!factory->open||!factory->registry||!factory->registry->domain||
       !factory->pool||!factory->engine.tick||!frames||!capacity||capacity>GOOL_NATIVE_ROUTER_DEPTH||
       !arguments_base||(arguments_base&3u)||(uint64_t)arguments_base+(capacity+1u)*256u>UINT64_C(0x100000000)||
       !instruction_budget||!max_nodes||!destruction||!destruction->protected_object||
       !destruction->detached_object||!destruction->usage||!destruction->usage_count||
       !destruction->free_root||!destruction->scan_limit||!audio||!audio->stop_audio||!audio->cancel_audio)
        return GOOL_ZONE_RETIRE_INVALID;
    o->factory=factory;o->frames=frames;o->capacity=capacity;o->arguments_base=arguments_base;
    o->instruction_budget=instruction_budget;o->audio=*audio;o->destruction=*destruction;
    o->destruction.links=(gool_tree_services){.context=o,.read=read_node,.write=write_node};
    o->destruction.notify=notify_node;o->destruction.stop_audio=audio_node;
    o->services=(gool_zone_retire_services){.context=o,.domain=factory->registry->domain,
        .max_nodes=max_nodes,.signal=signal_entry,.retire=retire_entry,.cancel=cancel_entry};
    o->bound=1;return 0;
}
int gool_native_zone_retirement_close(gool_native_zone_retirement *o) {
    if(!o)return GOOL_ZONE_RETIRE_INVALID;
    if(!o->bound)return 0;
    int error=cancel_entry(o,o->node,o->operation==OP_RETIRE);if(error)return error;
    *o=(gool_native_zone_retirement){0};return 0;
}
