#include "gool_native_controller.h"
#include "gool_atan.h"
#include "gool_trig.h"

#define ENGINE 0x80068e98u
#define PAD_COUNT 0x80065bc8u
#define PAD_BASE 0x80065bccu
static int32_t signed_word(uint32_t n){return n<=INT32_MAX?(int32_t)n:-1-(int32_t)(UINT32_MAX-n);}
static uint32_t sar(uint32_t n,unsigned s){return (n>>s)|((n&0x80000000u)?UINT32_MAX<<(32u-s):0u);}
static int fail(gool_native_controller *o,int e){return o->status=e<0?e:GOOL_NATIVE_CONTROLLER_INVALID;}
static int rd(gool_native_controller *o,uint32_t a,uint32_t *v){return gool_native_registry_read(o->factory->registry,a,v);}
static int wr(gool_native_controller *o,uint32_t a,uint32_t v){return gool_native_registry_write(o->factory->registry,a,v);}
#define READ(a,v) do{int e=rd(o,(a),&(v));if(e)return e;}while(0)
#define WRITE(a,v) do{int e=wr(o,(a),(uint32_t)(v));if(e)return e;}while(0)
#define BYTE(a,v) do{int e=gool_domain_read8(o->domain,(a),&(v));if(e)return e;}while(0)
#define PUTBYTE(a,v) do{int e=gool_domain_write8(o->domain,(a),(uint8_t)(v));if(e)return e;}while(0)
static int pad_call(gool_native_controller *o,uint32_t f,uint32_t a,uint32_t b,uint32_t c,uint32_t argc) {
    uint32_t args[3]={a,b,c};int e=o->services.pad(o->services.context,f,args,argc,&o->native_pad_result);
    return e?e<0?e:GOOL_NATIVE_CONTROLLER_INVALID:0;
}
int gool_native_controller_initialize(gool_domain *d,int32_t count,uint32_t maximum,
    const gool_native_controller_services *s) {
    if(!d||!s||!s->pad||!maximum||(count>0&&(uint32_t)count>maximum)||
       (uint64_t)PAD_BASE+(uint64_t)maximum*0x50u>UINT64_C(0x100000000))return GOOL_NATIVE_CONTROLLER_INVALID;
    static const uint16_t reset[]={0x164,0x18c,0x1f0,0x1f4,0x224,0x220};
    for(unsigned i=0;i<sizeof(reset)/sizeof(reset[0]);i++){int e=gool_domain_write32(d,ENGINE+reset[i],0);if(e)return e;}
    int e=gool_domain_write32(d,PAD_COUNT,(uint32_t)count);if(e)return e;
    for(int32_t i=0;i<count;i++) {
        uint32_t at=PAD_COUNT+(uint32_t)i*0x50u;
        static const uint8_t offsets[]={0x38,0x30,0x2c,0x28};
        for(unsigned j=0;j<sizeof(offsets);j++){e=gool_domain_write32(d,at+offsets[j],0);if(e)return e;}
        if(!i) {
            uint32_t args[3]={PAD_BASE,PAD_BASE+0x50u,0},result;
            e=s->pad(s->context,0x8004a748u,args,2,&result);if(e)return e<0?e:GOOL_NATIVE_CONTROLLER_INVALID;
            e=s->pad(s->context,0x80048e68u,args,0,&result);if(e)return e<0?e:GOOL_NATIVE_CONTROLLER_INVALID;
        }
        e=gool_domain_write8(d,at+0x50,0);if(e)return e;
        e=gool_domain_write8(d,at+0x51,0);if(e)return e;
        e=gool_domain_write32(d,at+0x4c,1);if(e)return e;
    }
    return 0;
}
static int timeline(gool_native_controller *o) {
    if(!o->factory->open||!o->factory->engine.tick||*o->factory->engine.tick!=o->tick)
        return GOOL_NATIVE_CONTROLLER_TIMELINE;
    uint32_t v;READ(0x80068eecu,v);return v==(uint32_t)o->tick?0:GOOL_NATIVE_CONTROLLER_TIMELINE;
}
static int close_signal(gool_native_controller *o) {
    int e=gool_native_router_close(&o->router);if(e)return e;
    e=gool_native_state_close(&o->state);if(e)return e;
    e=gool_native_delivery_close(&o->delivery);if(e)return e;
    if(o->arguments_mapped){e=gool_domain_release(o->domain,o->arguments_lease);if(e)return e;o->arguments_mapped=0;}
    o->signal_owned=0;return 0;
}
static int signal_step(gool_native_controller *o) {
    int e;
    if(!o->signal_owned) {
        o->signal_owned=1;
        e=gool_domain_map_mutable(o->domain,o->arguments_base+0x40u,&o->argument,4,&o->arguments_lease);if(e)return e;
        o->arguments_mapped=1;
        gool_native_state_engine engine={.context=o->factory,.actor=gool_native_factory_actor};
        e=gool_native_state_bind(&o->state,o->factory->registry,o->factory->pool,&engine,o->instruction_budget);if(e)return e;
        e=gool_native_delivery_bind(&o->delivery,o->factory,&o->state,o->arguments_base,o->instruction_budget);if(e)return e;
        e=gool_native_router_bind(&o->router,o->factory,o->frames,o->capacity,o->arguments_base+256u);if(e)return e;
        e=gool_native_delivery_begin_native(&o->delivery,0,o->target,0xe00,1,o->arguments_base+0x40u);
        if(e<0)return e;
        if(e!=0&&e!=1)return GOOL_NATIVE_CONTROLLER_INVALID;
    }
    e=gool_native_router_engine_delivery_step(&o->router,&o->delivery,1);if(e)return e;
    return close_signal(o);
}
static uint32_t root(uint32_t value) {
    uint32_t result=0,remainder=0;
    for(unsigned i=0;i<16;i++) {
        remainder|=value>>30;value<<=2;
        uint32_t trial=remainder-((result<<2)+1);result<<=1;
        if(signed_word(trial)>=0){result++;remainder=trial;}remainder<<=2;
    }
    return result;
}
/* Exact1FD58 with this caller's fixed512/5000 arguments. */
static uint32_t warp_angle(uint32_t angle) {
    int32_t wave=signed_word(sar((uint32_t)gool_trig_sin((int32_t)((angle%512u)*4u)-1024)*0x5000u,12));
    if(wave>4096)wave=4096;
    if(wave<-4096)wave=-4096;
    return (angle/512u)*512u+sar((uint32_t)(wave+4096)*512u,13);
}
static int normalize(gool_native_controller *o) {
    uint8_t x,y;uint32_t center,edge,scene,word,angle,sector,magnitude;
    BYTE(o->record+6,x);READ(ENGINE+0x2c0,center);BYTE(o->record+7,y);
    uint32_t delta=(uint32_t)x-center,numerator=delta<<7;
    READ(ENGINE+(signed_word(delta)<0?0x2c8:0x2d0),edge);
    uint32_t denominator=signed_word(delta)<0?center-edge:edge-center;
    if(!denominator||(numerator==0x80000000u&&denominator==UINT32_MAX))return GOOL_VM_ARITHMETIC;
    o->s3=signed_word(numerator)/signed_word(denominator);
    if(o->s3>127)o->s3=127;
    if(o->s3<-128)o->s3=-128;
    READ(ENGINE+0x2c4,center);delta=(uint32_t)y-center;numerator=(delta<<7)-delta;
    READ(ENGINE+(signed_word(delta)<0?0x2cc:0x2d4),edge);
    denominator=signed_word(delta)<0?center-edge:edge-center;
    if(!denominator||(numerator==0x80000000u&&denominator==UINT32_MAX))return GOOL_VM_ARITHMETIC;
    o->s2=signed_word(numerator)/signed_word(denominator);
    if(o->s2>127)o->s2=127;
    if(o->s2<-128)o->s2=-128;
    READ(0x80065d5cu,scene);
    int warped=0;
    if(scene){READ(scene+0x10,word);READ(word+0x2dc,word);warped=(word&0x00200000u)!=0;}
    int32_t result;int e=gool_atan2(o->s3,o->s2,&result);if(e)return e;
    angle=(uint32_t)result&0xfffu;if(warped)angle=warp_angle(angle);
    WRITE(o->record+0x38,angle);
    READ(o->record+0x38,angle);sector=((0x8fau-angle)&0xfffu)>>9;
    WRITE(o->record+0x40,sector);
    magnitude=sar(root((uint32_t)o->s3*(uint32_t)o->s3+(uint32_t)o->s2*(uint32_t)o->s2)<<8,7);
    WRITE(o->record+0x3c,magnitude);
    if(signed_word(magnitude)<0x66){WRITE(o->record+0x3c,0);WRITE(o->record+0x40,8);}
    return 0;
}
static int buttons(gool_native_controller *o,int special) {
    uint16_t raw;int e=gool_domain_read16(o->domain,o->record+2,&raw);if(e)return e;
    uint32_t value=(~(((uint32_t)raw&255u)<<8|(raw>>8)))&0xffffu;
    WRITE(o->record+0x28,value);
    if(special) {
        uint32_t v=value&0xf961u;
        if(value&2u)v|=4u;
        if(value&4u)v|=0x80u;
        if(value&8u)v|=0x10u;
        if(value&0x10u)v|=8u;
        if(value&0x80u)v|=2u;
        WRITE(o->record+0x28,v);
    }
    return 0;
}
static int rumble(gool_native_controller *o) {
    uint32_t a,b,c;READ(o->record+0x44,a);if(!a||o->index)return 0;
    READ(ENGINE+0xe0,a);READ(0x8006087cu,b);READ(ENGINE+0x204,c);
    int eligible=!(a&2u)&&!b&&!c;
    if(eligible){READ(ENGINE+0xa4,a);eligible=a==0x100u;}
    if(eligible) {
        READ(ENGINE+0x18c,a);
        uint32_t v=0;if(a){READ(ENGINE+0x164,b);v=(b&(1u<<((a-1u)&31u)))?255u:0u;}
        PUTBYTE(o->record+0x4c,v);READ(ENGINE+0x1f4,a);
        if(a){uint8_t v8;BYTE(ENGINE+0x1f0,v8);PUTBYTE(o->record+0x4d,v8);}
        else {READ(ENGINE+0x220,a);if(a){uint8_t v8;BYTE(ENGINE+0x224,v8);PUTBYTE(o->record+0x4d,v8);}else PUTBYTE(o->record+0x4d,0);}
    } else {PUTBYTE(o->record+0x4c,0);PUTBYTE(o->record+0x4d,0);}
    READ(0x8006087cu,a);
    if(!a) {
        static const uint16_t counters[]={0x18c,0x1f4,0x220};
        for(unsigned i=0;i<3;i++){READ(ENGINE+counters[i],a);if(a)WRITE(ENGINE+counters[i],a-1u);}
    }
    return pad_call(o,0x800492b4u,o->port,o->record+0x4c,2,3);
}
static int demo(gool_native_controller *o) {
    if(o->index){o->phase=11;return 0;}
    uint32_t mode,a,b,c,d;READ(ENGINE+0x204,mode);
    if(mode==3u){WRITE(o->record+0x28,0);o->phase=11;return 0;}
    if(mode==4u) {
        READ(ENGINE+0x208,a);
        if(a+1u<2u) {
            READ(o->gp+0x210,a);READ(o->gp+0x1e8,b);READ(a,c);READ(ENGINE+0x190,o->target);
            o->argument=b>=a+c*8u+0xc9cu;
            if(o->target){o->phase=101;return 0;}
            WRITE(ENGINE+0x204,0);
        }
    } else if(mode==2u) {
        READ(o->gp+0x210,a);WRITE(o->record+0x40,8);READ(o->gp+0x1e8,b);READ(o->record+0x28,c);WRITE(o->record+0x3c,0);
        if(b==a+0xc9cu){READ(0x80061a7cu,d);READ(a+0xc98,a);WRITE(d+0x18,a);}
        READ(o->gp+0x1e8,b);READ(b+4,d);WRITE(o->gp+0x1e8,b+8);WRITE(o->record+0x28,d);
        int end=0;
        if(c&0x9f0u){uint8_t raw;BYTE(o->record,raw);end=raw==0;}
        if(!end){READ(o->gp+0x210,a);READ(a,c);end=!(b+8u<a+c*8u+0xc9cu);}
        if(end) {
            READ(ENGINE+0x190,a);
            if(a){READ(ENGINE+0x70,b);WRITE(ENGINE+0x204,4);WRITE(ENGINE+0x70,b&0xffff6ff5u);}
            else WRITE(ENGINE+0x204,0);
        }
    }
    o->phase=11;return 0;
}
static int filter(gool_native_controller *o) {
    uint32_t flags,pause,buttons,mode;READ(ENGINE+0x84,flags);
    int apply;
    if(flags&0x10000u)apply=0;
    else {READ(0x8006087cu,pause);apply=!pause;}
    if(!apply){uint8_t raw;BYTE(o->record,raw);if(raw){READ(ENGINE+0x204,mode);apply=!mode;}}
    if(apply){READ(o->record+0x28,buttons);WRITE(o->record+0x3c,0);WRITE(o->record+0x40,8);WRITE(o->record+0x28,buttons&0xffff0f13u);}
    READ(o->record+0x28,buttons);if(buttons&0x1000u)WRITE(o->record+0x28,buttons&0xffffbfffu);
    READ(o->record+0x28,buttons);if(buttons&0x8000u)WRITE(o->record+0x28,buttons&0xffffdfffu);
    return 0;
}
static int publish(gool_native_controller *o) {
    /* Original readonly sixteen direction records8005BF00..8005C000. */
    static const struct {uint32_t sector,angle,magnitude;int8_t x,y;} directions[16]={
        {8,0,256,0,0},{0,0x800,256,0,127},{2,0x400,256,127,0},{1,0x600,327,127,127},
        {4,0,256,0,-128},{8,0,0,0,0},{3,0x200,327,127,-128},{8,0,0,0,0},
        {6,0xc00,256,-128,0},{7,0xa00,327,-128,127},{8,0,0,0,0},{8,0,0,0,0},
        {5,0xe00,327,-128,-128},{8,0,0,0,0},{8,0,0,0,0},{8,0,0,0,0}};
    static const uint16_t direction_buttons[]={0x1000,0x3000,0x2000,0x6000,0x4000,0xc000,0x8000,0x9000};
    uint32_t current,sector,angle,magnitude,previous;READ(o->record+0x28,current);
    uint32_t direction=(current>>12)&15u;
    if(direction) {
        WRITE(o->record+0x40,directions[direction].sector);WRITE(o->record+0x38,directions[direction].angle);
        WRITE(o->record+0x3c,directions[direction].magnitude);o->s3=directions[direction].x;o->s2=-directions[direction].y;
    } else {READ(o->record+0x40,sector);if(sector<8u){READ(o->record+0x28,current);WRITE(o->record+0x28,current|direction_buttons[sector]);}}
    if(!o->index) {
        READ(o->record+0x3c,magnitude);WRITE(ENGINE+0x250,magnitude);
        READ(o->record+0x38,angle);WRITE(ENGINE+0x2e4,o->s3);WRITE(ENGINE+0x2e8,0u-(uint32_t)o->s2);WRITE(ENGINE+0x254,angle);
    } else if(o->index==1u) {
        WRITE(ENGINE+0x300,o->s3);WRITE(ENGINE+0x304,0u-(uint32_t)o->s2);READ(o->record+0x28,current);WRITE(ENGINE+0x25c,current);
    }
    READ(o->record+0x2c,previous);READ(o->record+0x28,current);WRITE(o->record+0x24,(~previous&current)&0xffffu);
    return 0;
}
int gool_native_controller_bind(gool_native_controller *o,gool_native_factory *factory,uint32_t gp,uint32_t maximum,
    const gool_native_controller_services *s,gool_native_route_frame *frames,uint32_t capacity,uint32_t arguments_base,uint32_t instruction_budget) {
    if(!o||o->bound||!factory||!factory->open||!factory->registry||!factory->registry->domain||!factory->pool||!factory->engine.tick||
       !gp||(gp&3u)||gp>UINT32_MAX-0x214u||!maximum||(uint64_t)PAD_BASE+(uint64_t)maximum*0x50u>UINT64_C(0x100000000)||
       !s||!s->pad||!frames||!capacity||capacity>GOOL_NATIVE_ROUTER_DEPTH||!arguments_base||(arguments_base&3u)||
       (uint64_t)arguments_base+(capacity+1u)*256u>UINT64_C(0x100000000)||!instruction_budget)return GOOL_NATIVE_CONTROLLER_INVALID;
    o->factory=factory;o->domain=factory->registry->domain;o->gp=gp;o->max_controllers=maximum;o->services=*s;
    o->frames=frames;o->capacity=capacity;o->arguments_base=arguments_base;o->instruction_budget=instruction_budget;o->bound=1;return 0;
}
int gool_native_controller_begin(gool_native_controller *o,int32_t s2,int32_t s3) {
    if(!o||!o->bound||o->active||o->signal_owned||o->arguments_mapped||!o->factory->open||!o->factory->engine.tick)return GOOL_NATIVE_CONTROLLER_INVALID;
    o->tick=*o->factory->engine.tick;int e=timeline(o);if(e)return e;
    o->s2=s2;o->s3=s3;o->index=o->port=o->phase=0;o->record=PAD_BASE;o->active=1;return o->status=1;
}
int gool_native_controller_step(gool_native_controller *o,uint32_t budget) {
    if(!o||!o->bound)return GOOL_NATIVE_CONTROLLER_INVALID;
    if(!o->active||o->status<0)return o->status;
    while(budget--) {
        int e=timeline(o);if(e)return fail(o,e);
        uint32_t a,b,c;
        switch(o->phase) {
            case 0:
                READ(PAD_COUNT,a);if(signed_word(a)<=0){o->active=0;return o->status=0;}
                if(a>o->max_controllers)return fail(o,GOOL_NATIVE_CONTROLLER_LIMIT);
                o->phase=1;break;
            case 1:
                READ(o->record+0x2c,a);READ(o->record+0x28,b);READ(o->record+0x24,c);
                WRITE(o->record+0x34,a);WRITE(o->record+0x2c,b);WRITE(o->record+0x30,c);
                e=pad_call(o,0x80048f00u,o->port,0,0,1);if(e)return fail(o,e);
                if(o->native_pad_result==6u)o->phase=2;else {WRITE(o->record+0x44,0);o->phase=6;}
                break;
            case 2:
                e=pad_call(o,0x80048fc0u,o->port,1,0,3);if(e)return fail(o,e);
                if(o->native_pad_result!=7u){READ(o->record+0x48,a);o->phase=a==1u?3u:4u;}else o->phase=4;
                break;
            case 3:
                e=pad_call(o,0x8004926cu,o->port,1,0,3);if(e)return fail(o,e);
                WRITE(o->record+0x48,0);o->phase=4;break;
            case 4:
                e=pad_call(o,0x80049234u,o->port,0x800608ecu,0,2);if(e)return fail(o,e);
                WRITE(o->record+0x44,2);o->phase=6;break;
            case 6: {
                uint8_t raw,kind;BYTE(o->record,raw);
                if(raw){WRITE(o->record+0x48,1);o->phase=10;break;}
                BYTE(o->record+1,kind);
                if(kind==0x12){WRITE(o->record+0x48,1);PUTBYTE(o->record,1);}
                else if(kind==0x53||kind==0x73) {e=normalize(o);if(e)return fail(o,e);e=buttons(o,kind==0x53);if(e)return fail(o,e);}
                else {o->s2=o->s3=0;WRITE(o->record+0x3c,0);WRITE(o->record+0x40,8);e=buttons(o,0);if(e)return fail(o,e);}
                o->phase=9;break;
            }
            case 9:e=rumble(o);if(e)return fail(o,e);o->phase=10;break;
            case 10:e=demo(o);if(e)return fail(o,e);break;
            case 101:
                e=signal_step(o);if(e<0)return fail(o,e);if(e)break;
                WRITE(ENGINE+0x70,0x108);WRITE(ENGINE+0x204,3);o->phase=11;break;
            case 11:e=filter(o);if(e)return fail(o,e);o->phase=12;break;
            case 12:
                e=publish(o);if(e)return fail(o,e);
                READ(PAD_COUNT,a);o->index++;
                if((int32_t)o->index>=signed_word(a)){e=timeline(o);if(e)return fail(o,e);o->active=0;return o->status=0;}
                if(o->index>=o->max_controllers)return fail(o,GOOL_NATIVE_CONTROLLER_LIMIT);
                o->record+=0x50;o->port+=0x10;o->phase=1;break;
            default:return fail(o,GOOL_NATIVE_CONTROLLER_INVALID);
        }
    }
    return 1;
}
int gool_native_controller_close(gool_native_controller *o) {
    if(!o)return GOOL_NATIVE_CONTROLLER_INVALID;
    int e=close_signal(o);if(e)return e;
    *o=(gool_native_controller){0};return 0;
}
