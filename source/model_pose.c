#include "model_pose.h"

static uint32_t sra1(uint32_t value){return (value>>1)|(value&0x80000000u);}
static int32_t signed16(uint32_t value) {
    value&=0xffffu;return value<=INT16_MAX?(int32_t)value:-1-(int32_t)(0xffffu-value);
}
static int overlap(uintptr_t a,uintptr_t size,uintptr_t b,uintptr_t other) {
    return a<b+other&&b<a+size;
}
static int ready(const model_vertices *v) {
    return v&&v->opened&&!v->active&&v->status==MODEL_VERTICES_DONE&&v->output&&
        v->decoded==v->count&&v->capacity>=v->count&&v->geometry_retained;
}
static int begin(model_pose *o,const model_vertices *a,const model_vertices *b,
    model_pose_vertex *output,uint32_t capacity,int paired) {
    if(!o||o->opened||o->active||!ready(a)||!ready(b)||!output||a->bank!=b->bank||
       a->geometry!=b->geometry||a->geometry_lease.slot!=b->geometry_lease.slot||
       a->geometry_lease.generation!=b->geometry_lease.generation||a->count!=b->count||
       a->count>capacity||capacity>UINT32_MAX/sizeof(*output)||((uintptr_t)output&3u)||
       a->count>UINT32_MAX/sizeof(*a->output))return MODEL_POSE_INVALID;
    uintptr_t dst=(uintptr_t)output,bytes=(uintptr_t)capacity*sizeof(*output),owner=(uintptr_t)o;
    uintptr_t first=(uintptr_t)a->output,second=(uintptr_t)b->output;
    uintptr_t input_bytes=(uintptr_t)a->count*sizeof(*a->output),va=(uintptr_t)a,vb=(uintptr_t)b;
    if(dst>UINTPTR_MAX-bytes||owner>UINTPTR_MAX-sizeof(*o)||first>UINTPTR_MAX-input_bytes||
       second>UINTPTR_MAX-input_bytes||va>UINTPTR_MAX-sizeof(*a)||vb>UINTPTR_MAX-sizeof(*b)||
       overlap(dst,bytes,owner,sizeof(*o))||overlap(dst,bytes,first,input_bytes)||
       overlap(dst,bytes,second,input_bytes)||overlap(dst,bytes,va,sizeof(*a))||
       overlap(dst,bytes,vb,sizeof(*b))||overlap(owner,sizeof(*o),first,input_bytes)||
       overlap(owner,sizeof(*o),second,input_bytes)||overlap(owner,sizeof(*o),va,sizeof(*a))||
       overlap(owner,sizeof(*o),vb,sizeof(*b)))return MODEL_POSE_INVALID;
    uint32_t x=(uint32_t)(int32_t)a->origin[0],y=(uint32_t)(int32_t)a->origin[1];
    if(paired) {
        x=sra1(x+(uint32_t)(int32_t)b->origin[0]);
        y=sra1(y+(uint32_t)(int32_t)b->origin[1]);
    }
    *o=(model_pose){.first=a->output,.second=paired?b->output:NULL,.output=output,.count=a->count,
        .origin_xy=(x&0xffffu)|(y<<16),.origin_z=paired?sra1(a->origin_z_word+b->origin_z_word):a->origin_z_word,
        .bank=a->bank,.geometry=a->geometry,.geometry_identity=a->geometry_lease,.paired=paired,
        .opened=1,.active=1,.status=MODEL_POSE_PENDING};
    return o->status;
}
int model_pose_pair_begin(model_pose *o,const model_vertices *a,const model_vertices *b,
    model_pose_vertex *output,uint32_t capacity) {
    return begin(o,a,b,output,capacity,1);
}
int model_pose_single_begin(model_pose *o,const model_vertices *frame,
    model_pose_vertex *output,uint32_t capacity) {
    return begin(o,frame,frame,output,capacity,0);
}
int model_pose_step(model_pose *o,uint32_t budget) {
    if(!o||!o->opened)return MODEL_POSE_INVALID;
    if(!o->active||o->status<0||!budget)return o->status;
    while(budget--&&o->decoded<o->count) {
        model_vertex_bytes a=o->first[o->decoded];uint32_t xy,z;
        if(o->paired) {
            model_vertex_bytes b=o->second[o->decoded];
            xy=((uint32_t)a.x|((uint32_t)a.y<<16))+((uint32_t)b.x|((uint32_t)b.y<<16));
            xy=(xy<<4)+o->origin_xy;z=(((uint32_t)a.z+b.z)<<4)+o->origin_z;
        } else {
            xy=((((uint32_t)a.x|((uint32_t)a.y<<16))<<5)&0x1fe01fe0u)+o->origin_xy;
            z=((uint32_t)a.z<<5)+o->origin_z;
        }
        xy&=0xfffeffffu;
        o->output[o->decoded++]=(model_pose_vertex){xy,z};
    }
    if(o->decoded==o->count){o->active=0;o->status=MODEL_POSE_DONE;}
    return o->status;
}
int model_pose_components(const model_pose_vertex *vertex,int32_t output[3]) {
    if(!vertex||!output)return MODEL_POSE_INVALID;
    model_pose_vertex v=*vertex;
    output[0]=signed16(v.xy);output[1]=signed16(v.xy>>16);output[2]=signed16(v.z);return 0;
}
int model_pose_close(model_pose *o) {
    if(!o)return MODEL_POSE_INVALID;
    *o=(model_pose){0};return 0;
}
