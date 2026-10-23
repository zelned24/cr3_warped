#include "model_texture.h"

#define PAGE_BYTES 65536u
static int fail(model_texture *o,int error){o->status=error;return error;}
static int read_word(model_texture *o,uint32_t offset,uint16_t *value) {
    if(offset>PAGE_BYTES-2u)return MODEL_TEXTURE_FORMAT;
    *value=(uint16_t)((uint16_t)o->view.page[offset]|(uint16_t)o->view.page[offset+1u]<<8);
    return 0;
}
int model_texture_begin(model_texture *o,nsf_resource_ticket ticket,
    const model_material_uv *material,uint16_t *output,uint32_t capacity) {
    if(!o||o->opened||o->view.leased||!material||!output||!capacity||
       capacity>UINT32_MAX/sizeof(*output)||((uintptr_t)output&1u))return MODEL_TEXTURE_INVALID;
    model_material_uv copy=*material;
    uint32_t code=(copy.texpage>>7)&3u;
    if(code==3u||copy.depth!=(4u<<code)||copy.min_u>copy.max_u||copy.min_v>copy.max_v||
       copy.max_v>=128u||ticket.eid!=copy.texture_eid)return MODEL_TEXTURE_FORMAT;
    uint32_t width=1u+copy.max_u-copy.min_u,height=1u+copy.max_v-copy.min_v;
    if(capacity<width*height)return MODEL_TEXTURE_INVALID;
    uintptr_t dst=(uintptr_t)output,bytes=(uintptr_t)capacity*sizeof(*output),owner=(uintptr_t)o;
    if(dst>UINTPTR_MAX-bytes||owner>UINTPTR_MAX-sizeof(*o)||
       (dst<owner+sizeof(*o)&&owner<dst+bytes))return MODEL_TEXTURE_INVALID;
    *o=(model_texture){.material=copy,.output=output,.capacity=capacity,.width=width,.height=height,
        .count=width*height,.opened=1,.active=1,.status=MODEL_TEXTURE_PENDING};
    int error=nsf_resource_acquire(ticket,&o->view);if(error)return fail(o,error);
    if(o->view.page_type!=1u||o->view.eid!=copy.texture_eid||!o->view.page)
        return fail(o,MODEL_TEXTURE_FORMAT);
    uintptr_t page=(uintptr_t)o->view.page;
    if(page>UINTPTR_MAX-PAGE_BYTES||(dst<page+PAGE_BYTES&&page<dst+bytes)||
       (owner<page+PAGE_BYTES&&page<owner+sizeof(*o)))return fail(o,MODEL_TEXTURE_INVALID);
    return o->status;
}
int model_texture_step(model_texture *o,uint32_t budget) {
    if(!o||!o->opened)return MODEL_TEXTURE_INVALID;
    if(!o->active||o->status<0||!budget)return o->status;
    if(!o->view.leased||!o->view.page)return fail(o,MODEL_TEXTURE_INVALID);
    const model_material_uv *m=&o->material;
    uint32_t tex_x=(m->texpage&15u)*64u;
    while(budget--&&o->decoded<o->count) {
        uint32_t x=m->min_u+o->decoded%o->width,y=m->min_v+o->decoded/o->width;
        uint32_t offset=y*512u+tex_x*2u;uint16_t pixel;int error;
        if(m->depth==16u) {
            error=read_word(o,offset+x*2u,&pixel);if(error)return fail(o,error);
        } else {
            uint32_t index;
            if(m->depth==8u) {
                offset+=x;if(offset>=PAGE_BYTES)return fail(o,MODEL_TEXTURE_FORMAT);
                index=o->view.page[offset];
            } else {
                offset+=x>>1;if(offset>=PAGE_BYTES)return fail(o,MODEL_TEXTURE_FORMAT);
                index=(o->view.page[offset]>>((x&1u)*4u))&15u;
            }
            uint32_t clut_x=(m->clut&15u)*16u,clut_y=(m->clut&0x1fc0u)>>6;
            error=read_word(o,clut_y*512u+(clut_x+index)*2u,&pixel);if(error)return fail(o,error);
        }
        o->output[o->decoded++]=pixel;
    }
    if(o->decoded==o->count){o->active=0;o->status=MODEL_TEXTURE_DONE;}
    return o->status;
}
int model_texture_close(model_texture *o) {
    if(!o)return MODEL_TEXTURE_INVALID;
    if(o->view.leased) {
        int error=nsf_resource_release_view(&o->view);if(error)return error;
    }
    *o=(model_texture){0};return 0;
}
