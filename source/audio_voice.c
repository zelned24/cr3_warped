#include "audio_voice.h"
#include <string.h>

static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | (unsigned)p[1]<<8); }
static void put32(uint8_t *p, uint32_t v) { for (unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i)); }
static void put16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8); }
static int32_t signed32(uint32_t v) { return v<=INT32_MAX?(int32_t)v:-1-(int32_t)(UINT32_MAX-v); }

int audio_voice_decode_play(uint32_t instruction, AudioVoicePlay *out) {
    if(!out || instruction>>24!=0x41)return AUDIO_VOICE_INVALID;
    *out=(AudioVoicePlay){(uint16_t)((instruction>>12)&4095),(uint16_t)(instruction&4095)};
    return AUDIO_VOICE_OK;
}

int audio_voice_decode_control(uint32_t instruction, AudioVoiceControl *out) {
    if(!out || instruction>>24!=0x42)return AUDIO_VOICE_INVALID;
    AudioVoiceControl c={(uint16_t)(instruction&4095),(uint8_t)((instruction>>12)&63),
                         (uint8_t)((instruction>>20)&15),0};
    if(c.selector==15){c.options|=0x80000000u;c.selector=0;}
    if(instruction&0x40000)c.options|=0x40000000u;
    if(instruction&0x80000)c.options|=0x20000000u;
    *out=c;return AUDIO_VOICE_OK;
}

int audio_voice_spatial_mode_native(void *context, AudioVoiceNativeRead read, uint8_t *mode) {
    if(!read||!mode)return AUDIO_VOICE_INVALID;
    uint32_t entry,level;
    int error=read(context,0x80061044u,&entry);
    if(error)return error<0?error:AUDIO_VOICE_INVALID;
    if((entry&3u)||entry>UINT32_MAX-8u)return AUDIO_VOICE_INVALID;
    error=read(context,entry+8u,&level);
    if(error)return error<0?error:AUDIO_VOICE_INVALID;
    /* Original35-entry jump table8001057C: these cases reach33040 (6),
     * all other entries and unsigned(level-5)>=35 reach33048 (5).
     * SCUS_942.44 SHA2561b93cc563c90966e1d79b750f7d19bc1662859cceeb991775e9e9a22656d71d1. */
    uint8_t value=5;
    switch(level) {
        case 5:case 10:case 13:case 14:case 17:case 19:case 20:case 21:
        case 23:case 25:case 28:case 31:case 32:case 33:case 37:case 39:
            value=6;break;
        default:break;
    }
    *mode=value;return AUDIO_VOICE_OK;
}

