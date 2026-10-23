#include "gool_native_router.h"
#include "gool_tree.h"
static int spawn_read(void *context,uint32_t address,uint32_t *value) {
    gool_native_route_frame *f=context;
    return gool_native_registry_read(f->factory->registry,address,value);
}
static int spawn_write(void *context,uint32_t address,uint32_t value) {
    gool_native_route_frame *f=context;
    return gool_native_registry_write(f->factory->registry,address,value);
}
static int spawn_tree_read(void *context,uint32_t node,uint32_t offset,uint32_t *value)
{return spawn_read(context,node+offset,value);}
static int spawn_tree_write(void *context,uint32_t node,uint32_t offset,uint32_t value)
{return spawn_write(context,node+offset,value);}
static int spawn_geometry(void *context,uint32_t database,uint32_t *value) {
    gool_native_route_frame *f=context;const gool_create_services *s=f->factory->engine.creation;
    return s->scalar(s->context,database,0x47u,value);
}
static int spawn_load(void *context,uint32_t address,uint32_t *value) {
    gool_native_route_frame *f=context;const gool_create_services *s=f->factory->engine.creation;
    return s->load(s->context,address,value);
}
static int spawn_enter(void *context,uint32_t node,uint32_t state,uint32_t argc,uint32_t arguments,uint32_t *value) {
    gool_native_route_frame *f=context;
    return gool_native_state_enter(&f->state,node,state,argc,arguments,value);
}
/* Original376F8/1A42C: each router frame owns its child initializer/state,
 * allowing child RESUME handlers to create grandchildren without replay. */
