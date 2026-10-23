#include "audio_native.h"

static int address(gool_native_registry *r,uint32_t node,uint32_t operand,
    const audio_native_services *s,uint32_t *out) {
    operand&=0xfffu;
    if((operand&0xe00u)==0xe00u) {
        if(operand==0xe1fu) {
            uint32_t sp;int error=gool_domain_read32(r->domain,node+0xbcu,&sp);if(error)return error;
            if(sp<4u)return GOOL_VM_STACK;
            uint32_t next=sp-4u;
            for(uint32_t i=0;i<r->count;i++) {
                gool_native_actor *a=r->actors[i];
                if(a&&a->attached&&a->node==node&&
                   (next<a->stack_address||(uint64_t)sp>(uint64_t)a->stack_address+a->stack_words*4u))
                    return GOOL_VM_STACK;
            }
            error=gool_domain_write32(r->domain,node+0xbcu,next);if(error)return error;
            *out=next;return 0;
        }
        *out=node+0x40u+(operand&0x1ffu)*4u;return 0;
    }
    if(!s||!s->address)return AUDIO_VOICE_SERVICE_REQUIRED;
    int error=s->address(s->context,node,operand,out);
    return error>0?AUDIO_VOICE_INVALID:error;
}

int audio_native_reset(gool_native_registry *r,const audio_native_services *s) {
    if(!r||!r->domain||!s||!s->voice_template||!s->environment)return AUDIO_VOICE_INVALID;
    return audio_voice_template_reset_native(s->voice_template,s->environment,r->domain,gool_domain_read);
}

static int template_control(gool_native_registry *r,const audio_native_services *s,
    const AudioVoiceControl *c,uint32_t source,int32_t *result) {
    if(!s||!s->voice_template||!s->environment||c->selector==2||c->selector==14)
        return AUDIO_VOICE_SERVICE_REQUIRED;
    unsigned count=c->selector==3?12:(c->selector==1||c->selector==7?2:
        (c->selector==4||c->selector==12?1:(c->selector==9?0:4)));
    if(c->selector==13&&!source)count=0;
    uint8_t bytes[12];
    for(unsigned i=0;i<count;i++) {
        if(source>UINT32_MAX-i)return AUDIO_VOICE_INVALID;
        int error=gool_domain_read8(r->domain,source+i,&bytes[i]);if(error)return error;
    }
    AudioVoiceControl template=*c;template.target_member=0;
    return audio_voice_template_control_native(s->voice_template,s->environment,&template,
        source?bytes:NULL,count,result,r->domain,gool_domain_read);
}

int audio_native_apply(gool_native_registry *r,uint32_t node,uint32_t instruction,
    const audio_native_services *s) {
    uint32_t op=instruction>>24;
    if(!r||!r->domain||!r->actors||!node||(node&3u)||node>UINT32_MAX-0x83cu||
       (op!=0x41&&op!=0x42))return AUDIO_VOICE_INVALID;
    uint32_t source;int32_t result;int error;
    if(op==0x41) {
        uint32_t volume;uint16_t raw;
        error=address(r,node,instruction>>12,s,&volume);if(error)return error;
        error=address(r,node,instruction,s,&source);if(error)return error;
        error=gool_domain_read16(r->domain,volume,&raw);if(error)return error;
        if(!s||!s->play)return AUDIO_VOICE_SERVICE_REQUIRED;
        int16_t signed_volume=raw<=INT16_MAX?(int16_t)raw:(int16_t)(-1-(int32_t)(UINT16_MAX-raw));
        error=s->play(s->context,node,source,signed_volume,&result);
        if(error)return error<0?error:AUDIO_VOICE_INVALID;
        return gool_native_registry_write(r,node+0x12cu,(uint32_t)result);
    }
    AudioVoiceControl control;
    error=audio_voice_decode_control(instruction,&control);if(error)return error;
    error=address(r,node,control.source_operand,s,&source);if(error)return error;
    uint32_t target=0;
    if(control.target_member) {
        uint32_t target_address;
        error=address(r,node,0xe00u|control.target_member,s,&target_address);if(error)return error;
        error=gool_domain_read32(r->domain,target_address,&target);if(error)return error;
    }
    if(!target)error=template_control(r,s,&control,source,&result);
    else error=AUDIO_VOICE_SERVICE_REQUIRED;
    if(error==AUDIO_VOICE_SERVICE_REQUIRED&&s&&s->control)
        error=s->control(s->context,target,control.selector|control.options,source,node,&result);
    if(error)return error<0?error:AUDIO_VOICE_INVALID;
    return gool_native_registry_write(r,node+0xd4u,(uint32_t)result);
}
