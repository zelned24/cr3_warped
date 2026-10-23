#include "gool_initialize.h"
#include <limits.h>

static int32_t signed_word(uint32_t value) {
    return value<=INT32_MAX?(int32_t)value:-1-(int32_t)(UINT32_MAX-value);
}
static int status(int error){return error>0?GOOL_INITIALIZE_INVALID:error;}
static int at(uint32_t base,uint32_t offset,uint32_t *address) {
    if(!base||(base&3u)||offset>UINT32_MAX-base)return GOOL_INITIALIZE_INVALID;
    *address=base+offset;return 0;
}
static int rd(const gool_initialize_services *s,uint32_t base,uint32_t offset,uint32_t *value) {
    uint32_t address;int error=at(base,offset,&address);if(error)return error;
    return status(s->read(s->context,address,value));
}
static int wr(const gool_initialize_services *s,uint32_t base,uint32_t offset,uint32_t value) {
    uint32_t address;int error=at(base,offset,&address);if(error)return error;
    return status(s->write(s->context,address,value));
}
#define TRY(expression) do{int error=(expression);if(error)return error;}while(0)
static int prepare(const gool_initialize_request *r,
    const gool_initialize_services *s,int32_t *native_result,uint32_t *selected) {
    if(!r||!s||!native_result||!s->read||!s->write||!s->geometry||!s->load||!s->enter_state||
       !r->node||(r->node&3u)||r->node>UINT32_MAX-0x138u)return GOOL_INITIALIZE_INVALID;
    const uint32_t node=r->node;uint32_t value,parent,type;
    if(!r->placed) {
        TRY(wr(s,node,0xb8,0));TRY(wr(s,node,0xf0,0));
        TRY(wr(s,node,0xf8,0));TRY(wr(s,node,0x120,0));
    } else {
        TRY(rd(s,node,0xf0,&value));TRY(status(s->geometry(s->context,value,&value)));
        TRY(wr(s,node,0x120,value));
    }
    TRY(rd(s,node,0x44,&parent));
    TRY(wr(s,node,0x12c,0xfffffffeu));TRY(wr(s,node,0xf4,0));TRY(wr(s,node,0,1));
    static const uint16_t zero_offsets[]={0x84,0x88,0x8c,0x90,0x94,0x98,0x9c,0xa0,0xa4,
        0x104,0xec,0xd8,0x94,0xa8,0xb0,0xac,0x108,0x118,0x110,0x128};
    for(unsigned i=0;i<sizeof(zero_offsets)/sizeof(zero_offsets[0]);i++)TRY(wr(s,node,zero_offsets[i],0));
    TRY(wr(s,node,0x138,65535));TRY(rd(s,parent,0,&type));
    if(type==1) {
        TRY(rd(s,parent,0x18,&value));TRY(wr(s,node,0x18,value));
        for(unsigned group=0;group<2;group++) {
            uint32_t words[4];
            for(unsigned i=0;i<4;i++)TRY(rd(s,parent,0x60+group*16+i*4,&words[i]));
            for(unsigned i=0;i<4;i++)TRY(wr(s,node,0x60+group*16+i*4,words[i]));
        }
        TRY(rd(s,parent,0x80,&value));TRY(wr(s,node,0x80,value));
    } else {
        TRY(wr(s,node,0x18,0));
        for(unsigned offset=0x6c;offset<=0x74;offset+=4)TRY(wr(s,node,offset,0));
        for(unsigned offset=0x78;offset<=0x80;offset+=4)TRY(wr(s,node,offset,0x1000));
    }
    if(r->class_index==4||r->class_index==5||r->class_index==29)TRY(wr(s,node,0x18,0));
    TRY(rd(s,node,4,&type));
    if(type!=3||r->class_index>=128){*native_result=-14;return 0;}
    uint32_t nsd,address;
    TRY(rd(s,0x80061044,0,&nsd));
    TRY(at(nsd,16+r->class_index*4,&address));TRY(rd(s,address,0,&value));
    if(value==0x6396347fu){*native_result=-14;return 0;}
    TRY(status(s->load(s->context,address,&value)));
    TRY(wr(s,node,0xc,value));TRY(wr(s,node,0x10,value));
    if(signed_word(value)>=-254){*native_result=-14;return 0;}
    TRY(wr(s,node,0x40,node));TRY(wr(s,node,0x58,0));
    TRY(wr(s,node,0x50,0));TRY(wr(s,node,0x5c,0));
    uint32_t reserved,behavior;
    TRY(rd(s,0x80068e98,0,&reserved));TRY(rd(s,node,0xc,&behavior));
    TRY(wr(s,node,0xb4,(r->class_index<<16)|r->object_id));
    TRY(wr(s,node,0xe8,0));TRY(wr(s,node,0xd0,0));TRY(wr(s,node,0x54,reserved));
    uint32_t header,index,indices;
    TRY(rd(s,behavior,0x10,&header));TRY(rd(s,node,0xb4,&index));index&=65535u;
    TRY(rd(s,header,0x10,&value));
    uint64_t byte_offset=((uint64_t)value+index)*2;
    if(byte_offset>UINT32_MAX)return GOOL_INITIALIZE_RANGE;
    TRY(rd(s,behavior,0x1c,&indices));
    if(!indices||(indices&1u)||byte_offset>UINT32_MAX-indices)return GOOL_INITIALIZE_INVALID;
    address=indices+(uint32_t)byte_offset;
    TRY(rd(s,address&~3u,0,&value));
    uint32_t state=(value>>((address&2u)*8))&65535u;
    if(state==255){*native_result=-27;return 0;}
    *selected=state;return GOOL_INITIALIZE_BUDGET;
}
#undef TRY

