#include "model_project.h"

static int32_t signed_word(uint32_t v){return v<=INT32_MAX?(int32_t)v:-1-(int32_t)(UINT32_MAX-v);}
static int32_t half(uint32_t v){v&=0xffffu;return v<=INT16_MAX?(int32_t)v:(int32_t)v-65536;}
static int64_t shift(int64_t value,uint32_t n) {
    int64_t divisor=INT64_C(1)<<n;
    return value<0?-1-((-1-value)/divisor):value/divisor;
}
static int64_t mac44(int64_t value,uint32_t axis,uint32_t *flags,int wrap) {
    const int64_t limit=INT64_C(1)<<43;
    if(value>=limit)*flags|=1u<<(30u-axis);
    if(value< -limit)*flags|=1u<<(27u-axis);
    if(wrap) {
        uint64_t bits=(uint64_t)value&UINT64_C(0xfffffffffff);
        value=(int64_t)bits-((bits&UINT64_C(0x80000000000))?(INT64_C(1)<<44):0);
    }
    return value;
}
static void mac0_flags(int64_t value,uint32_t *flags) {
    if(value>INT32_MAX)*flags|=1u<<16;
    if(value<INT32_MIN)*flags|=1u<<15;
}
static int32_t saturate(int64_t value,int32_t low,int32_t high,uint32_t bit,uint32_t *flags) {
    if(value<low){*flags|=1u<<bit;return low;}
    if(value>high){*flags|=1u<<bit;return high;}
    return (int32_t)value;
}
/* Seeds generated from the documented integer reciprocal formula, not a
 * rounded host division during vertex processing. Final index256 is valid. */