static int native_priority(void *context,AudioVoiceNativeRead read,uint32_t address,uint32_t *priority) {
    uint32_t word;int error=read(context,address&~3u,&word);
    if(error)return error<0?error:AUDIO_VOICE_INVALID;
    *priority=(word>>((address&3u)*8u))&255u;return 0;
}
int audio_voice_resolve_native(void *context,AudioVoiceNativeRead read,
    AudioVoiceNativeWrite write,AudioVoiceNativeBank bank,AudioVoiceNativeResource resource,
    uint32_t state,uint32_t sample,int32_t *native_result) {
    if(!read||!write||!native_result||(state&3u)||(sample&3u)||state>UINT32_MAX-20u)
        return AUDIO_VOICE_INVALID;
    uint32_t reference,eid,flags,handle;
    int error=read(context,sample,&reference);if(error)return error<0?error:AUDIO_VOICE_INVALID;
    eid=reference;
    if(!(reference&1u)) {
        if(reference>UINT32_MAX-4u)return AUDIO_VOICE_INVALID;
        error=read(context,reference+4u,&eid);if(error)return error<0?error:AUDIO_VOICE_INVALID;
    }
    if(!bank)return AUDIO_VOICE_SERVICE_REQUIRED;
    error=bank(context,eid,&handle);if(error)return error<0?error:AUDIO_VOICE_INVALID;
    error=read(context,state,&flags);if(error)return error<0?error:AUDIO_VOICE_INVALID;
    error=write(context,state,flags&0xfffc7fffu);if(error)return error<0?error:AUDIO_VOICE_INVALID;
    error=read(context,sample,&reference);if(error)return error<0?error:AUDIO_VOICE_INVALID;
    error=write(context,state+20u,handle);if(error)return error<0?error:AUDIO_VOICE_INVALID;
    error=write(context,state+12u,reference);if(error)return error<0?error:AUDIO_VOICE_INVALID;
    if(handle) {
        error=read(context,state,&flags);if(error)return error<0?error:AUDIO_VOICE_INVALID;
        error=write(context,state,(flags&0xfffc7fffu)|0x50000u);if(error)return error<0?error:AUDIO_VOICE_INVALID;
        *native_result=0;return 0;
    }
    if(!resource)return AUDIO_VOICE_SERVICE_REQUIRED;
    error=resource(context,sample,0,1,&handle);if(error)return error<0?error:AUDIO_VOICE_INVALID;
    if(handle&&signed32(handle)>=-254){*native_result=-14;return 0;}
    if(handle&&(handle&0xc0000002u)==0xc0000002u) {
        *native_result=(int32_t)((handle&0x3ffffffdu)>>2);return 0;
    }
    error=read(context,state,&flags);if(error)return error<0?error:AUDIO_VOICE_INVALID;
    error=write(context,state,(flags&0xfffc7fffu)|0x48000u);if(error)return error<0?error:AUDIO_VOICE_INVALID;
    *native_result=0;return 0;
}
int audio_voice_select_native(void *context,AudioVoiceNativeRead read,
    AudioVoiceNativeVolume volume,AudioVoiceNativeRandom random,uint32_t first,
    int32_t *selected_voice) {
    if(!read||!selected_voice||first>24u)return AUDIO_VOICE_INVALID;
    uint32_t minimum_priority=255u;
    for(uint32_t i=first;i<24u;i++) {
        uint32_t priority,flags;
        int error=native_priority(context,read,0x8006c7d8u+i*80u+0x1du,&priority);if(error)return error;
        if(priority<minimum_priority)minimum_priority=priority;
        error=read(context,0x8006c7dcu+i*80u,&flags);
        if(error)return error<0?error:AUDIO_VOICE_INVALID;
        if(!(flags&8u)){*selected_voice=(int32_t)i;return 0;}
    }
    uint32_t priority;
    int error=native_priority(context,read,0x8006cf91u,&priority);if(error)return error;
    if(priority<minimum_priority){*selected_voice=-1;return 0;}
    int32_t selected=-1,minimum_volume=0x3fff;
    for(uint32_t i=first;i<24u;i++) {
        error=native_priority(context,read,0x8006c7d8u+i*80u+0x1du,&priority);if(error)return error;
        if(priority!=minimum_priority)continue;
        if(!volume)return AUDIO_VOICE_SERVICE_REQUIRED;
        int16_t left,right;error=volume(context,1u<<i,&left,&right);
        if(error)return error<0?error:AUDIO_VOICE_INVALID;
        int32_t l=left<0?-(int32_t)left:left,r=right<0?-(int32_t)right:right;
        int32_t amplitude=l>r?l:r;
        if(amplitude<minimum_volume){selected=(int32_t)i;minimum_volume=amplitude;}
    }
    error=native_priority(context,read,0x8006cf91u,&priority);if(error)return error;
    if(priority==minimum_priority&&minimum_volume>=0x800) {
        if(!random)return AUDIO_VOICE_SERVICE_REQUIRED;
        int32_t value;error=random(context,100,&value);
        if(error)return error<0?error:AUDIO_VOICE_INVALID;
        if(value>=50)selected=-1;
    }
    *selected_voice=selected;return 0;
}

int audio_voice_template_reset(AudioVoiceTemplate *state, unsigned spatial_mode) {
    if(!state || spatial_mode>15)return AUDIO_VOICE_INVALID;
    uint8_t *p=state->bytes;
    put32(p+8,1);p[24]=1;p[25]=128;put16(p+26,0x3fff);put16(p+30,0x1000);
    put32(p+16,0x6396347f);put16(p+28,0);put32(p+20,0);
    put32(p+32,0);put32(p+36,0);put32(p+40,0);
    uint32_t flags=u32(p)&~(0x1ffu|0x1e00u|0x4000u|0x38000u|0x40000u);
    put32(p,flags|(spatial_mode<<9)|0x2000u);
    return AUDIO_VOICE_OK;
}

