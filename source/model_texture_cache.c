#include "model_texture_cache.h"

enum {EMPTY=0,LOADING=1,READY=2,FAILED=3};
static int same(const model_material_uv *a,const model_material_uv *b) {
    return a->texture_eid==b->texture_eid&&a->clut==b->clut&&
        (a->texpage&~0x1c00u)==(b->texpage&~0x1c00u)&&
        a->min_u==b->min_u&&a->max_u==b->max_u&&a->min_v==b->min_v&&a->max_v==b->max_v;
}
static model_texture_cache_slot *lookup(model_texture_cache *o,model_texture_handle h) {
    if(!o||!o->opened||h.slot>=MODEL_TEXTURE_CACHE_SLOTS||!h.generation)return NULL;
    model_texture_cache_slot *s=&o->slots[h.slot];
    return s->state&&s->generation==h.generation?s:NULL;
}
static int fail(model_texture_cache_slot *s,int error){s->state=FAILED;s->error=error;return error;}
int model_texture_cache_init(model_texture_cache *o) {
    if(!o||o->opened)return MODEL_TEXTURE_CACHE_INVALID;
    o->opened=1;o->closing=0;o->error=0;o->active_slot=UINT32_MAX;
    for(uint32_t i=0;i<MODEL_TEXTURE_CACHE_SLOTS;i++) {
        int error=model_gpu_texture_init(&o->slots[i].gpu);
        if(error){o->error=error;return error;}
    }
    return 0;
}
int model_texture_cache_request(model_texture_cache *o,const model_material_uv *material,
    model_texture_handle *handle) {
    if(!o||!o->opened||o->closing||!material||!handle||o->error)
        return o&&o->error?o->error:MODEL_TEXTURE_CACHE_INVALID;
    uint32_t code=(material->texpage>>7)&3u;
    if(!(material->texture_eid&1u)||code==3u||material->depth!=(4u<<code)||
       material->min_u>material->max_u||material->min_v>material->max_v||material->max_v>=128u)
        return MODEL_TEXTURE_CACHE_INVALID;
    uint32_t empty=UINT32_MAX;
    for(uint32_t i=0;i<MODEL_TEXTURE_CACHE_SLOTS;i++) {
        model_texture_cache_slot *s=&o->slots[i];
        if(s->state&&same(&s->material,material)) {
            if(s->state==FAILED)return s->error;
            *handle=(model_texture_handle){s->generation,i};
            return s->state==READY?0:MODEL_TEXTURE_CACHE_PENDING;
        }
        if(!s->state&&empty==UINT32_MAX)empty=i;
    }
    if(o->active_slot!=UINT32_MAX)return MODEL_TEXTURE_CACHE_BUSY;
    if(empty==UINT32_MAX)return MODEL_TEXTURE_CACHE_FULL;
    if(o->generation==UINT64_MAX)return MODEL_TEXTURE_CACHE_INVALID;
    nsf_resource_ticket ticket;
    int error=nsf_resource_request(material->texture_eid,&ticket);if(error)return error;
    model_texture_cache_slot *s=&o->slots[empty];
    s->material=*material;s->ticket=ticket;s->has_ticket=1;s->generation=++o->generation;
    s->phase=0;s->state=LOADING;s->error=0;o->active_slot=empty;
    *handle=(model_texture_handle){s->generation,empty};return MODEL_TEXTURE_CACHE_PENDING;
}
int model_texture_cache_poll(model_texture_cache *o,uint32_t budget) {
    if(!o||!o->opened||o->closing)return MODEL_TEXTURE_CACHE_INVALID;
    if(o->error)return o->error;
    if(o->active_slot==UINT32_MAX)return 0;
    if(o->active_slot>=MODEL_TEXTURE_CACHE_SLOTS)return MODEL_TEXTURE_CACHE_INVALID;
    model_texture_cache_slot *s=&o->slots[o->active_slot];
    if(s->state==FAILED)return s->error;
    if(s->state!=LOADING)return MODEL_TEXTURE_CACHE_INVALID;
    if(!budget)return MODEL_TEXTURE_CACHE_PENDING;
    int error;
    switch(s->phase) {
        case 0: {
            nsf_stream_state state;int worker_error;
            error=nsf_resource_poll(s->ticket,&state,&worker_error);if(error)return fail(s,error);
            if(state==NSF_STREAM_QUEUED||state==NSF_STREAM_READING)return MODEL_TEXTURE_CACHE_PENDING;
            if(state!=NSF_STREAM_READY)return fail(s,worker_error?worker_error:MODEL_TEXTURE_CACHE_INVALID);
            error=model_texture_begin(&o->decoder,s->ticket,&s->material,o->pixels,MODEL_TEXTURE_CACHE_PIXELS);
            if(error!=MODEL_TEXTURE_PENDING)return fail(s,error<0?error:MODEL_TEXTURE_CACHE_INVALID);
            s->phase=1;break;
        }
        case 1:
            error=nsf_resource_release_ticket(s->ticket);if(error)return fail(s,error);
            s->has_ticket=0;s->phase=2;break;
        case 2:
            error=model_texture_step(&o->decoder,budget);if(error<0)return fail(s,error);
            if(!error)s->phase=3;
            break;
        case 3:
            error=model_gpu_texture_begin(&s->gpu,&o->decoder);
            if(error!=MODEL_GPU_TEXTURE_PENDING)return fail(s,error<0?error:MODEL_TEXTURE_CACHE_INVALID);
            s->phase=4;break;
        case 4:
            error=model_texture_close(&o->decoder);if(error)return fail(s,error);
            s->phase=5;break;
        case 5:
            error=model_gpu_texture_step(&s->gpu,budget);if(error<0)return fail(s,error);
            if(!error){s->state=READY;o->active_slot=UINT32_MAX;return 0;}
            break;
        default:return fail(s,MODEL_TEXTURE_CACHE_INVALID);
    }
    return MODEL_TEXTURE_CACHE_PENDING;
}
int model_texture_cache_acquire(model_texture_cache *o,model_texture_handle h,
    C3D_Tex **texture,model_texture_cache_lease *lease) {
    model_texture_cache_slot *s=lookup(o,h);
    if(!s||o->closing||!texture||!lease||lease->texture.generation||lease->handle.generation)
        return MODEL_TEXTURE_CACHE_INVALID;
    if(s->state==FAILED)return s->error;
    if(s->state!=READY)return MODEL_TEXTURE_CACHE_PENDING;
    model_gpu_texture_lease acquired={0};
    int error=model_gpu_texture_acquire(&s->gpu,texture,&acquired);if(error)return error;
    *lease=(model_texture_cache_lease){h,acquired};return 0;
}
int model_texture_cache_release(model_texture_cache *o,model_texture_cache_lease lease) {
    model_texture_cache_slot *s=lookup(o,lease.handle);if(!s)return MODEL_TEXTURE_CACHE_INVALID;
    return model_gpu_texture_release(&s->gpu,lease.texture);
}
int model_texture_cache_uv(const model_texture_cache *o,model_texture_cache_lease lease,
    uint8_t u,uint8_t v,float *x,float *y) {
    if(!o||!o->opened||lease.handle.slot>=MODEL_TEXTURE_CACHE_SLOTS)return MODEL_TEXTURE_CACHE_INVALID;
    const model_texture_cache_slot *s=&o->slots[lease.handle.slot];
    if(s->state!=READY||s->generation!=lease.handle.generation||
       !lease.texture.generation||s->gpu.generation!=lease.texture.generation)
        return MODEL_TEXTURE_CACHE_INVALID;
    return model_gpu_texture_uv(&s->gpu,u,v,x,y);
}
int model_texture_cache_evict(model_texture_cache *o,model_texture_handle h) {
    model_texture_cache_slot *s=lookup(o,h);if(!s)return MODEL_TEXTURE_CACHE_INVALID;
    if(s->gpu.references)return MODEL_TEXTURE_CACHE_BUSY;
    int error=model_gpu_texture_cancel(&s->gpu);if(error)return error;
    if(o->active_slot==h.slot) {
        error=model_texture_close(&o->decoder);if(error)return error;
    }
    if(s->has_ticket) {
        error=nsf_resource_cancel(s->ticket);if(error)return error;
        error=nsf_resource_release_ticket(s->ticket);if(error)return error;
        s->has_ticket=0;
    }
    if(o->active_slot==h.slot)o->active_slot=UINT32_MAX;
    s->state=EMPTY;s->error=0;s->phase=0;return 0;
}
int model_texture_cache_close(model_texture_cache *o) {
    if(!o)return MODEL_TEXTURE_CACHE_INVALID;
    if(!o->opened)return 0;
    o->closing=1;
    /* Preflight every submitted texture before any teardown. */
    for(uint32_t i=0;i<MODEL_TEXTURE_CACHE_SLOTS;i++)
        if(o->slots[i].gpu.references)return MODEL_TEXTURE_CACHE_BUSY;
    for(uint32_t i=0;i<MODEL_TEXTURE_CACHE_SLOTS;i++) {
        model_texture_cache_slot *s=&o->slots[i];
        if(s->state) {
            int error=model_texture_cache_evict(o,(model_texture_handle){s->generation,i});if(error)return error;
        }
        int error=model_gpu_texture_close(&s->gpu);if(error)return error;
    }
    int error=model_texture_close(&o->decoder);if(error)return error;
    o->opened=0;o->closing=0;o->error=0;o->active_slot=UINT32_MAX;return 0;
}
