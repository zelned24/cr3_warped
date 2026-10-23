#include "model_actor.h"

static int fail(model_actor *o,int error){o->status=error;return error;}
static int read_word(gool_native_actor *a,uint32_t address,uint32_t *word) {
    return gool_domain_read32(a->registry->domain,address,word);
}
static int stable(const model_actor *o) {
    gool_native_actor *a=o->actor;
    if(!a||!a->attached||a->enabled||a->executing||a->error||!a->call_references||
       !a->tick_source||*a->tick_source!=o->tick||a->registry->models!=o->bank||
       !o->bank->open||o->bank->closing||!a->frame_retained||!a->geometry_retained||
       a->frame_lease.slot!=o->frame_identity.slot||a->frame_lease.generation!=o->frame_identity.generation||
       a->geometry_lease.slot!=o->geometry_identity.slot||a->geometry_lease.generation!=o->geometry_identity.generation)
        return MODEL_ACTOR_CHANGED;
    uint32_t value;int error=read_word(a,0x80068eecu,&value);if(error)return error;
    if(value!=(uint32_t)o->tick)return MODEL_ACTOR_CHANGED;
    const uint32_t offsets[4]={8u,0xe8u,0xecu,0x120u};
    const uint32_t expected[4]={o->auxiliary,o->descriptor,o->frame_word,o->render_word};
    for(uint32_t i=0;i<4;i++) {
        error=read_word(a,a->node+offsets[i],&value);if(error)return error;
        if(value!=expected[i])return MODEL_ACTOR_CHANGED;
    }
    for(uint32_t i=0;i<11;i++) {
        error=read_word(a,o->auxiliary+i*4u,&value);if(error)return error;
        if(value!=o->auxiliary_words[i])return MODEL_ACTOR_CHANGED;
    }
    for(uint32_t i=0;i<3;i++) {
        error=read_word(a,a->node+0x60u+i*4u,&value);if(error)return error;
        if(value!=o->position[i])return MODEL_ACTOR_CHANGED;
    }
    return 0;
}
static int buffers_ok(model_actor *o,const model_actor_buffers *b,int paired) {
    if(!b->first||!b->pose||!b->triangles||(paired&&!b->second)||!b->vertex_capacity||
       !b->triangle_capacity||b->vertex_capacity>UINT32_MAX/sizeof(*b->pose)||
       b->triangle_capacity>UINT32_MAX/sizeof(*b->triangles)||
       ((uintptr_t)b->pose&3u)||((uintptr_t)b->triangles&3u))return 0;
    const uintptr_t addresses[5]={(uintptr_t)o,(uintptr_t)b->first,paired?(uintptr_t)b->second:0,
        (uintptr_t)b->pose,(uintptr_t)b->triangles};
    const uintptr_t sizes[5]={sizeof(*o),(uintptr_t)b->vertex_capacity*sizeof(*b->first),
        paired?(uintptr_t)b->vertex_capacity*sizeof(*b->second):0,
        (uintptr_t)b->vertex_capacity*sizeof(*b->pose),(uintptr_t)b->triangle_capacity*sizeof(*b->triangles)};
    for(uint32_t i=0;i<5;i++) {
        if(addresses[i]>UINTPTR_MAX-sizes[i])return 0;
        for(uint32_t j=0;j<i;j++)if(sizes[i]&&sizes[j]&&
            addresses[i]<addresses[j]+sizes[j]&&addresses[j]<addresses[i]+sizes[i])return 0;
    }
    return 1;
}
int model_actor_begin(model_actor *o,gool_native_actor *a,const model_actor_buffers *buffers) {
    if(!o||o->opened||o->retained||!a||!a->attached||a->enabled||a->executing||a->error||
       !a->tick_source||!a->registry->models||!a->registry->models->open||a->registry->models->closing||
       !a->frame_retained||!a->geometry_retained||!buffers||
       a->call_references==UINT32_MAX)return MODEL_ACTOR_INVALID;
    model_actor_buffers copy=*buffers;model_actor snapshot={.actor=a,.bank=a->registry->models};
    int error=read_word(a,a->node+8u,&snapshot.auxiliary);if(error)return error;
    if(!snapshot.auxiliary||(snapshot.auxiliary&3u)||snapshot.auxiliary>UINT32_MAX-0x28u)
        return MODEL_ACTOR_INVALID;
    error=read_word(a,a->node+0xe8u,&snapshot.descriptor);if(error)return error;
    uint8_t kind;error=gool_domain_read8(a->registry->domain,snapshot.descriptor,&kind);if(error)return error;
    if(kind!=1)return MODEL_ACTOR_INVALID;
    error=read_word(a,a->node+0xecu,&snapshot.frame_word);if(error)return error;
    error=read_word(a,a->node+0x120u,&snapshot.render_word);if(error)return error;
    for(uint32_t i=0;i<11;i++) {
        error=read_word(a,snapshot.auxiliary+i*4u,&snapshot.auxiliary_words[i]);if(error)return error;
    }
    for(uint32_t i=0;i<3;i++) {
        error=read_word(a,a->node+0x60u+i*4u,&snapshot.position[i]);if(error)return error;
    }
    if(snapshot.auxiliary_words[8]!=a->frame_address||snapshot.auxiliary_words[9]!=a->frame_next_address||
       snapshot.auxiliary_words[10]!=a->geometry_address)return MODEL_ACTOR_CHANGED;
    snapshot.paired=snapshot.auxiliary_words[8]!=snapshot.auxiliary_words[9];
    if(!buffers_ok(o,&copy,snapshot.paired))return MODEL_ACTOR_INVALID;
    uint32_t tick;error=read_word(a,0x80068eecu,&tick);if(error)return error;
    if(tick!=(uint32_t)*a->tick_source)return MODEL_ACTOR_CHANGED;
    snapshot.tick=*a->tick_source;snapshot.frame_identity=a->frame_lease;snapshot.geometry_identity=a->geometry_lease;
    snapshot.buffers=copy;snapshot.opened=snapshot.active=snapshot.retained=1;snapshot.status=MODEL_ACTOR_PENDING;
    *o=snapshot;a->call_references++;return o->status;
}
int model_actor_step(model_actor *o,uint32_t budget) {
    if(!o||!o->opened)return MODEL_ACTOR_INVALID;
    if(!o->active||o->status<0||!budget)return o->status;
    int error=stable(o);if(error)return fail(o,error);
    switch(o->phase) {
        case 0:
            error=model_vertices_begin_actor(&o->frames[0],o->actor,0,o->buffers.first,o->buffers.vertex_capacity);
            if(error!=MODEL_VERTICES_PENDING)return fail(o,error<0?error:MODEL_ACTOR_INVALID);
            o->phase=1;break;
        case 1:
            error=model_vertices_step(&o->frames[0],budget);if(error<0)return fail(o,error);
            if(!error)o->phase=o->paired?2:4;
            break;
        case 2:
            error=model_vertices_begin_actor(&o->frames[1],o->actor,1,o->buffers.second,o->buffers.vertex_capacity);
            if(error!=MODEL_VERTICES_PENDING)return fail(o,error<0?error:MODEL_ACTOR_INVALID);
            o->phase=3;break;
        case 3:
            error=model_vertices_step(&o->frames[1],budget);if(error<0)return fail(o,error);
            if(!error)o->phase=4;
            break;
        case 4:
            error=model_triangles_begin(&o->topology,&o->frames[0],o->buffers.triangles,o->buffers.triangle_capacity);
            if(error!=MODEL_TRIANGLES_PENDING)return fail(o,error<0?error:MODEL_ACTOR_INVALID);
            o->phase=5;break;
        case 5:
            error=model_triangles_step(&o->topology,budget);if(error<0)return fail(o,error);
            if(!error)o->phase=6;
            break;
        case 6:
            if(o->paired)error=model_pose_pair_begin(&o->pose,&o->frames[0],&o->frames[1],o->buffers.pose,o->buffers.vertex_capacity);
            else error=model_pose_single_begin(&o->pose,&o->frames[0],o->buffers.pose,o->buffers.vertex_capacity);
            if(error!=MODEL_POSE_PENDING)return fail(o,error<0?error:MODEL_ACTOR_INVALID);
            o->phase=7;break;
        case 7:
            error=model_pose_step(&o->pose,budget);if(error<0)return fail(o,error);
            if(!error){o->active=0;o->status=0;return 0;}
            break;
        default:return fail(o,MODEL_ACTOR_INVALID);
    }
    return MODEL_ACTOR_PENDING;
}
int model_actor_close(model_actor *o) {
    if(!o)return MODEL_ACTOR_INVALID;
    if(o->consumers)return GOOL_NATIVE_ACTOR_BUSY;
    int error=model_pose_close(&o->pose);if(error)return error;
    error=model_triangles_close(&o->topology);if(error)return error;
    for(uint32_t i=2;i>0;i--) {
        error=model_vertices_close(&o->frames[i-1u]);if(error)return error;
    }
    if(o->retained) {
        if(!o->actor||!o->actor->attached||o->actor->enabled||o->actor->executing||!o->actor->call_references)
            return GOOL_NATIVE_ACTOR_BUSY;
        o->actor->call_references--;o->retained=0;
    }
    *o=(model_actor){0};return 0;
}
int model_actor_render_prepare(const model_actor *o,uint32_t function,
    const gool_native_render_input *engine,gool_native_render_output *output) {
    if(!o||!o->opened||o->active||o->status!=MODEL_ACTOR_DONE||!o->retained||!engine||!output||
       !(engine->packet_flags&0x40000000u)||
       ((engine->packet_flags&0x20000000u)!=0)!=(o->paired!=0))return MODEL_ACTOR_INVALID;
    gool_native_render_input input=*engine;
    for(uint32_t i=0;i<5;i++)input.auxiliary_rotation[i]=o->auxiliary_words[i];
    for(uint32_t i=0;i<3;i++)input.translation.geometry_scale[i]=o->frames[0].scale[i];
    for(uint32_t i=0;i<2;i++) {
        input.translation.frame_offset[i]=o->frames[0].view_offset_words[i];
        if(o->paired)input.translation.second_offset[i]=o->frames[1].view_offset_words[i];
    }
    return gool_native_render_prepare(function,&input,output);
}
int model_actor_render_capture(const model_actor *o,uint32_t gp,uint32_t scratch,uint32_t packet,
    const uint32_t prior_light[5],uint32_t projection_distance,
    gool_native_render_input *captured,gool_native_render_output *output) {
    if(!o||!o->opened||o->active||o->status!=MODEL_ACTOR_DONE||!o->retained||!captured||!output||
       !packet||(packet&3u)||packet>UINT32_MAX-0x6cu)return MODEL_ACTOR_INVALID;
    int error=stable(o);if(error)return error;
    uint32_t header,bytes;
    error=model_resources_item(o->bank,o->topology.lease,0,&header,&bytes);if(error)return error;
    if(bytes<0x4cu)return MODEL_ACTOR_INVALID;
    const uint32_t offsets[5]={0x10u,0x14u,0x30u,0x6cu,0x34u};
    const uint32_t expected[5]={o->actor->node,o->auxiliary,o->auxiliary_words[8],header,o->auxiliary_words[9]};
    for(uint32_t i=0;i<(o->paired?5u:4u);i++) {
        uint32_t value;error=gool_domain_read32(o->bank->domain,packet+offsets[i],&value);if(error)return error;
        if(value!=expected[i])return MODEL_ACTOR_CHANGED;
    }
    uint32_t function;gool_native_render_input input;gool_native_render_output result;
    error=gool_native_render_capture(o->bank->domain,gp,scratch,packet,prior_light,projection_distance,
        &function,&input);if(error)return error;
    error=model_actor_render_prepare(o,function,&input,&result);if(error)return error;
    *captured=input;*output=result;return 0;
}
