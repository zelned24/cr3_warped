#include "gool_native_activation_path.h"

int gool_native_activation_path_begin(gool_native_activation_path *o,
    gool_domain *domain,uint32_t gp,uint32_t capacity,uint32_t database,
    uint32_t position,uint32_t direction,const gool_native_activation_path_services *s) {
    if(!o||o->opened||!domain||!gp||(gp&3u)||gp>UINT32_MAX-0x21fu||!capacity||
       (uint64_t)0x80067a38u+(uint64_t)capacity*12u>UINT64_C(0x100000000)||
       !s||!s->query_begin||!s->query_step||!s->query_cancel)
        return GOOL_NATIVE_ACTIVATION_INVALID;
    gool_native_activation_path_services services=*s;
    *o=(gool_native_activation_path){.services=services,.domain=domain,.gp=gp,
        .capacity=capacity,.database=database,.position=position,.direction=direction,
        .opened=1,.status=GOOL_NATIVE_ACTIVATION_PENDING};
    return o->status;
}

int gool_native_activation_path_step(gool_native_activation_path *o,uint32_t budget) {
    if(!o||!o->opened)return GOOL_NATIVE_ACTIVATION_INVALID;
    if(o->status!=GOOL_NATIVE_ACTIVATION_PENDING)return o->status;
    while(budget--) {
        int error;
        switch(o->phase) {
        case 0:case 4:
            if(!o->query_owned) {
                uint32_t key=o->direction==2u?0x13bu:0x13cu;
                uint32_t position=o->position+(o->direction==2u?0xffffff00u:0x100u);
                if(o->phase==4){key=o->direction==2u?0x13cu:0x13bu;position=o->position;}
                o->query_owned=1;o->query_completed=0;
                error=o->services.query_begin(o->services.context,o->database,key,position,
                                              &o->list,&o->count);
            } else error=o->services.query_step(o->services.context,1);
            if(error==GOOL_NATIVE_ACTIVATION_PENDING)return error;
            if(error)return o->status=error<0?error:GOOL_NATIVE_ACTIVATION_INVALID;
            o->query_completed=1;
            if(!o->list||!o->count){o->phase+=3u;break;}
            error=gool_native_activation_register_begin(&o->registration,o->domain,o->gp,
                o->capacity,o->phase==0?0x13bu:0x13cu,o->list,o->count);
            if(error!=GOOL_NATIVE_ACTIVATION_PENDING)
                return o->status=error?error:GOOL_NATIVE_ACTIVATION_INVALID;
            o->phase++;break;
        case 1:case 5:
            error=gool_native_activation_step(&o->registration,1);
            if(error==GOOL_NATIVE_ACTIVATION_PENDING)break;
            if(error)return o->status=error;
            o->phase++;break;
        case 2:case 6:
            error=gool_native_activation_close(&o->registration);
            if(error)return o->status=error;
            o->phase++;break;
        case 3:case 7:
            error=o->services.query_cancel(o->services.context);
            if(error)return o->status=error<0?error:GOOL_NATIVE_ACTIVATION_INVALID;
            o->query_owned=0;o->query_completed=0;
            if(o->phase==7)return o->status=GOOL_NATIVE_ACTIVATION_DONE;
            o->phase=4;break;
        default:return o->status=GOOL_NATIVE_ACTIVATION_INVALID;
        }
    }
    return GOOL_NATIVE_ACTIVATION_PENDING;
}

int gool_native_activation_path_close(gool_native_activation_path *o) {
    if(!o)return GOOL_NATIVE_ACTIVATION_INVALID;
    if(!o->opened)return 0;
    int error=gool_native_activation_close(&o->registration);
    if(error)return error;
    if(o->query_owned) {
        error=o->services.query_cancel(o->services.context);
        if(error)return error<0?error:GOOL_NATIVE_ACTIVATION_INVALID;
    }
    *o=(gool_native_activation_path){0};return 0;
}