static int spawn_step(gool_native_router *r,gool_native_route_frame *f) {
    const gool_create_services *services=r->factory->engine.creation;
    if(!services)return GOOL_VM_UNSUPPORTED;
    uint32_t parent=f->outer->actor->node,instruction=f->outer->actor->vm->instruction;
    int e;
    if(!f->spawn_phase) {
        f->factory=r->factory;f->spawn_argc=(instruction>>20)&15u;
        f->spawn_drop=f->spawn_argc;f->spawn_count=instruction&63u;
        uint32_t sp;e=spawn_read(f,parent+0xbcu,&sp);if(e)return e;
        if(!f->spawn_count) {
            f->spawn_drop++;uint32_t count;
            e=spawn_read(f,sp-f->spawn_drop*4u,&count);if(e)return e;
            if(count&0x80000000u)count=0;
            f->spawn_count=count;
        }
        if(f->spawn_drop>f->outer->actor->vm->stack.top)return GOOL_VM_STACK;
        f->spawn_arguments=sp-f->spawn_argc*4u;
        f->spawn_phase=1;
    }
    if(f->spawn_phase==1) {
        if(f->spawn_index>=f->spawn_count) {
            uint32_t sp;e=spawn_read(f,parent+0xbcu,&sp);if(e)return e;
            e=spawn_write(f,parent+0xbcu,sp-f->spawn_drop*4u);if(e)return e;
            e=gool_native_dispatch_resume(f->outer,0,0);
            return e==GOOL_NATIVE_DISPATCH_BUDGET?0:e;
        }
        uint32_t class_index=(instruction>>12)&255u,object_id=(instruction>>6)&63u;
        if(!class_index&&!object_id) {
            e=spawn_read(f,0x80068e98u,&f->spawn_node);if(e)return e;
            e=spawn_write(f,0x80060a90u,f->spawn_node);if(e)return e;
        } else {
            uint32_t type;e=spawn_read(f,0x80068ee0u,&type);if(e)return e;
            e=spawn_read(f,0x80068ee0u+(type==2u?4u:0x4cu),&f->spawn_node);if(e)return e;
        }
        if(!f->spawn_node) {
            if((instruction>>24)==0x46u)return GOOL_VM_UNSUPPORTED;
            e=spawn_write(f,parent+0xd4u,0);if(e)return e;
            f->spawn_index++;return 1;
        }
        gool_tree_services tree={.context=f,.read=spawn_tree_read,.write=spawn_tree_write};
        e=gool_tree_reparent(f->spawn_node,parent,&tree,services->scan_limit);
        if(e!=2&&e!=-24)return e<0?e:GOOL_NATIVE_ROUTER_INVALID;
        e=spawn_write(f,f->spawn_node+4u,3);if(e)return e;
        gool_initialize_request request={.node=f->spawn_node,.class_index=class_index,
            .object_id=object_id,.argument_count=f->spawn_argc,.arguments=f->spawn_arguments,.placed=0};
        gool_initialize_services initialize={f,spawn_read,spawn_write,spawn_geometry,spawn_load,spawn_enter};
        gool_native_state_engine engine={r->factory,gool_native_factory_actor};
        e=gool_native_state_bind(&f->state,r->factory->registry,r->factory->pool,&engine,128);if(e)return e;
        e=gool_initialize_begin(&f->spawn_initialize,&request,&initialize);
        if(e!=GOOL_INITIALIZE_BUDGET)return e?e:GOOL_NATIVE_ROUTER_INVALID;
        f->spawn_phase=2;
    }
    e=gool_initialize_step(&f->spawn_initialize,1);if(e)return e<0?e:1;
    for(unsigned i=0;i<0x20u;i+=4u){e=spawn_write(f,f->spawn_node+0x20u+i,0);if(e)return e;}
    uint32_t child=(uint32_t)f->spawn_initialize.native_result;
    if((child^0x80000000u)<(0xffffff02u^0x80000000u)) {
        child=f->spawn_node;e=spawn_write(f,child+0x50u,parent);if(e)return e;
    } else child=0;
    e=spawn_write(f,parent+0xd4u,child);if(e)return e;
    e=gool_native_state_close(&f->state);if(e)return e;
    f->spawn_index++;f->spawn_phase=1;return 1;
}
static int fail(gool_native_router *r,int error){r->status=error;return error;}
static int cleanup(gool_native_route_frame *frame,gool_native_factory *factory,int cancel) {
    if(cancel&&frame->kind==GOOL_NATIVE_DISPATCH_RESOURCE&&factory->engine.resource) {
        int e=factory->engine.resource(factory->engine.resource_context,
            frame->outer->actor->node,frame->outer->actor->vm->instruction,1);
        if(e)return e;
    }
    int geometry_error=gool_native_geometry_search_close(&frame->geometry);if(geometry_error)return geometry_error;
    int error=gool_native_state_close(&frame->state);if(error)return error;
    if(cancel&&frame->request&&frame->request->mapped&&
       frame->request->generation==frame->request_generation) {
        if(!frame->outer||!frame->outer->actor||
           gool_native_factory_request(factory,frame->outer->actor->node)!=frame->request)
            return GOOL_NATIVE_ACTOR_BUSY;
        error=gool_native_factory_cancel_request(factory,frame->outer->actor->node);if(error)return error;
    }
    error=gool_native_delivery_close(&frame->delivery);if(error)return error;
    *frame=(gool_native_route_frame){0};return 0;
}
static int push(gool_native_router *r,gool_native_dispatch *outer) {
    if(!outer||!outer->active||!outer->actor||outer->actor->registry!=r->factory->registry||
       (outer->status!=GOOL_NATIVE_DISPATCH_SEND&&outer->status!=GOOL_NATIVE_DISPATCH_STATE&&
        outer->status!=GOOL_NATIVE_DISPATCH_GEOMETRY&&outer->status!=GOOL_NATIVE_DISPATCH_SPAWN&&outer->status!=GOOL_NATIVE_DISPATCH_RESOURCE))
        return GOOL_NATIVE_ROUTER_INVALID;
    for(uint32_t i=0;i<r->depth;i++)if(r->frames[i].outer==outer)return GOOL_NATIVE_ROUTER_INVALID;
    if(r->depth==r->capacity) {
        /* This child already exists: rejecting another call frame must not
         * discard its request ownership. Close stops/cancels it before parents. */
        r->overflow=outer;
        if(outer->status==GOOL_NATIVE_DISPATCH_STATE) {
            r->overflow_request=gool_native_factory_request(r->factory,outer->actor->node);
            if(!r->overflow_request)return GOOL_NATIVE_ROUTER_INVALID;
            r->overflow_generation=r->overflow_request->generation;
        }
        return GOOL_NATIVE_ROUTER_LIMIT;
    }
    gool_native_route_frame *frame=&r->frames[r->depth];
    if(frame->occupied)return GOOL_NATIVE_ROUTER_INVALID;
    frame->occupied=1;frame->outer=outer;frame->kind=(uint32_t)outer->status;r->depth++;
    if(frame->kind==GOOL_NATIVE_DISPATCH_STATE) {
        frame->request=gool_native_factory_request(r->factory,outer->actor->node);
        if(!frame->request)return GOOL_NATIVE_ROUTER_INVALID;
        frame->request_generation=frame->request->generation;
    }
    if(frame->kind==GOOL_NATIVE_DISPATCH_SPAWN||frame->kind==GOOL_NATIVE_DISPATCH_RESOURCE)return 0;
    if(frame->kind==GOOL_NATIVE_DISPATCH_GEOMETRY) {
        int e=gool_native_geometry_search_begin(&frame->geometry,r->factory,outer,
                    r->arguments_base+(r->depth-1)*256u);
        return e==1?0:e<0?e:GOOL_NATIVE_ROUTER_INVALID;
    }
    gool_native_state_engine engine={.context=r->factory,.actor=gool_native_factory_actor};
    int error=gool_native_state_bind(&frame->state,r->factory->registry,r->factory->pool,&engine,1);
    if(error)return error;
    if(frame->kind==GOOL_NATIVE_DISPATCH_SEND) {
        uint32_t address=r->arguments_base+(r->depth-1)*256u;
        error=gool_native_delivery_bind(&frame->delivery,r->factory,&frame->state,address,1);if(error)return error;
        error=gool_native_delivery_begin(&frame->delivery,outer);
        if(error!=GOOL_SEND_NATIVE_BUDGET)return error<0?error:GOOL_NATIVE_ROUTER_INVALID;
    }
    return 0;
}
int gool_native_router_bind(gool_native_router *r,gool_native_factory *factory,
    gool_native_route_frame *frames,uint32_t capacity,uint32_t base) {
    if(!r||r->bound||!factory||!factory->open||!frames||!capacity||capacity>GOOL_NATIVE_ROUTER_DEPTH||
       !base||(base&255u)||(uint64_t)base+(uint64_t)capacity*256u>UINT32_MAX)
        return GOOL_NATIVE_ROUTER_INVALID;
    uint64_t factory_start=factory->request_base;
    uint64_t factory_end=factory_start+(uint64_t)GOOL_NATIVE_REQUEST_COUNT*256u;
    if((uint64_t)base<factory_end&&factory_start<(uint64_t)base+capacity*256u)return GOOL_NATIVE_ROUTER_INVALID;
    for(uint32_t i=0;i<capacity;i++)if(frames[i].occupied||frames[i].state.bound||frames[i].delivery.bound||frames[i].geometry.factory)
        return GOOL_NATIVE_ROUTER_INVALID;
    *r=(gool_native_router){.factory=factory,.frames=frames,.capacity=capacity,.arguments_base=base,.bound=1};return 0;
}
int gool_native_router_begin(gool_native_router *r,gool_native_dispatch *root) {
    if(!r||!r->bound||r->active||r->depth||r->overflow||r->root_retained||!root||!root->active||!root->actor||
       !root->actor->attached||root->actor->registry!=r->factory->registry||root->actor->call_references==UINT32_MAX||
       (root->status!=GOOL_NATIVE_DISPATCH_BUDGET&&root->status!=GOOL_NATIVE_DISPATCH_SEND&&
        root->status!=GOOL_NATIVE_DISPATCH_STATE&&root->status!=GOOL_NATIVE_DISPATCH_GEOMETRY&&root->status!=GOOL_NATIVE_DISPATCH_SPAWN&&root->status!=GOOL_NATIVE_DISPATCH_RESOURCE))return GOOL_NATIVE_ROUTER_INVALID;
    root->actor->call_references++;r->root_retained=1;r->root=root;r->active=1;r->status=GOOL_NATIVE_ROUTER_BUDGET;
    return r->status;
}
int gool_native_router_step(gool_native_router *r,uint32_t budget) {
    if(!r||!r->bound)return GOOL_NATIVE_ROUTER_INVALID;
    if(!r->active||r->status<0||!budget)return r->status;
    while(budget--) {
        int error;
        if(!r->depth) {
            error=gool_native_dispatch_step(r->root,1);
            if(error<0)return fail(r,error);
            if(!error) {
                if(!r->root->actor->attached||!r->root->actor->call_references)return fail(r,GOOL_NATIVE_ROUTER_INVALID);
                r->root->actor->call_references--;r->root_retained=0;
                r->native_result=r->root->native_result;r->active=0;r->status=0;return 0;
            }
            if(error==GOOL_NATIVE_DISPATCH_SEND||error==GOOL_NATIVE_DISPATCH_STATE||error==GOOL_NATIVE_DISPATCH_GEOMETRY||error==GOOL_NATIVE_DISPATCH_SPAWN||error==GOOL_NATIVE_DISPATCH_RESOURCE) {
                error=push(r,r->root);if(error)return fail(r,error);
            }
            continue;
        }
        gool_native_route_frame *frame=&r->frames[r->depth-1];
        gool_native_dispatch *pending;
        if(frame->kind==GOOL_NATIVE_DISPATCH_RESOURCE) {
            if(!r->factory->engine.resource)return fail(r,GOOL_VM_UNSUPPORTED);
            error=r->factory->engine.resource(r->factory->engine.resource_context,
                frame->outer->actor->node,frame->outer->actor->vm->instruction,0);
            pending=0;
            if(!error) {
                error=gool_native_dispatch_resume(frame->outer,0,0);
                if(error==GOOL_NATIVE_DISPATCH_BUDGET)error=0;
            }
        } else if(frame->kind==GOOL_NATIVE_DISPATCH_SPAWN) {
            error=spawn_step(r,frame);pending=gool_native_state_pending(&frame->state);
        } else if(frame->kind==GOOL_NATIVE_DISPATCH_GEOMETRY) {
            error=gool_native_geometry_search_step(&frame->geometry,1);
            pending=gool_native_geometry_search_pending(&frame->geometry);
        } else if(frame->kind==GOOL_NATIVE_DISPATCH_SEND) {
            error=gool_native_delivery_step(&frame->delivery,1);
            pending=gool_native_delivery_pending(&frame->delivery);
        } else {
            error=gool_native_state_apply(&frame->state,r->factory,frame->outer);
            pending=gool_native_state_pending(&frame->state);
        }
        if(error<0)return fail(r,error);
        if(!error) {
            error=cleanup(frame,r->factory,0);if(error)return fail(r,error);
            r->depth--;continue;
        }
        if(pending){error=push(r,pending);if(error)return fail(r,error);}
    }
    return GOOL_NATIVE_ROUTER_BUDGET;
}
int gool_native_router_engine_delivery_step(gool_native_router *r,gool_native_delivery *d,uint32_t budget) {
    if(!r||!r->bound||!d||!d->bound||!d->native_entry||d->factory!=r->factory||
       (r->engine_delivery&&r->engine_delivery!=d))return GOOL_NATIVE_ROUTER_INVALID;
    if(r->status<0)return r->status;
    if(!r->engine_delivery) {
        if(r->active||r->root_retained||r->depth||r->overflow)return GOOL_NATIVE_ROUTER_INVALID;
        r->engine_delivery=d;
    }
    while(budget--) {
        int error;
        if(r->active) {
            error=gool_native_router_step(r,1);if(error<0)return error;
            continue;
        }
        error=gool_native_delivery_step(d,1);
        if(error<0)return error;
        if(!error){r->engine_delivery=0;return GOOL_NATIVE_ROUTER_DONE;}
        gool_native_dispatch *pending=gool_native_delivery_pending(d);
        if(pending) {
            error=gool_native_router_begin(r,pending);
            if(error!=GOOL_NATIVE_ROUTER_BUDGET)return error<0?error:GOOL_NATIVE_ROUTER_INVALID;
        }
    }
    return GOOL_NATIVE_ROUTER_BUDGET;
}
int gool_native_router_close(gool_native_router *r) {
    if(!r)return GOOL_NATIVE_ROUTER_INVALID;
    if(!r->bound)return 0;
    if(r->overflow) {
        if(r->overflow->active) {
            int error=gool_native_dispatch_abort(r->overflow,GOOL_NATIVE_ROUTER_LIMIT);
            if(r->overflow->active)return error;
        }
        gool_native_request *request=r->overflow_request;
        if(request&&request->mapped&&request->generation==r->overflow_generation) {
            if(!r->overflow->actor||
               gool_native_factory_request(r->factory,r->overflow->actor->node)!=request)
                return GOOL_NATIVE_ACTOR_BUSY;
            int error=gool_native_factory_cancel_request(r->factory,r->overflow->actor->node);
            if(error)return error;
        }
        r->overflow=0;r->overflow_request=0;r->overflow_generation=0;
    }
    while(r->depth) {
        int error=cleanup(&r->frames[r->depth-1],r->factory,1);if(error)return error;
        r->depth--;
    }
    if(r->root_retained) {
        if(!r->root||!r->root->actor||!r->root->actor->attached||!r->root->actor->call_references)
            return GOOL_NATIVE_ROUTER_INVALID;
        r->root->actor->call_references--;r->root_retained=0;
    }
    *r=(gool_native_router){0};return 0;
}
