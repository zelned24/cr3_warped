#include "model_gpu_texture.h"

static uint32_t morton(uint32_t x,uint32_t y) {
    return (x&1u)|((y&1u)<<1)|((x&2u)<<1)|((y&2u)<<2)|((x&4u)<<2)|((y&4u)<<3);
}
static uint8_t expand(uint32_t value){return (uint8_t)((value<<3)|(value>>2));}
int model_gpu_texture_init(model_gpu_texture *o) {
    if(!o||o->initialized||o->references||o->active)return MODEL_GPU_TEXTURE_INVALID;
    C3D_Tex texture={0};
    if(!C3D_TexInit(&texture,MODEL_GPU_TEXTURE_WIDTH,MODEL_GPU_TEXTURE_HEIGHT,GPU_RGBA8))
        return MODEL_GPU_TEXTURE_MEMORY;
    C3D_TexSetFilter(&texture,GPU_NEAREST,GPU_NEAREST);
    C3D_TexSetWrap(&texture,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
    /* Preserve lifetime epoch through close/reinitialization; stale leases
     * cannot refer to a new upload in the same stable owner. */
    uint64_t generation=o->generation;
    *o=(model_gpu_texture){.texture=texture,.initialized=1,.generation=generation};return 0;
}
int model_gpu_texture_begin(model_gpu_texture *o,const model_texture *source) {
    if(!o||!o->initialized||!source||!source->opened||source->active||
       source->status!=MODEL_TEXTURE_DONE||!source->output||!source->width||!source->height||
       source->width>MODEL_GPU_TEXTURE_WIDTH||source->height>MODEL_GPU_TEXTURE_HEIGHT||
       source->count!=source->width*source->height||source->capacity<source->count||
       source->decoded!=source->count||o->generation==UINT64_MAX)return MODEL_GPU_TEXTURE_INVALID;
    if(o->active||o->references)return MODEL_GPU_TEXTURE_BUSY;
    uintptr_t input=(uintptr_t)source->output,size=(uintptr_t)source->count*2u;
    uintptr_t owner=(uintptr_t)o,storage=(uintptr_t)o->texture.data;
    if(!storage||input>UINTPTR_MAX-size||owner>UINTPTR_MAX-sizeof(*o)||
       storage>UINTPTR_MAX-MODEL_GPU_TEXTURE_BYTES||
       (input<owner+sizeof(*o)&&owner<input+size)||
       (input<storage+MODEL_GPU_TEXTURE_BYTES&&storage<input+size))return MODEL_GPU_TEXTURE_INVALID;
    o->material=source->material;o->source=source->output;o->width=source->width;o->height=source->height;
    o->cursor=o->transparent=o->opaque=o->stp=0;o->generation++;
    o->active=1;o->ready=0;o->status=MODEL_GPU_TEXTURE_PENDING;return o->status;
}
int model_gpu_texture_step(model_gpu_texture *o,uint32_t budget) {
    if(!o||!o->initialized)return MODEL_GPU_TEXTURE_INVALID;
    if(!o->active||o->status<0||!budget)return o->status;
    uint8_t *bytes=o->texture.data;
    while(budget--&&o->cursor<MODEL_GPU_TEXTURE_WIDTH*MODEL_GPU_TEXTURE_HEIGHT) {
        uint32_t x=o->cursor%MODEL_GPU_TEXTURE_WIDTH,y=o->cursor/MODEL_GPU_TEXTURE_WIDTH;
        uint32_t source_y=MODEL_GPU_TEXTURE_HEIGHT-1u-y;
        int actual=x<o->width&&source_y<o->height;
        uint32_t sx=x<o->width?x:o->width-1u,sy=source_y<o->height?source_y:o->height-1u;
        uint16_t raw=o->source[sy*o->width+sx];
        uint8_t alpha=!raw?0u:(raw&0x8000u)?128u:255u;
        if(actual){if(!alpha)o->transparent++;else if(alpha==128u)o->stp++;else o->opaque++;}
        uint32_t address=4u*(((y>>3)*(MODEL_GPU_TEXTURE_WIDTH>>3)+(x>>3))*64u+morton(x&7u,y&7u));
        /* GPU_RGBA8 is ABGR in little-endian texture memory. */
        bytes[address]=alpha;bytes[address+1u]=expand((raw>>10)&31u);
        bytes[address+2u]=expand((raw>>5)&31u);bytes[address+3u]=expand(raw&31u);
        o->cursor++;
    }
    if(o->cursor==MODEL_GPU_TEXTURE_WIDTH*MODEL_GPU_TEXTURE_HEIGHT) {
        C3D_TexFlush(&o->texture);o->source=NULL;o->active=0;o->ready=1;o->status=0;
    }
    return o->status;
}
int model_gpu_texture_acquire(model_gpu_texture *o,C3D_Tex **texture,model_gpu_texture_lease *lease) {
    if(!o||!o->initialized||!o->ready||o->active||!texture||!lease||lease->generation||
       o->references==UINT32_MAX)return MODEL_GPU_TEXTURE_INVALID;
    o->references++;*texture=&o->texture;*lease=(model_gpu_texture_lease){o->generation};return 0;
}
int model_gpu_texture_release(model_gpu_texture *o,model_gpu_texture_lease lease) {
    if(!o||!o->initialized||!o->ready||!o->references||!lease.generation||
       lease.generation!=o->generation)return MODEL_GPU_TEXTURE_INVALID;
    o->references--;return 0;
}
int model_gpu_texture_uv(const model_gpu_texture *o,uint8_t u,uint8_t v,float *s,float *t) {
    if(!o||!o->initialized||!o->ready||!o->references||!s||!t||s==t||
       u<o->material.min_u||u>o->material.max_u||v<o->material.min_v||v>o->material.max_v)
        return MODEL_GPU_TEXTURE_INVALID;
    float x=((float)(u-o->material.min_u)+0.5f)/MODEL_GPU_TEXTURE_WIDTH;
    float y=1.0f-((float)(v-o->material.min_v)+0.5f)/MODEL_GPU_TEXTURE_HEIGHT;
    *s=x;*t=y;return 0;
}
int model_gpu_texture_cancel(model_gpu_texture *o) {
    if(!o||!o->initialized)return MODEL_GPU_TEXTURE_INVALID;
    if(o->references)return MODEL_GPU_TEXTURE_BUSY;
    if(o->active){o->active=0;o->ready=0;o->source=NULL;o->status=0;}
    return 0;
}
int model_gpu_texture_close(model_gpu_texture *o) {
    if(!o)return MODEL_GPU_TEXTURE_INVALID;
    if(!o->initialized)return 0;
    if(o->references)return MODEL_GPU_TEXTURE_BUSY;
    C3D_TexDelete(&o->texture);
    uint64_t generation=o->generation;*o=(model_gpu_texture){.generation=generation};return 0;
}
