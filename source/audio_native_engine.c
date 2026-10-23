#include "audio_native_engine.h"
#include <string.h>

#define TRY(call) do{int native_error=(call);if(native_error)return native_error<0?native_error:AUDIO_VOICE_INVALID;}while(0)
#define REQUIRE(service) do{if(!(service))return AUDIO_VOICE_SERVICE_REQUIRED;}while(0)
static int valid(AudioNativeEngine *e){return e&&e->registry&&e->registry->domain&&!(e->gp&3u)&&e->gp<=UINT32_MAX-0x28cu;}
static int32_t s32(uint32_t v){return v<=INT32_MAX?(int32_t)v:-1-(int32_t)(UINT32_MAX-v);}
static int16_t s16(uint16_t v){return v<=INT16_MAX?(int16_t)v:(int16_t)(-1-(int32_t)(UINT16_MAX-v));}
static uint32_t shr(uint32_t v,unsigned n){return (v>>n)|((v&0x80000000u)?UINT32_MAX<<(32u-n):0);}
static int rd(AudioNativeEngine *e,uint32_t a,uint32_t *v){return gool_domain_read32(e->registry->domain,a,v);}
static int wr(AudioNativeEngine *e,uint32_t a,uint32_t v){return gool_domain_write32(e->registry->domain,a,v);}
static int rh(AudioNativeEngine *e,uint32_t a,uint16_t *v){return gool_domain_read16(e->registry->domain,a,v);}
static int wh(AudioNativeEngine *e,uint32_t a,uint16_t v){return gool_domain_write16(e->registry->domain,a,v);}
static int rb(AudioNativeEngine *e,uint32_t a,uint8_t *v){return gool_domain_read8(e->registry->domain,a,v);}
static int wb(AudioNativeEngine *e,uint32_t a,uint8_t v){return gool_domain_write8(e->registry->domain,a,v);}
static int read_cb(void *c,uint32_t a,uint32_t *v){return rd(c,a,v);}
static int write_cb(void *c,uint32_t a,uint32_t v){return wr(c,a,v);}
static int volume_cb(void *c,uint32_t mask,int16_t *l,int16_t *r){AudioNativeEngine *e=c;REQUIRE(e->volume);return e->volume(e->context,mask,l,r);}
static int random_cb(void *c,uint32_t upper,int32_t *v){AudioNativeEngine *e=c;REQUIRE(e->random);return e->random(e->context,upper,v);}
static int bank_cb(void *c,uint32_t eid,uint32_t *v){AudioNativeEngine *e=c;REQUIRE(e->bank);return e->bank(e->context,eid,v);}
static int resource_cb(void *c,uint32_t ref,uint32_t mode,uint32_t create,uint32_t *v){AudioNativeEngine *e=c;REQUIRE(e->resource);return e->resource(e->context,ref,mode,create,v);}
static void put32(AudioNativeSpuAttributes *a,unsigned at,uint32_t v){for(unsigned i=0;i<4;i++)a->bytes[at+i]=(uint8_t)(v>>(8*i));}
static void put16(AudioNativeSpuAttributes *a,unsigned at,uint16_t v){a->bytes[at]=(uint8_t)v;a->bytes[at+1]=(uint8_t)(v>>8);}
static int reset(AudioNativeEngine *e){
    AudioVoiceTemplate state;AudioVoiceEnvironment env={0};
    for(unsigned i=0;i<76;i++)TRY(rb(e,0x8006cf78u+i,&state.bytes[i]));
    TRY(audio_voice_template_reset_native(&state,&env,e,read_cb));
    /* Actual reset changes these fields; do not rewrite untouched template
     * bytes, which could be shared with a live native provider. */
    TRY(wr(e,0x8006cf80u,1));TRY(wb(e,0x8006cf90u,1));TRY(wb(e,0x8006cf91u,128));
    TRY(wh(e,0x8006cf92u,0x3fff));TRY(wh(e,0x8006cf96u,0x1000));
    TRY(wr(e,0x8006cf88u,0x6396347f));TRY(wh(e,0x8006cf94u,0));TRY(wr(e,0x8006cf8cu,0));
    TRY(wr(e,0x8006cf98u,0));TRY(wr(e,0x8006cf9cu,0));TRY(wr(e,0x8006cfa0u,0));
    uint32_t flags=(uint32_t)state.bytes[0]|(uint32_t)state.bytes[1]<<8|(uint32_t)state.bytes[2]<<16|(uint32_t)state.bytes[3]<<24;
    return wr(e,0x8006cf78u,flags);
}
static int divide(uint32_t a,uint32_t b,uint32_t *out){
    if(!b||(a==0x80000000u&&b==UINT32_MAX))return GOOL_VM_ARITHMETIC;
    *out=(uint32_t)(s32(a)/s32(b));return 0;
}
static int pitch(AudioNativeEngine *e,uint16_t input,uint16_t *output){
    uint32_t factor;TRY(rd(e,0x800690b4u,&factor));
    uint32_t product=(uint32_t)((uint64_t)input*factor);
    if(product&0x80000000u)product+=0xfffu;
    *output=(uint16_t)shr(product,12);return 0;
}
static int vector(AudioNativeEngine *e,uint32_t address,uint32_t output[3]){
    for(unsigned i=0;i<3;i++)TRY(rd(e,address+i*4u,&output[i]));
    return 0;
}
static int store_vector(AudioNativeEngine *e,uint32_t address,const uint32_t input[3]){
    for(unsigned i=0;i<3;i++)TRY(wr(e,address+i*4u,input[i]));
    return 0;
}
int audio_native_engine_play(void *context,uint32_t node,uint32_t sample,int16_t volume,int32_t *result){
    AudioNativeEngine *e=context;if(!valid(e)||!result)return AUDIO_VOICE_INVALID;
    uint32_t word,first,flags;int32_t channel;
    TRY(rd(e,0x80068f80u,&word));
    if(!word)goto refused;
    TRY(rd(e,0x80068f1cu,&word));
    if(!(word&0x1000u)){TRY(rd(e,node+0x100u,&word));if(!(word&0x40000u))goto refused;}
    TRY(rd(e,e->gp+0x28cu,&first));
    TRY(audio_voice_select_native(e,read_cb,volume_cb,random_cb,first,&channel));
    if(channel<=0)goto refused;
    if(channel>=24)return AUDIO_VOICE_INVALID;
    uint32_t voice=0x8006c7d8u+(uint32_t)channel*80u;
    uint32_t sample_address=sample;
    if(sample&1u)sample_address=sample&~1u;
    else {
        int32_t decoded;TRY(audio_voice_resolve_native(e,read_cb,write_cb,bank_cb,resource_cb,0x8006cf78u,sample,&decoded));
        if(decoded==-14)goto refused;
        sample_address=(uint32_t)decoded;
        TRY(rd(e,0x8006cf78u,&flags));if((flags>>15)&7u)TRY(wr(e,0x8006cf78u,flags|0x40000u));
    }
    for(unsigned block=0;block<4;block++){
        uint32_t words[4];for(unsigned i=0;i<4;i++)TRY(rd(e,0x8006cf78u+block*16u+i*4u,&words[i]));
        for(unsigned i=0;i<4;i++)TRY(wr(e,voice+4u+block*16u+i*4u,words[i]));
    }
    uint16_t master;TRY(rh(e,e->gp+0x278u,&master));
    uint32_t tail[3];for(unsigned i=0;i<3;i++)TRY(rd(e,0x8006cfb8u+i*4u,&tail[i]));
    for(unsigned i=0;i<3;i++)TRY(wr(e,voice+68u+i*4u,tail[i]));
    uint32_t product=(uint32_t)((int64_t)volume*s16(master));
    TRY(rd(e,voice+4u,&flags));TRY(wh(e,voice+0x1eu,(uint16_t)shr(product,14)));
    if(flags&0x40u){
        uint16_t target,current;uint32_t ticks,increment;
        TRY(rh(e,voice+0x1eu,&current));TRY(rh(e,voice+0x3cu,&target));TRY(rd(e,voice+0x40u,&ticks));
        TRY(divide((uint32_t)((int32_t)s16(target)-s16(current)),ticks,&increment));TRY(wr(e,voice+0x44u,increment));
    }
    uint32_t owner;TRY(rd(e,voice+0xcu,&owner));
    if(owner==1){TRY(wr(e,voice+0xcu,node));TRY(rd(e,voice+0xcu,&owner));}
    if(owner){
        uint32_t input[3],output[3];TRY(vector(e,owner+0x60u,input));TRY(store_vector(e,voice+0x30u,input));
        REQUIRE(e->transform);TRY(e->transform(e->context,input,output,1));TRY(store_vector(e,voice+0x24u,output));
    }
    TRY(reset(e));TRY(rd(e,voice+4u,&flags));flags=(flags&0xfe07ffffu)|(((uint32_t)channel&63u)<<19);
    TRY(wr(e,voice+4u,flags));
    AudioNativeSpuAttributes attr={{0}};uint32_t mask=1u<<(uint32_t)channel;
    put32(&attr,0,mask);put32(&attr,4,0xff93u);
    TRY(rd(e,voice+0xcu,&owner));uint8_t category;TRY(rb(e,owner+0x120u,&category));
    int16_t stereo[2];uint16_t gain;TRY(rh(e,voice+0x1eu,&gain));
    if(category==6||category==7||!((flags>>9)&15u)){
        int32_t value=s16(gain);if(value<0)value=0;if(value>0x3fff)value=0x3fff;
        stereo[0]=stereo[1]=(int16_t)value;
        TRY(rd(e,voice+4u,&flags));TRY(wr(e,voice+4u,flags&~0x1e00u));
    }else{
        if(flags&0x4000u){REQUIRE(e->curve);TRY(rh(e,e->gp+0x278u,&master));TRY(e->curve(e->context,0x8001056cu,volume,s16(master),s16(gain)));}
        REQUIRE(e->spatial);TRY(e->spatial(e->context,voice+4u,voice+0x24u,s16(gain),stereo));
    }
    put16(&attr,8,(uint16_t)stereo[0]);put16(&attr,10,(uint16_t)stereo[1]);
    uint16_t raw_pitch,scaled;TRY(rh(e,voice+0x22u,&raw_pitch));TRY(pitch(e,raw_pitch,&scaled));put16(&attr,0x14,scaled);
    put32(&attr,0x1c,sample_address);put32(&attr,0x24,1);put32(&attr,0x28,1);put32(&attr,0x2c,3);
    put16(&attr,0x38,15);
    REQUIRE(e->attributes);TRY(e->attributes(e->context,&attr));
    TRY(rd(e,voice+4u,&flags));REQUIRE(e->reverb);TRY(e->reverb(e->context,(flags>>13)&1u,mask));
    TRY(rd(e,voice+4u,&flags));if(!(flags&0x40010u)){REQUIRE(e->key);TRY(e->key(e->context,1,mask));}
    TRY(rd(e,e->gp+0x270u,&word));word++;TRY(rd(e,voice+4u,&flags));
    TRY(wr(e,voice,word));TRY(wr(e,e->gp+0x270u,word));TRY(wr(e,voice+4u,flags|8u));
    *result=s32(word);return 0;
refused:
    TRY(reset(e));*result=-99;return 0;
}
int audio_native_engine_control(void *context,uint32_t target,uint32_t options,
    uint32_t source,uint32_t node,int32_t *result){
    AudioNativeEngine *e=context;if(!valid(e)||!result)return AUDIO_VOICE_INVALID;
    uint32_t word,first;TRY(rd(e,0x80068f1cu,&word));
    if(!(word&0x1000u)){TRY(rd(e,node+0x100u,&word));if(!(word&0x40000u)){*result=0;return 0;}}
    if(s32(target)<-98){*result=0;return 0;}
    TRY(rd(e,e->gp+0x28cu,&first));if(first>24u)return AUDIO_VOICE_INVALID;
    uint32_t selector=options&0x0fffffffu;
    for(uint32_t channel=first;channel<24u;channel++){
        uint32_t voice=0x8006c7d8u+channel*80u,state=voice+4u;
        if(target&&target!=UINT32_MAX-1u){
            TRY(rd(e,voice+(target==UINT32_MAX?0xcu:0),&word));
            if(word!=(target==UINT32_MAX?node:target))continue;
        }
        if(!target)state=0x8006cf78u;
        uint32_t flags;TRY(rd(e,state,&flags));if(target&&!(flags&8u))continue;
        const uint32_t option_masks[3]={0x80000000u,0x40000000u,0x20000000u};
        for(unsigned i=0;i<3;i++)if(options&option_masks[i]){
            TRY(rd(e,state,&flags));TRY(wr(e,state,flags|(1u<<i)));
        }
        AudioNativeSpuAttributes attr={{0}};uint32_t attr_mask=0;
        TRY(rd(e,state,&flags));
        if(selector==0){
            uint16_t master;TRY(rh(e,e->gp+0x278u,&master));TRY(rd(e,source,&word));
            uint16_t gain=(uint16_t)((uint32_t)((uint64_t)word*(uint32_t)(int32_t)s16(master))>>14);
            if(flags&4u){
                uint32_t ticks;TRY(rd(e,e->gp+0x188u,&ticks));TRY(wr(e,state+60u,ticks));TRY(wh(e,state+56u,gain));
                if(target){uint16_t current;uint32_t increment;TRY(rh(e,state+26u,&current));
                    TRY(divide((uint32_t)((int32_t)s16(gain)-s16(current)),ticks,&increment));TRY(wr(e,state+64u,increment));}
                TRY(rd(e,state,&flags));TRY(wr(e,state,flags|0x40u));
            }else{
                TRY(wh(e,state+26u,gain));
                if(target){int16_t stereo[2];REQUIRE(e->spatial);TRY(e->spatial(e->context,state,state+32u,s16(gain),stereo));
                    attr_mask=3;put16(&attr,8,(uint16_t)stereo[0]);put16(&attr,10,(uint16_t)stereo[1]);}
            }
        }else if(selector==1){
            uint16_t value;TRY(rh(e,source,&value));
            if(flags&4u){
                TRY(wh(e,state+58u,value));uint16_t current;uint32_t ticks,increment;
                TRY(rh(e,state+30u,&current));TRY(rd(e,e->gp+0x188u,&ticks));
                TRY(divide((uint32_t)((int32_t)value-current),ticks,&increment));
                TRY(rd(e,state,&flags));TRY(wr(e,state+68u,ticks));TRY(wr(e,state,flags|0x80u));TRY(wr(e,state+72u,increment));
            }else{TRY(wh(e,state+30u,value));if(target){uint16_t scaled;TRY(pitch(e,value,&scaled));attr_mask=0x10;put16(&attr,0x14,scaled);}}
        }else if(selector==2||selector==3){
            uint32_t values[3];TRY(vector(e,source,values));
            if(selector==2){uint32_t transformed[3];REQUIRE(e->transform);TRY(e->transform(e->context,values,transformed,1));TRY(store_vector(e,source,transformed));}
            TRY(rd(e,state,&flags));if(!(flags&4u)){
                TRY(vector(e,source,values));TRY(store_vector(e,state+32u,values));
                if(target){int16_t stereo[2];uint16_t gain;TRY(vector(e,source,values));TRY(store_vector(e,voice+0x24u,values));
                    TRY(rh(e,state+26u,&gain));REQUIRE(e->spatial);TRY(e->spatial(e->context,state,state+32u,s16(gain),stereo));
                    attr_mask=3;put16(&attr,8,(uint16_t)stereo[0]);put16(&attr,10,(uint16_t)stereo[1]);}
            }
        }else if(selector==4||selector==12){uint8_t value;TRY(rb(e,source,&value));TRY(wb(e,state+(selector==4?24u:25u),value));
        }else if(selector==5){TRY(rd(e,source,&word));TRY(wr(e,state+8u,word));
        }else if(selector==6){TRY(rd(e,source,&word));TRY(wr(e,e->gp+0x188u,word));if(!word)TRY(wr(e,e->gp+0x188u,1));
        }else if(selector==7){uint16_t value;TRY(rh(e,source,&value));TRY(rd(e,state,&flags));TRY(wh(e,state+28u,value));TRY(wr(e,state,flags|0x10u));
        }else if(selector==8){TRY(rd(e,source,&word));if(word==1){uint8_t mode;TRY(audio_voice_spatial_mode_native(e,read_cb,&mode));word=mode;}
            TRY(rd(e,state,&flags));TRY(wr(e,state,(flags&~0x1e00u)|((word&15u)<<9)));
        }else if(selector==10||selector==11){TRY(rd(e,state,&flags));TRY(rd(e,source,&word));
            uint32_t mask=selector==10?0x4000u:0x2000u;TRY(wr(e,state,(flags&~mask)|((word<<(selector==10?6:5))&mask)));
        }else if(selector==13){
            if(source){TRY(rd(e,source,&word));TRY(wr(e,state+16u,word));}
            else{TRY(rd(e,state+16u,&word));*result=word!=0x6396347fu;return 0;}
        }else if(selector==14){
            TRY(rd(e,source,&word));
            if(word==256){TRY(rd(e,state,&flags));*result=!(flags&0x40010u);return 0;}
            REQUIRE(e->resource);uint32_t entry;TRY(e->resource(e->context,voice+0x10u,1,0,&entry));
            if(s32(entry)>=-254){*result=0;return 0;}
            TRY(rd(e,entry+0xcu,&word));if(s32(word)<2){*result=0;return 0;}
            uint32_t owner,start,end,catalog,count;TRY(rd(e,state+8u,&owner));TRY(rd(e,owner+0xdcu,&end));TRY(rd(e,owner+0xe0u,&start));
            TRY(rd(e,entry+0x14u,&catalog));uint32_t index=end-start+2u;
            if(s32(index)<0){*result=0;return 0;}
            TRY(rd(e,catalog,&count));if(s32(index)>=s32(count)){*result=0;return 0;}
            uint8_t packed;TRY(rb(e,catalog+4u+(index>>1),&packed));*result=(index&1u)?packed>>4:packed&15u;return 0;
        }
        if(attr_mask){put32(&attr,0,1u<<channel);put32(&attr,4,attr_mask);REQUIRE(e->attributes);TRY(e->attributes(e->context,&attr));}
        if(s32(target)>=0){*result=0;return 0;}
    }
    *result=s32(target)>=0?-1:0;return 0;
}
int audio_native_engine_stop(void *context,uint32_t node){
    AudioNativeEngine *e=context;if(!valid(e))return AUDIO_VOICE_INVALID;if(!node)return 0;
    uint32_t first;TRY(rd(e,e->gp+0x28cu,&first));if(first>24u)return AUDIO_VOICE_INVALID;
    for(uint32_t i=first;i<24u;i++){
        uint32_t voice=0x8006c7d8u+i*80u,flags,owner,id;TRY(rd(e,voice+4u,&flags));
        if(!(flags&8u))continue;
        TRY(rd(e,voice+0xcu,&owner));if(owner!=node)continue;
        TRY(rd(e,voice,&id));TRY(rd(e,voice+0xcu,&owner));TRY(wr(e,e->gp+0x188u,60));
        /* Original stack-local zero source is represented by a temporarily
         * mapped real domain word, not an arbitrary shared scratch overwrite. */
        uint32_t zero=0;gool_domain_lease lease;
        int mapped=gool_domain_map_mutable(e->registry->domain,0xbffe0000u,&zero,4,&lease);TRY(mapped);
        int32_t ignored;int service=audio_native_engine_control(e,id,0x40000000u,0xbffe0000u,owner,&ignored);
        int cleanup=gool_domain_release(e->registry->domain,lease);TRY(cleanup);TRY(service);
        TRY(wr(e,voice+0xcu,0));TRY(wb(e,voice+0x1du,0));
    }
    return 0;
}
