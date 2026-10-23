#include "native_runtime.h"

static int runtime_read(void *context,uint32_t node,uint32_t offset,uint32_t *value) {
    native_runtime *o=context;
    if(node>UINT32_MAX-offset)return NATIVE_RUNTIME_INVALID;
    return gool_native_registry_read(o->factory->registry,node+offset,value);
}
static int runtime_write(void *context,uint32_t node,uint32_t offset,uint32_t value) {
    native_runtime *o=context;
    if(node>UINT32_MAX-offset)return NATIVE_RUNTIME_INVALID;
    int error=gool_native_registry_write(o->factory->registry,node+offset,value);
    if(error)return error;
    /* Destruction's type-zero publication follows child/notification/audio
     * retirement. Detach any adopted VM before the slot can be reused. This
     * also covers recursively deleted children that were never world-visited.
     * Raw unadopted children have no factory resources to detach. */
    if(!offset&&!value)for(uint32_t i=0;i<o->factory->count;i++) {
        gool_native_factory_slot *slot=&o->factory->slots[i];
        if(slot->node==node&&slot->actor.attached)
            return gool_native_factory_retired(o->factory,node);
    }
    return 0;
}
static int runtime_visit(void *context,uint32_t node,int32_t *result) {
    native_runtime *o=context;int error;
    if(!o->visiting) {
        gool_native_actor *actor=NULL;
        error=gool_native_factory_actor(o->factory,node,&actor);if(error)return error;
        error=gool_native_object_begin(&o->object,actor,o->requested);
        if(error!=GOOL_NATIVE_OBJECT_PENDING)return error?error:NATIVE_RUNTIME_INVALID;
        o->visiting=node;
    } else if(o->visiting!=node)return NATIVE_RUNTIME_INVALID;
    error=gool_native_object_step(&o->object,1);
    if(error==GOOL_NATIVE_OBJECT_PENDING)return GOOL_TREE_PROVIDER_PENDING;
    if(error)return error;
    *result=o->object.native_result;o->visiting=0;return 0;
}
static int runtime_notify(void *context,uint32_t node,uint32_t signal) {
    native_runtime *o=context;
    return o->retirement.notify(o->retirement.links.context,node,signal);
}
static int runtime_stop_audio(void *context,uint32_t node) {
    native_runtime *o=context;
    return o->retirement.stop_audio(o->retirement.links.context,node);
}
int native_runtime_bind(native_runtime *o,gool_native_factory *factory,
    gool_native_route_frame *frames,uint32_t capacity,uint32_t arguments_base,
    const gool_native_object_services *objects,const gool_destroy_services *retirement) {
    if(!o||o->bound||o->active||!factory||!factory->open||!factory->engine.tick||
       !retirement||!retirement->notify||!retirement->stop_audio||
       !retirement->protected_object||!retirement->detached_object||!retirement->usage||
       !retirement->usage_count||!retirement->free_root||!retirement->scan_limit)
        return NATIVE_RUNTIME_INVALID;
    int error=gool_native_object_bind(&o->object,factory,frames,capacity,arguments_base,objects);
    if(error)return error;
    o->factory=factory;o->retirement=*retirement;o->bound=1;return 0;
}
int native_runtime_begin(native_runtime *o,uint32_t requested) {
    if(!o||!o->bound||!o->factory->open||!o->factory->engine.tick)return NATIVE_RUNTIME_INVALID;
    if(o->active||o->visiting||o->object.active)return NATIVE_RUNTIME_BUSY;
    uint32_t tick;int error=gool_native_registry_read(o->factory->registry,0x80068eecu,&tick);
    if(error)return error;
    if(tick!=(uint32_t)*o->factory->engine.tick)return GOOL_NATIVE_OBJECT_TIMELINE;
    uint32_t roots[GOOL_TREE_ROOTS];
    for(uint32_t i=0;i<GOOL_TREE_ROOTS;i++)roots[i]=0x80068ea0u+i*8u;
    gool_destroy_services services=o->retirement;
    services.links=(gool_tree_services){.context=o,.read=runtime_read,.write=runtime_write,.visit=runtime_visit};
    services.notify=runtime_notify;services.stop_audio=runtime_stop_audio;
    error=gool_forest_begin(&o->forest,roots,&services);
    if(error!=GOOL_TREE_BUDGET)return error?error:NATIVE_RUNTIME_INVALID;
    o->tick=*o->factory->engine.tick;o->requested=requested;o->active=1;
    return o->status=NATIVE_RUNTIME_PENDING;
}
int native_runtime_step(native_runtime *o,uint32_t budget) {
    if(!o||!o->bound)return NATIVE_RUNTIME_INVALID;
    if(!o->active||o->status<0)return o->status;
    if(!o->factory->open||!o->factory->engine.tick||*o->factory->engine.tick!=o->tick)
        return o->status=GOOL_NATIVE_OBJECT_TIMELINE;
    uint32_t tick;int error=gool_native_registry_read(o->factory->registry,0x80068eecu,&tick);
    if(error)return o->status=error;
    if(tick!=(uint32_t)o->tick)return o->status=GOOL_NATIVE_OBJECT_TIMELINE;
    error=gool_forest_step(&o->forest,budget);
    if(error<0)return o->status=error;
    if(error==GOOL_TREE_DONE){o->active=0;return o->status=NATIVE_RUNTIME_DONE;}
    return NATIVE_RUNTIME_PENDING;
}
int native_runtime_close(native_runtime *o) {
    if(!o)return NATIVE_RUNTIME_INVALID;
    if(o->forest.deletion_pending||o->forest.destroy.active)return NATIVE_RUNTIME_BUSY;
    int error=gool_native_object_close(&o->object);if(error)return error;
    *o=(native_runtime){0};return 0;
}

