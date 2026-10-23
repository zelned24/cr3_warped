#include "gool_send_native.h"
static int checked(int error){return error>0?GOOL_SEND_NATIVE_INVALID:error;}
static int read_at(gool_send_native *c,uint32_t base,uint32_t offset,uint32_t *value) {
    if(!base||(base&3u)||(uint64_t)base+offset>UINT32_MAX)return GOOL_SEND_NATIVE_RANGE;
    return checked(c->services.read(c->services.context,base+offset,value));
}
static int write_at(gool_send_native *c,uint32_t base,uint32_t offset,uint32_t value) {
    if(!base||(base&3u)||(uint64_t)base+offset>UINT32_MAX)return GOOL_SEND_NATIVE_RANGE;
    return checked(c->services.write(c->services.context,base+offset,value));
}
static int finish(gool_send_native *c,int status,int32_t result) {
    c->active=0;c->status=status;if(!status)c->native_result=result;return status;
}
#define TRY(call) do{int error=(call);if(error)return error;}while(0)
static int ack(gool_send_native *c,uint32_t value) {
    return c->sender?write_at(c,c->sender,0xd4,value):0;
}
static int push(gool_send_native *c,uint32_t value) {
    uint32_t sp;TRY(read_at(c,c->target,0xbc,&sp));
    if((sp&3u)||sp<c->stack_begin||sp>c->end-4)return GOOL_SEND_NATIVE_RANGE;
    TRY(checked(c->services.write(c->services.context,sp,value)));
    return write_at(c,c->target,0xbc,sp+4);
}
static int bound_stack(gool_send_native *c) {
    uint32_t behavior,header,start;
    TRY(checked(c->services.object_end(c->services.context,c->target,&c->end)));
    if(c->end<c->target+0x104u)return GOOL_SEND_NATIVE_RANGE;
    TRY(read_at(c,c->target,0xc,&behavior));TRY(read_at(c,behavior,0x10,&header));
    TRY(read_at(c,header,0xc,&start));
    uint64_t begin=(uint64_t)c->target+0x40+(uint64_t)start*4;
    if(start<64u||begin>c->end||c->end-begin<12)return GOOL_SEND_NATIVE_RANGE;
    c->stack_begin=(uint32_t)begin;return 0;
}
static int prepare(gool_send_native *c) {
    uint32_t type,behavior,header,count;
    if(!c->target){TRY(ack(c,0));c->phase=8;return 0;}
    TRY(read_at(c,c->target,0,&type));
    if(!type){TRY(ack(c,0));c->phase=8;return 0;}
    TRY(read_at(c,c->target,0xc,&behavior));TRY(read_at(c,behavior,0x10,&header));
    TRY(read_at(c,header,0x10,&count));
    if((c->signal>>8)>=count){TRY(ack(c,0));c->phase=8;return 0;}
    TRY(ack(c,1));
    uint32_t handler;TRY(read_at(c,c->target,0xcc,&handler));
    TRY(write_at(c,c->target,0x5c,c->sender));
    if(!handler){c->handler_result=-26;c->phase=3;return 0;}
    TRY(bound_stack(c));
    TRY(push(c,c->signal));TRY(push(c,c->arguments));
    TRY(checked(c->services.enter_frame(c->services.context,c->target,2,0xffff)));
    TRY(read_at(c,c->target,0xcc,&handler));TRY(write_at(c,c->target,0xc0,handler));
    c->phase=1;return 0;
}
static int select_route(gool_send_native *c) {
    if(c->handler_result<-254) {
        if(c->receiver.new_state<0)return GOOL_SEND_NATIVE_INVALID;
        c->selected=(uint32_t)c->receiver.new_state;
    } else {
        uint32_t behavior,indices;uint16_t raw;
        TRY(read_at(c,c->target,0xc,&behavior));TRY(read_at(c,behavior,0x1c,&indices));
        uint64_t address=(uint64_t)indices+(uint64_t)(c->signal>>8)*2;
        if(address>UINT32_MAX)return GOOL_SEND_NATIVE_RANGE;
        TRY(checked(c->services.read16(c->services.context,(uint32_t)address,&raw)));
        c->selected=raw;TRY(ack(c,raw!=255));
        if(raw&0x8000u) {
            TRY(write_at(c,c->target,0x11c,c->signal));
            TRY(bound_stack(c));
            c->index=0;c->phase=4;return 0;
        }
    }
    c->phase=6;return 0;
}
static int compatible(gool_send_native *c) {
    if(c->selected==255){c->phase=8;return 0;}
    uint32_t flags,behavior,states,motion,protected_node;
    TRY(read_at(c,c->target,0xb0,&flags));
    if(c->signal==0x1800u||c->signal==0x1900u||c->signal==0x2500u||c->signal==0x2600u)flags&=~2u;
    TRY(read_at(c,c->target,0xc,&behavior));TRY(read_at(c,behavior,0x20,&states));
    if((uint64_t)states+(uint64_t)c->selected*16>UINT32_MAX)return GOOL_SEND_NATIVE_RANGE;
    TRY(read_at(c,0x80060a90u,0,&protected_node));
    TRY(read_at(c,states,c->selected*16,&motion));
    if(c->target==protected_node) {
        uint32_t mode;TRY(read_at(c,c->target,0x108,&mode));if(mode-2u<3u)flags|=0x402u;
    }
    if(flags&motion){TRY(ack(c,0));c->phase=8;return 0;}
    TRY(write_at(c,c->target,0x11c,c->signal));
    if(c->signal==0x1800u||c->signal==0x1900u) {
        TRY(read_at(c,c->target,0xa8,&flags));TRY(write_at(c,c->target,0xa8,flags|0x10000u));
    }
    c->phase=7;return 0;
}
#undef TRY
int gool_send_native_begin(gool_send_native *c,const gool_send_native_services *s,
    uint32_t sender,uint32_t target,int32_t signal,uint32_t argc,uint32_t arguments) {
    if(!c||c->active||!s||!s->read||!s->read16||!s->write||!s->object_end||!s->enter_frame||
       !s->dispatch||!s->enter_state||(sender&3u)||(target&3u)||signal<0||argc>15||
       (argc&&(!arguments||(arguments&3u)))||target>UINT32_MAX-0x120u)return GOOL_SEND_NATIVE_INVALID;
    gool_send_native_services copy=*s;
    *c=(gool_send_native){.services=copy,.sender=sender,.target=target,.signal=(uint32_t)signal,
        .argc=argc,.arguments=arguments,.active=1,.status=GOOL_SEND_NATIVE_BUDGET,
        .receiver={.new_state=-1},.native_result=-255};return GOOL_SEND_NATIVE_BUDGET;
}
int gool_send_native_step(gool_send_native *c,uint32_t budget) {
    if(!c)return GOOL_SEND_NATIVE_INVALID;
    if(!c->active||!budget)return c->status;
    while(budget--) {
        int error=0;
        switch(c->phase) {
        case 0:error=prepare(c);break;
        case 1: {
            int32_t result;
            error=c->services.dispatch(c->services.context,c->target,8,&c->receiver,&result);
            if(error==GOOL_SEND_NATIVE_PROVIDER_PENDING){c->status=GOOL_SEND_NATIVE_WAIT;return c->status;}
            error=checked(error);
            if(!error) {
                if(result!=-26&&result>=-254)return finish(c,0,result);
                c->handler_result=result;
                if(c->sender) {
                    if(!c->receiver.has_ack)return finish(c,GOOL_SEND_NATIVE_INVALID,0);
                    error=ack(c,c->receiver.was_b!=0);
                }
                c->phase=3;
            }
            break;
        }
        case 3:error=select_route(c);break;
        case 4:
            if(c->index<c->argc) {
                uint32_t value;
                if((uint64_t)c->arguments+c->index*4>UINT32_MAX)error=GOOL_SEND_NATIVE_RANGE;
                else error=checked(c->services.read(c->services.context,c->arguments+c->index*4,&value));
                if(!error)error=push(c,value);
                if(!error)c->index++;
            } else {
                error=checked(c->services.enter_frame(c->services.context,c->target,c->argc,0xffff));
                uint32_t behavior=0,code=0;
                if(!error)error=read_at(c,c->target,0xc,&behavior);
                if(!error)error=read_at(c,behavior,0x14,&code);
                uint64_t address=(uint64_t)code+(c->selected&0x3fffu)*4;
                if(!error&&address>UINT32_MAX)error=GOOL_SEND_NATIVE_RANGE;
                if(!error)error=write_at(c,c->target,0xc0,(uint32_t)address);
                if(!error)c->phase=5;
            }
            break;
        case 5: {
            int32_t result;error=c->services.dispatch(c->services.context,c->target,3,0,&result);
            if(error==GOOL_SEND_NATIVE_PROVIDER_PENDING){c->status=GOOL_SEND_NATIVE_WAIT;return c->status;}
            error=checked(error);if(!error)return finish(c,0,result);break;
        }
        case 6:error=compatible(c);break;
        case 7: {
            int32_t result;
            error=c->services.enter_state(c->services.context,c->target,c->selected,c->argc,c->arguments,&result);
            if(error==GOOL_SEND_NATIVE_PROVIDER_PENDING){c->status=GOOL_SEND_NATIVE_WAIT;return c->status;}
            error=checked(error);if(!error)return finish(c,0,-255);break;
        }
        case 8:return finish(c,0,-255);
        default:error=GOOL_SEND_NATIVE_INVALID;break;
        }
        if(error)return finish(c,error,0);
    }
    c->status=GOOL_SEND_NATIVE_BUDGET;return c->status;
}
