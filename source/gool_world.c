#include "gool_world.h"

uint32_t gool_world_tick(uint32_t counter) {
    return counter/34u;
}

gool_world_decision gool_world_admit(gool_world_input input) {
    gool_world_decision result={0};
    result.tick=gool_world_tick(input.counter);
    result.effective_request=input.requested_update;
    uint32_t admitted=input.world_flags&8u;
    if(admitted) {
        if(input.status_100&0x40000u)result.effective_request=1;
        if(((input.flags_ac&0x02000000u)||(input.status_100&0x8000u))&&
           (input.world_flags&0x8000u))admitted=1;
        else switch(input.category) {
            case 0x100:admitted=input.world_flags&0x20u;break;
            case 0x200:admitted=input.world_flags&0x100u;break;
            case 0x300:case 0x500:case 0x600:
                admitted=input.world_flags&0x80u;break;
            case 0x400:case 0x900:admitted=input.world_flags&0x400u;break;
            default:break;
        }
    }
    result.run_update=result.effective_request&&admitted;
    return result;
}

int gool_world_presentation_admit(gool_world_presentation_input input) {
    if(!input.model||(input.geometry_format&0xffu)==1u||
       (input.flags_ac&0x100u)||!(input.world_flags&4u))return 0;
    uint32_t admitted=0;
    /* Native1D97C delay slot sets admission1 only on this override route.
     * Ordinary category dispatch resets A0 in the1D99C delay slot. */
    if(((input.flags_ac&0x02000000u)||(input.status_100&0x8000u))&&
       (input.world_flags&0x4000u))admitted=1;
    else switch(input.category) {
        case 0x100:
            if((input.world_flags&0x10u)&&((input.engine_69034&1u)||
               input.engine_6087c||input.mode!=4u))admitted=1;
            break;
        case 0x200:
            admitted=input.world_flags&0x200u;
            if(!input.requested_update&&!(input.status_100&0x40000u)&&input.engine_68f28)
                admitted=0;
            break;
        case 0x300:case 0x500:case 0x600:
            admitted=input.world_flags&0x40u;break;
        case 0x400:case 0x900:
            admitted=input.world_flags&0x800u;break;
        default:break;
    }
    /* The common late gates only preserve existing admission. They cannot
     * enable an unknown/disabled category (including category200's delay slot). */
    if(!input.requested_update&&!(input.world_flags&0x08000000u)&&
       !(input.status_100&0x40000u))admitted=0;
    return admitted!=0;
}

int gool_world_step(void *object,const gool_world_input *input,
                    const gool_world_services *services) {
    if(!object||!input||!services||!services->bind_current||
       !services->stamp_update||!services->dispatch)return GOOL_WORLD_INVALID;
    int error=services->bind_current(services->context,object);
    if(error)return error;
    gool_world_decision decision=gool_world_admit(*input);
    if(decision.run_update) {
        error=services->stamp_update(services->context,object,decision.tick);
        if(error)return error;
    }
    return services->dispatch(services->context,object,&decision);
}
