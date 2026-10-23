#include "gool_native_model_prepare.h"
static int fail(gool_native_model_prepare *o,int error){o->status=error;return error;}
static int stable(gool_native_model_prepare *o) {
    gool_native_actor *a=o->actor;
    if(!a||!a->attached||a->enabled||a->executing||a->error||!o->bank->open||o->bank->closing||
       a->registry->models!=o->bank||!a->tick_source||*a->tick_source!=o->tick)
        return GOOL_NATIVE_MODEL_INVALID;
    gool_domain *d=a->registry->domain;uint32_t value;
    int error=gool_domain_read32(d,0x80068eecu,&value);if(error)return error;
    if(value!=(uint32_t)o->tick)return GOOL_NATIVE_MODEL_INVALID;
    error=gool_domain_read32(d,a->node+0xe8u,&value);if(error)return error;
    if(value!=o->descriptor)return GOOL_NATIVE_MODEL_INVALID;
    error=gool_domain_read32(d,a->node+0xecu,&value);if(error)return error;
    if(value!=o->frame_word)return GOOL_NATIVE_MODEL_INVALID;
    uint8_t kind;uint16_t limit;
    error=gool_domain_read8(d,o->descriptor,&kind);if(error)return error;
    error=gool_domain_read16(d,o->descriptor+2u,&limit);if(error)return error;
    error=gool_domain_read32(d,o->descriptor+4u,&value);if(error)return error;
    if(kind!=1||limit!=o->frame_limit||value!=o->descriptor_flags)return GOOL_NATIVE_MODEL_INVALID;
    for(uint32_t i=0;i<3;i++) {
        error=gool_domain_read32(d,o->scale+i*4u,&value);if(error)return error;
        if(value!=o->scale_words[i])return GOOL_NATIVE_MODEL_INVALID;
    }
    for(uint32_t i=0;i<2;i++) {
        error=gool_domain_read32(d,o->descriptor+(i?0xcu:0x10u),&value);if(error)return error;
        if(value!=o->reference[i])return GOOL_NATIVE_MODEL_INVALID;
    }
    return 0;
}
static int release(gool_native_model_prepare *o) {
    for(uint32_t i=0;i<2;i++)if(o->retained[i]) {
        int error=model_resources_release(o->bank,o->leases[i]);if(error)return error;
        o->retained[i]=0;
    }
    if(o->actor_retained) {
        if(!o->actor||!o->actor->attached||o->actor->enabled||o->actor->executing||
           !o->actor->call_references)return GOOL_NATIVE_ACTOR_BUSY;
        o->actor->call_references--;o->actor_retained=0;
    }
    return 0;
}
int gool_native_model_prepare_begin(gool_native_model_prepare *o,gool_native_actor *a,uint32_t scale) {
    if(!o||o->active||o->actor_retained||o->retained[0]||o->retained[1]||!a||!a->attached||
       a->enabled||a->executing||a->error||!a->registry->models||!a->tick_source||
       a->call_references==UINT32_MAX||!scale||(scale&3u)||scale>UINT32_MAX-8u)
        return GOOL_NATIVE_MODEL_INVALID;
    gool_domain *d=a->registry->domain;uint32_t descriptor,word,tick,refs[2],flags,scales[3];
    uint8_t kind;uint16_t limit;
    int error=gool_domain_read32(d,a->node+0xe8u,&descriptor);if(error)return error;
    if(!descriptor||(descriptor&3u)||descriptor>UINT32_MAX-0x10u)return GOOL_NATIVE_MODEL_INVALID;
    error=gool_domain_read8(d,descriptor,&kind);if(error)return error;
    if(kind!=1)return GOOL_NATIVE_MODEL_INVALID;
    error=gool_domain_read16(d,descriptor+2u,&limit);if(error)return error;
    error=gool_domain_read32(d,descriptor+4u,&flags);if(error)return error;
    for(uint32_t i=0;i<3;i++) {
        error=gool_domain_read32(d,scale+i*4u,&scales[i]);if(error)return error;
    }
    error=gool_domain_read32(d,a->node+0xecu,&word);if(error)return error;
    error=gool_domain_read32(d,descriptor+0x10u,&refs[0]);if(error)return error;
    error=gool_domain_read32(d,descriptor+0xcu,&refs[1]);if(error)return error;
    if(!refs[0]||!refs[1])return GOOL_NATIVE_MODEL_INVALID;
    error=gool_domain_read32(d,0x80068eecu,&tick);if(error)return error;
    if(tick!=(uint32_t)*a->tick_source||!a->registry->models->open||a->registry->models->closing)
        return GOOL_NATIVE_MODEL_INVALID;
    *o=(gool_native_model_prepare){.actor=a,.bank=a->registry->models,.descriptor=descriptor,
        .frame_word=word,.scale=scale,.tick=*a->tick_source,.reference={refs[0],refs[1]},
        .descriptor_flags=flags,.frame_limit=limit,.scale_words={scales[0],scales[1],scales[2]},
        .active=1,.status=GOOL_NATIVE_MODEL_PENDING,.actor_retained=1};
    a->call_references++;return o->status;
}
static int acquire(gool_native_model_prepare *o,uint32_t index) {
    uint32_t reference=o->reference[index],address=reference;
    int error;
    if(reference&1u)error=model_resources_acquire(o->bank,reference,&address,&o->leases[index]);
    else error=zdat_resources_acquire_address(o->bank,reference,&o->leases[index]);
    if(error) {
        if(!(reference&1u)||(error!=NSF_RESOURCE_MISSING&&error!=ZDAT_RESOURCE_BUSY))return error;
        if(!o->requested[index]) {
            error=model_resources_request(o->bank,reference);if(error)return error;
            o->requested[index]=1;
        }
        return GOOL_NATIVE_MODEL_PENDING;
    }
    o->retained[index]=1;uint32_t type;
    error=gool_domain_read32(o->bank->domain,address+8u,&type);if(error)return error;
    return type==index+1u?0:GOOL_NATIVE_MODEL_INVALID;
}
int gool_native_model_prepare_step(gool_native_model_prepare *o,uint32_t budget) {
    if(!o||!o->actor)return GOOL_NATIVE_MODEL_INVALID;
    if(!o->active||o->status<0||!budget)return o->status;
    while(budget--) {
        int error=stable(o);if(error)return fail(o,error);
        if(o->phase==0) {
            int pending=0;
            for(uint32_t i=0;i<2;i++)if(!o->retained[i]) {
                error=acquire(o,i);if(error<0)return fail(o,error);
                if(error)pending=1;
            }
            if(pending) {
                error=model_resources_poll(o->bank);if(error<0)return fail(o,error);
                /* A worker continuation yields after one poll, including when
                 * it just completed. No spinning or manufactured frame time. */
                return GOOL_NATIVE_MODEL_PENDING;
            }
            o->phase=1;
        } else if(o->phase==1) {
            error=gool_native_actor_prepare_svtx(o->actor,o->scale);if(error)return fail(o,error);
            o->phase=2;
        } else if(o->phase==2) {
            error=release(o);if(error)return fail(o,error);
            o->active=0;o->status=0;return 0;
        } else return fail(o,GOOL_NATIVE_MODEL_INVALID);
    }
    return GOOL_NATIVE_MODEL_PENDING;
}
int gool_native_model_prepare_close(gool_native_model_prepare *o) {
    if(!o)return GOOL_NATIVE_MODEL_INVALID;
    int error=release(o);if(error)return error;
    *o=(gool_native_model_prepare){0};return 0;
}