int audio_voice_template_reset_native(AudioVoiceTemplate *state, AudioVoiceEnvironment *environment,
                                      void *context, AudioVoiceNativeRead read) {
    if(!state||!environment)return AUDIO_VOICE_INVALID;
    uint8_t mode;
    int error=audio_voice_spatial_mode_native(context,read,&mode);
    if(error)return error;
    error=audio_voice_template_reset(state,mode);
    if(error)return error;
    environment->spatial_mode=mode;return AUDIO_VOICE_OK;
}

int audio_voice_template_control(AudioVoiceTemplate *state, AudioVoiceEnvironment *environment,
                                 const AudioVoiceControl *control, const uint8_t *source,
                                 size_t source_size, int32_t *result) {
    if(!state||!environment||!control||!result||control->selector>14||
       environment->spatial_mode>15 || (control->options&0x1fffffffu))return AUDIO_VOICE_INVALID;
    if(control->target_member || control->selector==2 || control->selector==14)
        return AUDIO_VOICE_SERVICE_REQUIRED;
    unsigned selector=control->selector;
    size_t needed=selector==3?12:(selector==1||selector==7?2:
                  (selector==4||selector==12?1:(selector==9?0:4)));
    if(selector==13&&!source)needed=0;
    if((needed&&!source)||source_size<needed)return AUDIO_VOICE_INVALID;
    AudioVoiceTemplate next=*state;
    AudioVoiceEnvironment env=*environment;
    uint8_t *p=next.bytes;
    uint32_t flags=u32(p);
    if(control->options&0x80000000u)flags|=1;
    if(control->options&0x40000000u)flags|=2;
    if(control->options&0x20000000u)flags|=4;
    int32_t answer=0;
    switch(selector) {
    case 0: {
        uint32_t product=(uint32_t)((uint64_t)u32(source)*(uint32_t)(int32_t)env.master_volume);
        uint16_t volume=(uint16_t)(product>>14);
        if(flags&4) {put32(p+60,(uint32_t)env.ramp_ticks);put16(p+56,volume);flags|=0x40;}
        else put16(p+26,volume);
        break;
    }
    case 1:
        if(flags&4) {
            if(!env.ramp_ticks)return AUDIO_VOICE_INVALID;
            int32_t difference=(int32_t)u16(source)-(int32_t)u16(p+30);
            put16(p+58,u16(source));put32(p+68,(uint32_t)env.ramp_ticks);
            put32(p+72,(uint32_t)((int64_t)difference/env.ramp_ticks));flags|=0x80;
        } else put16(p+30,u16(source));
        break;
    case 3: if(!(flags&4))memcpy(p+32,source,12);break;
    case 4:p[24]=source[0];break;
    case 5:put32(p+8,u32(source));break;
    case 6:env.ramp_ticks=signed32(u32(source));if(!env.ramp_ticks)env.ramp_ticks=1;break;
    case 7:put16(p+28,u16(source));flags|=0x10;break;
    case 8: {
        uint32_t mode=u32(source);
        if(mode==1)mode=env.spatial_mode;
        flags=(flags&~0x1e00u)|((mode&15)<<9);break;
    }
    case 9:break;
    case 10:flags=(flags&~0x4000u)|((u32(source)<<6)&0x4000u);break;
    case 11:flags=(flags&~0x2000u)|((u32(source)<<5)&0x2000u);break;
    case 12:p[25]=source[0];break;
    case 13:
        if(source)put32(p+16,u32(source));
        else answer=u32(p+16)!=0x6396347fu;
        break;
    default:return AUDIO_VOICE_SERVICE_REQUIRED;
    }
    put32(p,flags);*state=next;*environment=env;*result=answer;
    return AUDIO_VOICE_OK;
}

int audio_voice_template_control_native(AudioVoiceTemplate *state, AudioVoiceEnvironment *environment,
                                        const AudioVoiceControl *control, const uint8_t *source,
                                        size_t source_size, int32_t *result,
                                        void *context, AudioVoiceNativeRead read) {
    if(!state||!environment||!control||!result)return AUDIO_VOICE_INVALID;
    AudioVoiceEnvironment captured=*environment;
    if(!control->target_member&&control->selector==8u&&source&&source_size>=4u&&u32(source)==1u) {
        int error=audio_voice_spatial_mode_native(context,read,&captured.spatial_mode);
        if(error)return error;
    }
    int error=audio_voice_template_control(state,&captured,control,source,source_size,result);
    if(error)return error;
    *environment=captured;return AUDIO_VOICE_OK;
}