int gool_initialize_begin(gool_initialize *c,const gool_initialize_request *r,
                            const gool_initialize_services *s) {
    if(!c||c->active||!r||!s||!s->read||!s->write||!s->geometry||!s->load||!s->enter_state||
       !r->node||(r->node&3u)||r->node>UINT32_MAX-0x138u)return GOOL_INITIALIZE_INVALID;
    gool_initialize_request request=*r;gool_initialize_services services=*s;
    *c=(gool_initialize){.request=request,.services=services,.active=1,.status=GOOL_INITIALIZE_BUDGET};
    return GOOL_INITIALIZE_BUDGET;
}
int gool_initialize_step(gool_initialize *c,uint32_t budget) {
    if(!c)return GOOL_INITIALIZE_INVALID;
    if(!c->active||!budget)return c->status;
    while(budget--) {
        int error;
        if(!c->phase) {
            error=prepare(&c->request,&c->services,&c->native_result,&c->state);
            if(error==GOOL_INITIALIZE_BUDGET){c->phase=1;continue;}
        } else {
            uint32_t result;
            error=c->services.enter_state(c->services.context,c->request.node,c->state,
                      c->request.argument_count,c->request.arguments,&result);
            if(error==GOOL_INITIALIZE_PROVIDER_PENDING){c->status=GOOL_INITIALIZE_WAIT;return c->status;}
            error=status(error);
            if(!error)c->native_result=signed_word(result)<-254?-255:-14;
        }
        c->active=0;c->status=error;return error;
    }
    c->status=GOOL_INITIALIZE_BUDGET;return c->status;
}
int gool_initialize_native(const gool_initialize_request *r,
    const gool_initialize_services *s,int32_t *result) {
    if(!result)return GOOL_INITIALIZE_INVALID;
    gool_initialize c={0};int error=gool_initialize_begin(&c,r,s);
    if(error!=GOOL_INITIALIZE_BUDGET)return error;
    error=gool_initialize_step(&c,2);
    if(!error){*result=c.native_result;return 0;}
    return error>0?GOOL_INITIALIZE_ASYNC:error;
}