static const uint8_t unr_seed[257]={
    0xff,0xfd,0xfb,0xf9,0xf7,0xf5,0xf3,0xf1,0xef,0xee,0xec,0xea,0xe8,0xe6,0xe4,0xe3,
    0xe1,0xdf,0xdd,0xdc,0xda,0xd8,0xd6,0xd5,0xd3,0xd1,0xd0,0xce,0xcd,0xcb,0xc9,0xc8,
    0xc6,0xc5,0xc3,0xc1,0xc0,0xbe,0xbd,0xbb,0xba,0xb8,0xb7,0xb5,0xb4,0xb2,0xb1,0xb0,
    0xae,0xad,0xab,0xaa,0xa9,0xa7,0xa6,0xa4,0xa3,0xa2,0xa0,0x9f,0x9e,0x9c,0x9b,0x9a,
    0x99,0x97,0x96,0x95,0x94,0x92,0x91,0x90,0x8f,0x8d,0x8c,0x8b,0x8a,0x89,0x87,0x86,
    0x85,0x84,0x83,0x82,0x81,0x7f,0x7e,0x7d,0x7c,0x7b,0x7a,0x79,0x78,0x77,0x75,0x74,
    0x73,0x72,0x71,0x70,0x6f,0x6e,0x6d,0x6c,0x6b,0x6a,0x69,0x68,0x67,0x66,0x65,0x64,
    0x63,0x62,0x61,0x60,0x5f,0x5e,0x5d,0x5d,0x5c,0x5b,0x5a,0x59,0x58,0x57,0x56,0x55,
    0x54,0x53,0x53,0x52,0x51,0x50,0x4f,0x4e,0x4d,0x4d,0x4c,0x4b,0x4a,0x49,0x48,0x48,
    0x47,0x46,0x45,0x44,0x43,0x43,0x42,0x41,0x40,0x3f,0x3f,0x3e,0x3d,0x3c,0x3c,0x3b,
    0x3a,0x39,0x39,0x38,0x37,0x36,0x36,0x35,0x34,0x33,0x33,0x32,0x31,0x31,0x30,0x2f,
    0x2e,0x2e,0x2d,0x2c,0x2c,0x2b,0x2a,0x2a,0x29,0x28,0x28,0x27,0x26,0x26,0x25,0x24,
    0x24,0x23,0x22,0x22,0x21,0x20,0x20,0x1f,0x1e,0x1e,0x1d,0x1d,0x1c,0x1b,0x1b,0x1a,
    0x19,0x19,0x18,0x18,0x17,0x16,0x16,0x15,0x15,0x14,0x14,0x13,0x12,0x12,0x11,0x11,
    0x10,0x0f,0x0f,0x0e,0x0e,0x0d,0x0d,0x0c,0x0c,0x0b,0x0a,0x0a,0x09,0x09,0x08,0x08,
    0x07,0x07,0x06,0x06,0x05,0x05,0x04,0x04,0x03,0x03,0x02,0x02,0x01,0x01,0x00,0x00,
    0x00
};
static uint32_t reciprocal(uint16_t distance,uint16_t depth,uint32_t *flags) {
    if((uint32_t)distance>=(uint32_t)depth*2u){*flags|=1u<<17;return 0x1ffffu;}
    uint32_t d=depth,exponent=0;
    while(d<0x8000u){d<<=1;exponent++;}
    uint32_t numerator=(uint32_t)distance<<exponent;
    uint32_t seed=unr_seed[(d-0x7fc0u)>>7]+0x101u;
    uint32_t correction=(0x2000080u-d*seed)>>8;
    uint32_t inverse=(0x80u+correction*seed)>>8;
    uint64_t scaled=((uint64_t)numerator*inverse+0x8000u)>>16;
    return scaled>0x1ffffu?0x1ffffu:(uint32_t)scaled;
}
static void perspective(const model_project_controls *c,int cue_written,model_project_vertex *result) {
    model_project_vertex out=*result;
    for(uint32_t row=0;row<3;row++)
        out.ir[row]=(int16_t)saturate(out.camera[row],-32768,32767,24u-row,&out.flags);
    out.depth=(uint16_t)saturate(out.camera[2],0,65535,18,&out.flags);
    out.quotient=reciprocal(c->distance,out.depth,&out.flags);
    int64_t screen_x=(int64_t)out.quotient*out.ir[0]+signed_word(c->offset_x);
    int64_t screen_y=(int64_t)out.quotient*out.ir[1]+signed_word(c->offset_y);
    mac0_flags(screen_x,&out.flags);mac0_flags(screen_y,&out.flags);
    out.x=(int16_t)saturate(shift(screen_x,16),-1024,1023,14,&out.flags);
    out.y=(int16_t)saturate(shift(screen_y,16),-1024,1023,13,&out.flags);
    if(cue_written) {
        int64_t cue=(int64_t)out.quotient*c->depth_scale+signed_word(c->depth_bias);
        mac0_flags(cue,&out.flags);out.mac0=(uint32_t)cue;
        out.cue=(uint16_t)saturate(shift(cue,12),0,4096,12,&out.flags);out.cue_written=1;
    }
    if(out.flags&0x7f87e000u)out.flags|=0x80000000u;
    *result=out;
}
static int project(const model_project_controls *c,const model_pose_vertex *vertex,
    int cue_written,model_project_vertex *output) {
    if(!c||!vertex||!output)return MODEL_PROJECT_INVALID;
    int32_t v[3];int error=model_pose_components(vertex,v);if(error)return error;
    model_project_vertex out={0};
    for(uint32_t row=0;row<3;row++) {
        int64_t accumulator=(int64_t)signed_word(c->translation[row])*4096;
        for(uint32_t col=0;col<3;col++) {
            uint32_t index=row*3u+col;
            accumulator+=(int64_t)half(c->rotation[index/2u]>>((index&1u)*16u))*v[col];
            accumulator=mac44(accumulator,row,&out.flags,col<2u);
        }
        out.camera[row]=signed_word((uint32_t)shift(accumulator,12));
    }
    perspective(c,cue_written,&out);*output=out;return 0;
}
int model_project_rtps(const model_project_controls *c,const model_pose_vertex *vertex,
    model_project_vertex *output) {
    return project(c,vertex,1,output);
}
int model_project_camera(const model_project_controls *c,const int32_t camera[3],model_project_vertex *output) {
    if(!c||!camera||!output)return MODEL_PROJECT_INVALID;
    model_project_vertex out={0};for(uint32_t i=0;i<3;i++)out.camera[i]=camera[i];
    perspective(c,1,&out);*output=out;return 0;
}
static void push(model_project_fifo *fifo,const model_project_vertex *vertex) {
    for(uint32_t i=0;i<3;i++)fifo->depth[i]=fifo->depth[i+1u];
    fifo->depth[3]=vertex->depth;
    for(uint32_t i=0;i<2;i++){fifo->x[i]=fifo->x[i+1u];fifo->y[i]=fifo->y[i+1u];}
    fifo->x[2]=vertex->x;fifo->y[2]=vertex->y;
    if(vertex->cue_written){fifo->cue=vertex->cue;fifo->mac0=vertex->mac0;}
}
int model_project_rtps_fifo(const model_project_controls *c,const model_pose_vertex *vertex,
    model_project_fifo *fifo,model_project_vertex *output) {
    if(!fifo||!output)return MODEL_PROJECT_INVALID;
    model_project_vertex result;int error=project(c,vertex,1,&result);if(error)return error;
    model_project_fifo next=*fifo;push(&next,&result);*fifo=next;*output=result;return 0;
}
int model_project_rtpt(const model_project_controls *c,const model_pose_vertex vertices[3],
    model_project_fifo *fifo,model_project_triangle *output) {
    if(!c||!vertices||!fifo||!output)return MODEL_PROJECT_INVALID;
    model_project_fifo next=*fifo;model_project_triangle result={0};
    for(uint32_t i=0;i<3;i++) {
        int error=project(c,&vertices[i],i==2u,&result.vertices[i]);if(error)return error;
        push(&next,&result.vertices[i]);result.flags|=result.vertices[i].flags;
    }
    *fifo=next;*output=result;return 0;
}
int model_project_nclip(const int16_t x[3],const int16_t y[3],uint32_t *mac0,uint32_t *flags) {
    if(!x||!y||!mac0||!flags||mac0==flags)return MODEL_PROJECT_INVALID;
    int64_t area=0;
    for(uint32_t i=0;i<3;i++) {
        uint32_t next=(i+1u)%3u;
        area+=(int64_t)x[i]*y[next]-(int64_t)x[next]*y[i];
    }
    uint32_t flag=0;mac0_flags(area,&flag);if(flag)flag|=0x80000000u;
    *mac0=(uint32_t)area;*flags=flag;return 0;
}
int model_project_average_depth(const uint16_t *depth,uint32_t count,int16_t scale,
    model_project_order_depth *output) {
    if(!depth||!output||(count!=3u&&count!=4u))return MODEL_PROJECT_INVALID;
    uint32_t sum=0;for(uint32_t i=0;i<count;i++)sum+=depth[i];
    int64_t product=(int64_t)sum*scale;model_project_order_depth result={.mac0=(uint32_t)product};
    mac0_flags(product,&result.flags);
    result.depth=(uint16_t)saturate(shift(product,12),0,65535,18,&result.flags);
    if(result.flags&0x7f87e000u)result.flags|=0x80000000u;
    *output=result;return 0;
}
int model_project_bind(const gool_native_render_output *prepared,uint32_t offset_x,uint32_t offset_y,
    uint16_t distance,int16_t depth_scale,uint32_t depth_bias,model_project_controls *output) {
    if(!prepared||!output)return MODEL_PROJECT_INVALID;
    model_project_controls next={.offset_x=offset_x,.offset_y=offset_y,.distance=distance,
        .depth_scale=depth_scale,.depth_bias=depth_bias};
    for(uint32_t i=0;i<5;i++)next.rotation[i]=prepared->rotation[i];
    for(uint32_t i=0;i<3;i++)next.translation[i]=prepared->translation[i];
    *output=next;return 0;
}
