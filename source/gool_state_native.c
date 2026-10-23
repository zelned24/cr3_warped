#include "gool_state_native.h"
#include <limits.h>

static int add(uint32_t base,uint32_t offset,uint32_t *address) {
    if((uint64_t)base+offset>UINT32_MAX)return GOOL_STATE_NATIVE_RANGE;
    *address=base+offset;return 0;
}
static int read_offset(const gool_state_native_services *s,uint32_t base,uint32_t offset,uint32_t *value) {
    uint32_t address;int error=add(base,offset,&address);if(error)return error;
    return s->read(s->context,address,value);
}
static int write_offset(const gool_state_native_services *s,uint32_t base,uint32_t offset,uint32_t value) {
    uint32_t address;int error=add(base,offset,&address);if(error)return error;
    return s->write(s->context,address,value);
}
static int script(const gool_state_native_services *s,uint32_t record,uint32_t offset,
                  uint32_t base,uint32_t alternate,uint32_t *pointer) {
    uint32_t address;int error=add(record,offset,&address);if(error)return error;
    uint16_t raw;error=s->read16(s->context,address,&raw);if(error)return error;
    if(raw==0x3fff){*pointer=0;return 0;}
    uint32_t code;error=read_offset(s,(raw&0x4000)?alternate:base,0x14,&code);if(error)return error;
    return add(code,(raw&0x3fff)*4u,pointer);
}
static int finish(gool_state_native *c,int status,int32_t result) {
    c->active=0;c->status=status;if(!status)c->native_result=result;return status;
}
static int checked(int status){return status>0?GOOL_STATE_NATIVE_INVALID:status;}
#define TRY(call) do{int error=checked(call);if(error)return error;}while(0)
static int prepare(gool_state_native *c) {
    const gool_state_native_services *s=&c->services;uint32_t node=c->node;
    TRY(s->object_end(s->context,node,&c->object_end));
    if(c->object_end<node+0x104u||(c->object_end&3u))return GOOL_STATE_NATIVE_RANGE;
    TRY(read_offset(s,node,0xd0,&c->resume));
    if(c->state==255){c->phase=8;c->native_result=-14;return 0;}
    uint32_t base,states,value,alternate_header,lookup,address;uint16_t index;
    TRY(read_offset(s,node,0xc,&base));
    TRY(write_offset(s,node,0x1c,c->state));
    TRY(read_offset(s,base,0x20,&states));
    TRY(add(states,c->state*16,&c->record));
    TRY(read_offset(s,c->record,4,&value));
    TRY(write_offset(s,node,0xb0,value));
    TRY(read_offset(s,node,0x10,&alternate_header));
    TRY(add(c->record,8,&address));TRY(s->read16(s->context,address,&index));
    TRY(read_offset(s,alternate_header,0x18,&lookup));
    TRY(add(lookup,(uint32_t)index*4,&c->class_word));
    c->phase=1;return 0;
}
static int setup(gool_state_native *c) {
    const gool_state_native_services *s=&c->services;uint32_t node=c->node;
    uint32_t alternate,base,value,header,start;
    TRY(s->load(s->context,c->class_word,&alternate));
    TRY(write_offset(s,node,0x14,alternate));
    const uint32_t offsets[3]={14,10,12},destinations[3]={0xc0,0xcc,0xc8};
    for(unsigned i=0;i<3;i++) {
        TRY(read_offset(s,node,0xc,&base));TRY(read_offset(s,node,0x14,&alternate));
        TRY(script(s,c->record,offsets[i],base,alternate,&value));
        TRY(write_offset(s,node,destinations[i],value));
    }
    TRY(read_offset(s,node,0xc,&base));
    TRY(write_offset(s,node,0xd0,0));TRY(write_offset(s,node,0xc4,node+0x40));
    TRY(read_offset(s,base,0x10,&header));TRY(read_offset(s,header,0xc,&start));
    if((uint64_t)node+0x40+(uint64_t)start*4>UINT32_MAX)return GOOL_STATE_NATIVE_RANGE;
    c->stack_begin=node+0x40+start*4;
    if(c->stack_begin>c->object_end||((uint64_t)c->argc+3)*4>c->object_end-c->stack_begin)
        return GOOL_STATE_NATIVE_RANGE;
    TRY(write_offset(s,node,0xbc,c->stack_begin));
    TRY(read_offset(s,node,0xa8,&value));TRY(write_offset(s,node,0xa8,value|0x20020u));
    TRY(read_offset(s,c->record,0,&value));TRY(write_offset(s,node,0x100,value));
    c->phase=2;return 0;
}
static int push(gool_state_native *c,uint32_t value) {
    const gool_state_native_services *s=&c->services;uint32_t sp;
    TRY(read_offset(s,c->node,0xbc,&sp));
    if((sp&3u)||sp<c->stack_begin||sp>c->object_end-4)return GOOL_STATE_NATIVE_RANGE;
    TRY(s->write(s->context,sp,value));return checked(write_offset(s,c->node,0xbc,sp+4));
}
static int after_resume(gool_state_native *c) {
    const gool_state_native_services *s=&c->services;uint32_t tick,pre,current;
    TRY(s->read(s->context,0x80068eecu,&tick));TRY(read_offset(s,c->node,0xc8,&pre));
    TRY(write_offset(s,c->node,0xe0,tick));
    if(pre) {
        TRY(s->read(s->context,0x80068ee8u,&current));
        if(current==c->node) {
            TRY(s->enter_frame(s->context,c->node,0,0xffff));
            TRY(read_offset(s,c->node,0xc8,&pre));TRY(write_offset(s,c->node,0xc0,pre));
            c->phase=7;return 0;
        }
    }
    c->native_result=-255;c->phase=8;return 0;
}
#undef TRY
int gool_state_native_begin(gool_state_native *c,const gool_state_native_services *s,
                            uint32_t node,uint32_t state,int32_t argc,uint32_t arguments) {
    if(!c||c->active||!s||!s->read||!s->read16||!s->write||!s->load||!s->object_end||
       !s->enter_frame||!s->dispatch||!node||(node&3u)||argc<0||argc>256||
       (argc&&(!arguments||(arguments&3u)))||(uint64_t)node+0x104>UINT32_MAX||
       (uint64_t)state*16>UINT32_MAX)return GOOL_STATE_NATIVE_INVALID;
    gool_state_native_services copy=*s;
    *c=(gool_state_native){.services=copy,.node=node,.state=state,.argc=(uint32_t)argc,
        .arguments=arguments,.active=1,.status=GOOL_STATE_NATIVE_BUDGET};
    return GOOL_STATE_NATIVE_BUDGET;
}
int gool_state_native_step(gool_state_native *c,uint32_t budget) {
    if(!c)return GOOL_STATE_NATIVE_INVALID;
    if(!c->active||!budget)return c->status;
    while(budget--) {
        const gool_state_native_services *s=&c->services;int error=0;
        switch(c->phase) {
        case 0:error=prepare(c);break;
        case 1:error=setup(c);break;
        case 2:
            if(c->argument_index==c->argc)c->phase=3;
            else {
                uint32_t address,value;
                error=add(c->arguments,c->argument_index*4,&address);
                if(!error)error=checked(s->read(s->context,address,&value));
                if(!error)error=push(c,value);
                if(!error)c->argument_index++;
            }
            break;
        case 3:
            error=checked(s->enter_frame(s->context,c->node,c->argc,0xffff));
            if(!error)c->phase=4;
            break;
        case 4:
            if(c->resume) {
                error=checked(s->enter_frame(s->context,c->node,0,0xffff));
                if(!error)error=checked(write_offset(s,c->node,0xc0,c->resume));
                if(!error)c->phase=5;
            } else {error=push(c,0);if(!error)c->phase=6;}
            break;
        case 5:case 7: {
            int32_t result;
            error=s->dispatch(s->context,c->node,c->phase==5?0x13:3,&result);
            if(error==GOOL_STATE_NATIVE_PROVIDER_PENDING){c->status=GOOL_STATE_NATIVE_WAIT;return c->status;}
            error=checked(error);
            if(!error) {
                if(c->phase==5&&result<-254)c->phase=6;
                else {c->native_result=result<-254?-255:result;c->phase=8;}
            }
            break;
        }
        case 6:error=after_resume(c);break;
        case 8:return finish(c,0,c->native_result);
        default:error=GOOL_STATE_NATIVE_INVALID;break;
        }
        if(error)return finish(c,error,0);
    }
    c->status=GOOL_STATE_NATIVE_BUDGET;return c->status;
}
int gool_state_native_enter(const gool_state_native_services *s,uint32_t node,uint32_t state,
                           int32_t argc,uint32_t arguments,int32_t *out) {
    if(!out)return GOOL_STATE_NATIVE_INVALID;
    gool_state_native cursor={0};
    int result=gool_state_native_begin(&cursor,s,node,state,argc,arguments);
    if(result!=GOOL_STATE_NATIVE_BUDGET)return result;
    result=gool_state_native_step(&cursor,1024);
    if(result==GOOL_STATE_NATIVE_DONE){*out=cursor.native_result;return 0;}
    return result>0?GOOL_STATE_NATIVE_ASYNC:result;
}