int native_frame_init(native_frame *o) {
    if(!o||o->initialized)return NATIVE_RUNTIME_INVALID;
    int error=model_draw_init(&o->draw);
    /* Partial GPU initialization still requires explicit close. */
    o->initialized=o->draw.initialized;
    return error;
}
int native_frame_stream_begin(native_frame *o,model_actor *actor,const model_stream_controls *controls) {
    if(!o||!o->initialized||o->active)return NATIVE_RUNTIME_INVALID;
    return model_stream_begin(&o->stream,actor,controls);
}
int native_frame_stream_step(native_frame *o,uint32_t budget) {
    if(!o||!o->initialized||o->active)return NATIVE_RUNTIME_INVALID;
    return model_stream_step(&o->stream,budget);
}
int native_frame_stream_close(native_frame *o) {
    return o?model_stream_close(&o->stream):NATIVE_RUNTIME_INVALID;
}
int native_frame_begin(native_frame *o,C3D_RenderTarget *target,uint32_t clear_color,
    gool_domain *domain,const model_packet_span spans[2],uint32_t first,
    const model_present_controls *controls,void (*finish)(void *),void *finish_context) {
    if(!o||!o->initialized||o->active||o->present.opened||o->stream.opened||!target||!controls)
        return NATIVE_RUNTIME_INVALID;
    int error=model_draw_begin(&o->draw);if(error)return error;
    model_present_controls real=*controls;real.draw=&o->draw;
    error=model_present_begin(&o->present,domain,spans,first,&real);
    /* Failed begin may own packet leases; close remains mandatory. */
    o->active=1;o->phase=0;o->complete=o->cleared=0;o->target=target;
    o->clear_color=clear_color;o->finish=finish;o->finish_context=finish_context;
    o->vertices=controls->prior_vertices;o->draw_mode=controls->draw_mode;
    return o->status=error;
}
int native_frame_step(native_frame *o,uint32_t budget,uint32_t pixel_budget) {
    if(!o||!o->initialized)return NATIVE_RUNTIME_INVALID;
    if(!o->active||o->status<0)return o->status;
    if(!budget)return NATIVE_RUNTIME_PENDING;
    int error;
    if(o->phase==0) {
        error=model_present_step(&o->present,budget,pixel_budget);
        o->vertices=o->present.frame_vertices;o->draw_mode=o->present.controls.draw_mode;
        if(error<0)return o->status=error;
        if(error==MODEL_PRESENT_PENDING)return NATIVE_RUNTIME_PENDING;
        if(error!=MODEL_PRESENT_DONE&&error!=MODEL_PRESENT_DRAIN)return o->status=NATIVE_RUNTIME_INVALID;
        o->complete=error==MODEL_PRESENT_DONE;o->phase=1;
        return NATIVE_RUNTIME_PENDING;
    }
    if(o->phase==1) {
        if(!C3D_FrameBegin(0))return NATIVE_RUNTIME_PENDING;
        if(!C3D_FrameDrawOn(o->target)) {
            C3D_FrameEnd(0);return o->status=NATIVE_RUNTIME_INVALID;
        }
        if(!o->cleared) {
            C3D_RenderTargetClear(o->target,C3D_CLEAR_ALL,o->clear_color,0);
            o->cleared=1;
        }
        error=model_draw_submit(&o->draw,budget);
        if(!error&&o->complete&&o->finish)o->finish(o->finish_context);
        C3D_FrameEnd(0);
        if(error<0)return o->status=error;
        if(!error)o->phase=2;
        return NATIVE_RUNTIME_PENDING;
    }
    if(o->phase==2) {
        error=model_draw_retire(&o->draw);
        if(error==MODEL_DRAW_BUSY)return NATIVE_RUNTIME_PENDING;
        if(error)return o->status=error;
        if(o->complete) {
            error=model_present_close(&o->present);if(error)return o->status=error;
            o->active=0;return o->status=NATIVE_RUNTIME_DONE;
        }
        error=model_draw_begin(&o->draw);if(error)return o->status=error;
        o->phase=0;return NATIVE_RUNTIME_PENDING;
    }
    return o->status=NATIVE_RUNTIME_INVALID;
}
int native_frame_close(native_frame *o) {
    if(!o)return NATIVE_RUNTIME_INVALID;
    int error=model_stream_close(&o->stream);if(error)return error;
    error=model_present_close(&o->present);if(error)return error;
    if(o->draw.initialized){error=model_draw_close(&o->draw);if(error)return error;}
    *o=(native_frame){0};return 0;
}
