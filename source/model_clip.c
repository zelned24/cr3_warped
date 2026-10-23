#include "model_clip.h"

static int equal(const model_clip_vertex *a,const model_clip_vertex *b) {
    for(uint32_t i=0;i<3;i++)if(a->camera[i]!=b->camera[i])return 0;
    for(uint32_t i=0;i<5;i++)if(a->attributes[i]!=b->attributes[i])return 0;
    return 1;
}
static int32_t interpolate(int32_t inside,int32_t outside,uint64_t distance,uint64_t span) {
    int64_t delta=(int64_t)outside-inside;
    uint64_t magnitude=(uint64_t)(delta<0?-delta:delta);
    /* Both factors are at most2^32-1; their product fits unsigned64. The
     * quotient never exceeds |delta|, so the result stays between endpoints. */
    uint64_t step=magnitude*distance/span;
    return (int32_t)((int64_t)inside+(delta<0?-(int64_t)step:(int64_t)step));
}
static model_clip_vertex intersection(const model_clip_vertex *inside,
    const model_clip_vertex *outside,int32_t near) {
    uint64_t distance=(uint64_t)((int64_t)inside->camera[2]-near);
    uint64_t span=(uint64_t)((int64_t)inside->camera[2]-outside->camera[2]);
    model_clip_vertex result;
    for(uint32_t i=0;i<2;i++)
        result.camera[i]=interpolate(inside->camera[i],outside->camera[i],distance,span);
    result.camera[2]=near;
    for(uint32_t i=0;i<5;i++)
        result.attributes[i]=interpolate(inside->attributes[i],outside->attributes[i],distance,span);
    return result;
}
static int append(model_clip_polygon *p,model_clip_vertex vertex) {
    if(p->count&&equal(&p->vertices[p->count-1u],&vertex))return 0;
    if(p->count>=4u)return MODEL_CLIP_INVALID;
    p->vertices[p->count++]=vertex;return 0;
}
int model_clip_near(const model_clip_vertex input[3],int32_t near,model_clip_polygon *output) {
    if(!input||!output||near<=0)return MODEL_CLIP_INVALID;
    model_clip_polygon next={0};
    for(uint32_t i=0;i<3;i++) {
        const model_clip_vertex *previous=&input[i?i-1u:2u],*current=&input[i];
        int a=previous->camera[2]>=near,b=current->camera[2]>=near,error;
        if(a!=b) {
            model_clip_vertex edge=a?intersection(previous,current,near):intersection(current,previous,near);
            error=append(&next,edge);if(error)return error;
        }
        if(b){error=append(&next,*current);if(error)return error;}
    }
    if(next.count>1u&&equal(&next.vertices[0],&next.vertices[next.count-1u]))next.count--;
    *output=next;return 0;
}
