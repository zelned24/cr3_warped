#include "gool_native_zone_transition.h"

static int read_word(gool_native_zone_transition *o,uint32_t address,uint32_t *value) {
    return gool_native_registry_read(o->registry,address,value);
}
static int write_word(gool_native_zone_transition *o,uint32_t address,uint32_t value) {
    return gool_native_registry_write(o->registry,address,value);
}
static int contains_read(void *context,uint32_t address,uint32_t *value) {
    return read_word(context,address,value);
}
static int contains_write(void *context,uint32_t address,uint32_t value) {
    return write_word(context,address,value);
}
static int fail(gool_native_zone_transition *o,int error) {o->status=error;return error;}
static int complete(gool_native_zone_transition *o,uint32_t result) {
    o->native_result=result;o->continuation=0x80021e5cu;return o->status=0;
}
static int32_t signed_word(uint32_t value) {
    return value<=INT32_MAX?(int32_t)value:-1-(int32_t)(UINT32_MAX-value);
}
static uint32_t shift8(uint32_t value) {
    return (value>>8)|((value&0x80000000u)?0xff000000u:0);
}
/* Request arguments are set only before begin; suspension keeps their storage
 * and native result untouched until the same invocation has completed. */
static int invoke(gool_native_zone_transition *o,uint32_t address,uint32_t a0,
                  uint32_t a1,uint32_t a2,uint32_t a3,uint32_t stacked) {
    int error;
    if(!o->call_owned) {
        uint32_t count=4;
        switch(address) {
        case 0x8001440cu:case 0x8001ae64u:case 0x80021e60u:count=2;break;
        case 0x800142b4u:case 0x80022c9cu:case 0x8001ad68u:count=3;break;
        case 0x80024330u:case 0x8002437cu:case 0x8004f724u:count=1;break;
        case 0x80014468u:case 0x80012eacu:case 0x80025c0cu:count=0;break;
        }
        o->call=(gool_native_zone_transition_call){.address=address,.gp=o->gp,
            .arguments={a0,a1,a2,a3},.argument_count=count,
            .stack_arguments={0,address==0x8001a42cu||a1==0x176u?1u:
                    (address==0x80032df8u&&a1==0x1aau?3u:2u),0,0,0},.stack_count=stacked};
        o->call_owned=1;o->call_completed=0;
        error=o->services.call_begin(o->services.context,&o->call,&o->result);
    } else if(o->call_completed)error=0;
    else error=o->services.call_step(o->services.context,1);
    if(error)return error==1?1:(error<0?error:GOOL_NATIVE_ZONE_TRANSITION_INVALID);
    o->call_completed=1;
    error=o->services.call_cancel(o->services.context);
    if(error)return error<0?error:GOOL_NATIVE_ZONE_TRANSITION_INVALID;
    o->call_owned=0;o->call_completed=0;return 0;
}
int gool_native_zone_transition_begin(gool_native_zone_transition *o,
    uint32_t zone,uint32_t database,uint32_t position,uint32_t flags,uint32_t gp,
    const gool_native_zone_transition_services *s) {
    if(!o||o->opened||!s||!s->context||!s->registry||!s->registry->domain||!s->registry->zones||
       !s->registry->zones->open||s->registry->zones->closing||
       s->registry->zones->domain!=s->registry->domain||!s->neighbor_limit||!s->path_limit||
       !s->activation_capacity||
       !s->activation_query.query_begin||!s->activation_query.query_step||
       !s->activation_query.query_cancel||
       !s->call_begin||!s->call_step||!s->call_cancel||!gp||(gp&3u)||
       s->retirement.domain!=s->registry->domain||!s->retirement.max_nodes||
       !s->retirement.signal||!s->retirement.retire||!s->retirement.cancel)
        return GOOL_NATIVE_ZONE_TRANSITION_INVALID;
    gool_native_zone_transition_services captured=*s;
    *o=(gool_native_zone_transition){.services=captured,.registry=s->registry,
        .bank=s->registry->zones,.domain=s->registry->domain,.zone=zone,.database=database,
        .position=position,.flags=flags,.gp=gp,.changes=(flags&8u)?7u:0u,
        .notify=1,.opened=1,.status=GOOL_NATIVE_ZONE_TRANSITION_PENDING};
    return o->status;
}
int gool_native_zone_transition_step(gool_native_zone_transition *o,uint32_t budget) {
    if(!o||!o->opened)return GOOL_NATIVE_ZONE_TRANSITION_INVALID;
    if(o->status!=GOOL_NATIVE_ZONE_TRANSITION_PENDING)return o->status;
    if(o->registry->domain!=o->domain||o->registry->zones!=o->bank||
       o->bank->domain!=o->domain||!o->bank->open||o->bank->closing)
        return fail(o,GOOL_NATIVE_ZONE_TRANSITION_INVALID);
    while(budget--) {
        int error;uint32_t value;
        switch(o->phase) {
        case 0:
            if(!o->database||!o->zone){o->phase=70;break;}
            error=zdat_resources_acquire_address(o->bank,o->zone,&o->zone_lease);
            if(error)return fail(o,error);
            o->zone_retained=1;
            error=read_word(o,0x80065d5cu,&o->previous);if(error)return fail(o,error);
            if(o->previous==o->zone){o->phase=10;break;}
            o->changes|=1u;
            if(!o->previous){o->phase=10;break;}
            o->notify=o->flags&2u;
            if(o->flags&16u){o->phase=10;break;}
            error=zdat_resources_acquire_address(o->bank,o->previous,&o->previous_lease);
            if(error)return fail(o,error);
            o->previous_retained=1;
            error=read_word(o,o->previous+0x10u,&o->previous_header);if(error)return fail(o,error);
            error=write_word(o,0x80065d60u,o->zone);if(error)return fail(o,error);
            error=read_word(o,o->previous_header+0x190u,&o->neighbor_count);if(error)return fail(o,error);
            o->phase=signed_word(o->neighbor_count)>0?1u:10u;break;
        case 1:
            if(o->index>=o->services.neighbor_limit||o->index>(UINT32_MAX-0x194u)/4u)
                return fail(o,GOOL_NATIVE_ZONE_TRANSITION_LIMIT);
            error=invoke(o,0x8001440cu,o->previous_header+0x194u+o->index*4u,0,0,0,0);
            if(error)return error==1?1:fail(o,error);
            o->phase=o->result?2u:8u;break;
        case 2:
            error=invoke(o,0x800142b4u,o->previous_header+0x194u+o->index*4u,1,0,0,0);
            if(error)return error==1?1:fail(o,error);
            o->candidate=o->result;
            error=zdat_resources_acquire_address(o->bank,o->candidate,&o->candidate_lease);
            if(error)return fail(o,error);
            o->candidate_retained=1;
            {zone_transition_services s={.context=o,.read=contains_read};
             error=zone_transition_contains_begin(&o->contains,o->zone,o->candidate,o->services.neighbor_limit,&s);}
            if(error!=1)return fail(o,error?error:GOOL_NATIVE_ZONE_TRANSITION_INVALID);
            o->phase=3;break;
        case 3:
            error=zone_transition_contains_step(&o->contains,1);
            if(error==1)break;
            if(error)return fail(o,error);
            if(o->contains.contains){o->phase=7;break;}
            error=gool_native_zone_exit_begin(&o->exit,o->registry,o->candidate,&o->services.retirement);
            if(error!=1)return fail(o,error?error:GOOL_NATIVE_ZONE_TRANSITION_INVALID);
            o->phase=4;break;
        case 4:
            error=gool_native_zone_exit_step(&o->exit,1);
            if(error==1)return 1;
            if(error)return fail(o,error);
            o->phase=5;break;
        case 5:
            error=gool_native_zone_exit_close(&o->exit);if(error)return fail(o,error);
            o->phase=7;break;
        case 7:
            error=zdat_resources_release(o->bank,o->candidate_lease);if(error)return fail(o,error);
            o->candidate_retained=0;o->phase=8;break;
        case 8:
            error=read_word(o,o->previous_header+0x190u,&o->neighbor_count);if(error)return fail(o,error);
            o->index++;
            o->phase=signed_word(o->index)<signed_word(o->neighbor_count)?1u:10u;break;
        case 10:
            error=read_word(o,0x80065d64u,&value);if(error)return fail(o,error);
            if(o->database!=value||(o->flags&8u)){o->changes|=2u;o->phase=11;}
            else {
                error=read_word(o,0x80065d74u,&o->maximum);if(error)return fail(o,error);
                error=read_word(o,0x80065d7cu,&o->samples);if(error)return fail(o,error);
                error=read_word(o,0x80065d80u,&o->property);if(error)return fail(o,error);
                o->phase=13;
            }break;
        case 11:
            error=invoke(o,0x80032edcu,o->database,0x27fu,0,2,0);
            if(error)return error==1?1:fail(o,error);
            o->samples=o->result;o->maximum=(o->samples-1u)<<8;o->phase=12;break;
        case 12:
            error=invoke(o,0x80032df8u,o->database,0x29u,0,0,3);
            if(error)return error==1?1:fail(o,error);
            o->property=o->result;o->phase=13;break;
        case 13:
            error=read_word(o,0x80065d80u,&o->previous_property);if(error)return fail(o,error);
            if(o->property!=o->previous_property)o->changes|=0x10u;
            if(signed_word(o->position)<0)o->position=0;
            else if(signed_word(o->maximum)<signed_word(o->position))o->position=o->maximum;
            o->phase=14;break;
        case 14:
            error=read_word(o,0x80065d70u,&value);if(error)return fail(o,error);
            if(o->position!=value)o->changes|=4u;
            error=read_word(o,0x80065d78u,&value);if(error)return fail(o,error);
            o->integer_position=shift8(o->position);
            if(o->integer_position!=value)o->changes|=8u;
            o->phase=15;break;
        case 15: {
            uint32_t copy[3];
            for(uint32_t i=0;i<3;i++) {
                error=read_word(o,0x80065cfcu+i*4u,&copy[i]);if(error)return fail(o,error);
            }
            for(uint32_t i=0;i<3;i++) {
                error=write_word(o,0x80065da8u+i*4u,copy[i]);if(error)return fail(o,error);
            }
            error=read_word(o,o->zone+0x10u,&value);if(error)return fail(o,error);
            error=read_word(o,value,&o->header_word);if(error)return fail(o,error);
            o->continuation=0x80021838u;o->phase=20;break;
        }
        case 20:
            if(!(o->changes&7u)){o->phase=65;break;}
            if(!o->header_word){o->phase=22;break;}
            error=invoke(o,0x80032710u,o->database,0x103u,0,0,5);
            if(error)return error==1?1:fail(o,error);
            o->scratch[0]=o->result;o->phase=21;break;
        case 21:
            error=invoke(o,0x800142b4u,o->scratch[0],1,1,0,0);
            if(error)return error==1?1:fail(o,error);
            o->data=o->result;o->phase=22;break;
        case 22:
            if(!(o->changes&2u)) {
                error=read_word(o,0x80065d78u,&o->current_index);if(error)return fail(o,error);
                o->phase=32;break;
            }
            error=gool_native_activation_reset_begin(&o->activation,o->domain,o->gp,
                                                     o->services.activation_capacity);
            if(error!=1)return fail(o,error?error:GOOL_NATIVE_ZONE_TRANSITION_INVALID);
            o->phase=221;break;
        case 221:
            error=gool_native_activation_step(&o->activation,1);
            if(error==1)break;
            if(error)return fail(o,error);
            error=gool_native_activation_close(&o->activation);if(error)return fail(o,error);
            /* Native reset's volatile V0 is unused before the next call. */
            o->phase=23;break;
        case 23:
            error=invoke(o,0x80014468u,0,0,0,0,0);
            if(error)return error==1?1:fail(o,error);
            o->phase=24;break;
        case 24:
            error=read_word(o,o->gp+0x240u,&value);if(error)return fail(o,error);
            error=read_word(o,o->gp+0x230u,&o->buffer);if(error)return fail(o,error);
            error=write_word(o,o->gp+0x230u,value);if(error)return fail(o,error);
            error=write_word(o,o->gp+0x240u,o->buffer);if(error)return fail(o,error);
            value=(o->samples>>1)|((o->samples&0x80000000u)?0x80000000u:0);
            o->direction=signed_word(o->integer_position)<signed_word(value)?2u:1u;
            o->phase=o->header_word?25u:26u;break;
        case 25:
            if(!o->call_owned) {
                uint32_t at=o->data+(o->direction==2u?0u:o->samples*4u)+0x10u;
                error=read_word(o,at,&o->scratch[0]);if(error)return fail(o,error);
                error=read_word(o,o->gp+0x22cu,&o->scratch[1]);if(error)return fail(o,error);
            }
            error=invoke(o,0x8002aa14u,o->scratch[0],o->scratch[1],o->buffer,o->direction==2u,0);
            if(error)return error==1?1:fail(o,error);
            error=write_word(o,o->gp+0x22cu,o->result);if(error)return fail(o,error);
            o->phase=26;break;
        case 26:
            o->current_index=o->direction==2u?0u:o->samples-1u;o->phase=27;break;
        case 27:
            error=invoke(o,0x80022c9cu,o->database,o->current_index<<8,o->direction,0,0);
            if(error)return error==1?1:fail(o,error);
            o->phase=28;break;
        case 28:case 37:
            error=gool_native_activation_path_begin(&o->activation_path,o->domain,o->gp,
                o->services.activation_capacity,o->database,o->current_index<<8,
                o->direction,&o->services.activation_query);
            if(error!=1)return fail(o,error?error:GOOL_NATIVE_ZONE_TRANSITION_INVALID);
            o->phase=o->phase==28u?281u:371u;break;
        case 281:case 371:
            error=gool_native_activation_path_step(&o->activation_path,1);
            if(error==1)break;
            if(error)return fail(o,error);
            error=gool_native_activation_path_close(&o->activation_path);
            if(error)return fail(o,error);
            if(o->phase==281u){o->phase=32;break;}
            o->difference+=o->direction==2u?UINT32_MAX:1u;
            o->phase=o->difference?33u:42u;break;
        case 32:
            o->difference=o->integer_position-o->current_index;
            o->list=o->data+o->current_index*4u;o->phase=o->difference?33u:42u;break;
        case 33:
            if(o->path_steps>=o->services.path_limit)return fail(o,GOOL_NATIVE_ZONE_TRANSITION_LIMIT);
            o->path_steps++;
            error=read_word(o,o->gp+0x240u,&value);if(error)return fail(o,error);
            error=read_word(o,o->gp+0x230u,&o->buffer);if(error)return fail(o,error);
            error=write_word(o,o->gp+0x230u,value);if(error)return fail(o,error);
            error=write_word(o,o->gp+0x240u,o->buffer);if(error)return fail(o,error);
            o->direction=signed_word(o->difference)>0?2u:1u;
            if(o->direction==2u){o->list+=4u;o->current_index++;}
            o->phase=o->header_word?34u:35u;break;
        case 34:
            if(!o->call_owned) {
                error=read_word(o,o->list+0x10u,&o->scratch[0]);if(error)return fail(o,error);
                error=read_word(o,o->gp+0x22cu,&o->scratch[1]);if(error)return fail(o,error);
            }
            error=invoke(o,0x8002aa14u,o->scratch[0],o->scratch[1],o->buffer,o->direction==2u,0);
            if(error)return error==1?1:fail(o,error);
            error=write_word(o,o->gp+0x22cu,o->result);if(error)return fail(o,error);
            o->phase=35;break;
        case 35:
            if(o->direction==1u){o->list-=4u;o->current_index--;}
            o->phase=36;break;
        case 36:
            error=invoke(o,0x80022c9cu,o->database,o->current_index<<8,o->direction,0,0);
            if(error)return error==1?1:fail(o,error);
            o->phase=37;break;
        case 42:
            if(!o->header_word){o->phase=44;break;}
            error=invoke(o,0x80032710u,o->database,0x103u,0,0,5);
            if(error)return error==1?1:fail(o,error);
            o->scratch[0]=o->result;o->phase=43;break;
        case 43:
            error=invoke(o,0x8001440cu,o->scratch[0],1,0,0,0);
            if(error)return error==1?1:fail(o,error);
            o->phase=44;break;
        case 44:
            if(!o->call_owned) {
                error=read_word(o,o->database+4u,&o->scratch[0]);if(error)return fail(o,error);
            }
            error=invoke(o,0x8001ae64u,o->scratch[0],0x13bu,0,0,0);
            if(error)return error==1?1:fail(o,error);
            o->phase=(o->changes&1u)?46u:52u;break;
        case 46:
            error=read_word(o,0x80068f3cu,&value);if(error)return fail(o,error);
            error=read_word(o,o->zone+0x10u,&o->header);if(error)return fail(o,error);
            if(value==0x600u&&!(o->flags&2u))o->notify=0;
            o->phase=47;break;
        case 47:case 52: {
            const uint32_t offsets[7]={0xcc,0xd4,0xe0,0xe8,0xec,0xe4,0xf0};
            const uint32_t values[7]={o->zone,o->database,o->position,o->integer_position,
                                      o->samples,o->maximum,o->property};
            for(uint32_t i=0;i<7;i++) {
                error=write_word(o,0x80065c90u+offsets[i],values[i]);if(error)return fail(o,error);
            }
            o->phase=o->phase==47u?48u:55u;break;
        }
        case 48:
            error=invoke(o,0x80021e60u,o->header+0x2c0u,o->flags,0,0,0);
            if(error)return error==1?1:fail(o,error);
            o->phase=49;break;
        case 49:
            if(o->notify) {
                error=invoke(o,0x80012eacu,0,0,0,0,0);
                if(error)return error==1?1:fail(o,error);
            }
            o->phase=50;break;
        case 50:
            error=read_word(o,o->header+0x190u,&o->neighbor_count);if(error)return fail(o,error);
            o->index=0;o->phase=signed_word(o->neighbor_count)>0?51u:55u;break;
        case 51:
            if(o->index>=o->services.neighbor_limit)return fail(o,GOOL_NATIVE_ZONE_TRANSITION_LIMIT);
            error=read_word(o,o->header+0x1d4u+o->index*4u,&value);if(error)return fail(o,error);
            o->phase=value&0x10u?511u:519u;break;
        case 511:
            error=invoke(o,0x8001440cu,o->header+0x194u+o->index*4u,0,0,0,0);
            if(error)return error==1?1:fail(o,error);
            o->phase=o->result?512u:519u;break;
        case 512:
            error=invoke(o,0x800142b4u,o->header+0x194u+o->index*4u,1,0,0,0);
            if(error)return error==1?1:fail(o,error);
            o->candidate=o->result;
            error=zdat_resources_acquire_address(o->bank,o->candidate,&o->candidate_lease);
            if(error)return fail(o,error);
            o->candidate_retained=1;
            error=read_word(o,o->candidate+0x10u,&value);if(error)return fail(o,error);
            error=read_word(o,value+0x2bcu,&value);if(error)return fail(o,error);
            o->phase=value&1u?515u:513u;break;
        case 513: {
            zone_transition_services s={o,contains_read,contains_write};
            error=zone_transition_enter(o->candidate,&s);if(error)return fail(o,error);
            o->phase=515;break;
        }
        case 515:
            error=zdat_resources_release(o->bank,o->candidate_lease);if(error)return fail(o,error);
            o->candidate_retained=0;o->phase=519;break;
        case 519:
            error=read_word(o,o->header+0x190u,&o->neighbor_count);if(error)return fail(o,error);
            o->index++;o->phase=signed_word(o->index)<signed_word(o->neighbor_count)?51u:55u;break;
        case 55:
            if(o->changes&0x10u) {
                error=invoke(o,0x80024330u,o->previous_property,0,0,0,0);
                if(error)return error==1?1:fail(o,error);
            }
            o->phase=56;break;
        case 56:
            if(!o->call_owned) {
                error=read_word(o,0x80065d8cu,&o->scratch[0]);if(error)return fail(o,error);
            }
            error=invoke(o,0x8002437cu,o->scratch[0],0,0,0,0);
            if(error)return error==1?1:fail(o,error);
            o->phase=57;break;
        case 57:
            if(!o->call_owned) {
                error=read_word(o,0x80065d64u,&o->scratch[0]);if(error)return fail(o,error);
                error=read_word(o,0x80065d70u,&o->scratch[1]);if(error)return fail(o,error);
            }
            error=invoke(o,0x80028740u,o->scratch[0],o->scratch[1],0x80065d14u,0x80065db4u,1);
            if(error)return error==1?1:fail(o,error);
            o->phase=58;break;
        case 58:
            if(!o->call_owned) {
                error=read_word(o,0x80065d64u,&o->scratch[0]);if(error)return fail(o,error);
                error=read_word(o,0x80065d70u,&o->scratch[1]);if(error)return fail(o,error);
            }
            error=invoke(o,0x80032df8u,o->scratch[0],0x1aau,o->scratch[1],0,3);
            if(error)return error==1?1:fail(o,error);
            error=write_word(o,0x80065d58u,o->result);if(error)return fail(o,error);
            o->phase=o->changes&3u?60u:63u;break;
        case 60:
            if(!o->call_owned) {
                error=read_word(o,0x80065d64u,&o->scratch[0]);if(error)return fail(o,error);
            }
            error=invoke(o,0x80032710u,o->scratch[0],0x176u,0,0,5);
            if(error)return error==1?1:fail(o,error);
            o->phase=61;break;
        case 61:case 621: {
            uint32_t sample=0xffffu;
            if(o->result) {
                uint16_t half;error=gool_domain_read16(o->domain,o->result,&half);if(error)return fail(o,error);
                sample=(half&0x8000u)?0xffff0000u|half:half;
            }
            error=write_word(o,o->phase==61u?0x80065d90u:0x80065d94u,sample);if(error)return fail(o,error);
            o->phase=o->phase==61u?62u:63u;break;
        }
        case 62:
            if(!o->call_owned) {
                error=read_word(o,0x80065d64u,&o->scratch[0]);if(error)return fail(o,error);
                error=read_word(o,0x80065d74u,&o->scratch[1]);if(error)return fail(o,error);
            }
            error=invoke(o,0x80032710u,o->scratch[0],0x176u,o->scratch[1],0,5);
            if(error)return error==1?1:fail(o,error);
            o->phase=621;break;
        case 63:
            if(!o->call_owned) {
                error=read_word(o,0x80065d5cu,&o->scratch[0]);if(error)return fail(o,error);
            }
            error=invoke(o,0x8001ae64u,o->scratch[0],UINT32_MAX,0,0,0);
            if(error)return error==1?1:fail(o,error);
            o->phase=65;break;
        case 65:case 66:
            for(uint32_t i=0;i<4;i++) {
                error=read_word(o,0x80065d14u+(o->phase==65u?0u:16u)+i*4u,&o->scratch[i]);
                if(error)return fail(o,error);
            }
            for(uint32_t i=0;i<4;i++) {
                error=write_word(o,0x80065cf0u+(o->phase==65u?0u:16u)+i*4u,o->scratch[i]);
                if(error)return fail(o,error);
            }
            o->phase++;break;
        case 67:
            error=read_word(o,0x80065d34u,&value);if(error)return fail(o,error);
            error=write_word(o,0x80065d10u,value);if(error)return fail(o,error);
            o->phase=68;break;
        case 68:
            error=invoke(o,0x80025c0cu,0,0,0,0,0);
            if(error)return error==1?1:fail(o,error);
            error=read_word(o,0x80065d54u,&value);if(error)return fail(o,error);
            if(value==o->result){o->phase=70;break;}
            error=write_word(o,0x80065d54u,o->result);if(error)return fail(o,error);
            o->scratch[0]=o->result;o->phase=69;break;
        case 69:
            error=invoke(o,0x8004f724u,o->scratch[0],0,0,0,0);
            if(error)return error==1?1:fail(o,error);
            o->phase=70;break;
        case 70:
            error=read_word(o,0x80060a90u,&o->player);if(error)return fail(o,error);
            error=write_word(o,0x80061a70u,0);if(error)return fail(o,error);
            if(o->player)return complete(o,0x80060000u);
            error=read_word(o,0x80061044u,&value);if(error)return fail(o,error);
            error=read_word(o,value+0x10u,&value);if(error)return fail(o,error);
            if(value==0x6396347fu)return complete(o,value);
            o->phase=71;break;
        case 71:
            error=invoke(o,0x8001a42cu,0x80068ed0u,0,0,0,2);
            if(error)return error==1?1:fail(o,error);
            o->phase=72;break;
        case 72:
            error=read_word(o,0x80069104u,&value);if(error)return fail(o,error);
            o->scratch[0]=value*24u;
            error=read_word(o,0x80060a90u,&o->player);if(error)return fail(o,error);
            error=read_word(o,0x80061044u,&value);if(error)return fail(o,error);
            o->scratch[0]+=value;o->phase=73;break;
        case 73: {
            uint32_t position[3];
            for(uint32_t i=0;i<3;i++) {
                error=read_word(o,o->scratch[0]+0x2e8u+i*4u,&position[i]);if(error)return fail(o,error);
            }
            for(uint32_t i=0;i<3;i++) {
                error=write_word(o,o->player+0x60u+i*4u,position[i]);if(error)return fail(o,error);
            }
            o->phase=74;break;
        }
        case 74:
            error=read_word(o,0x80060a90u,&o->player);if(error)return fail(o,error);
            error=read_word(o,0x80065d5cu,&value);if(error)return fail(o,error);
            error=write_word(o,o->player+0x18u,value);if(error)return fail(o,error);
            error=write_word(o,0x8006900cu,UINT32_MAX);if(error)return fail(o,error);
            o->phase=75;break;
        case 75:
            error=invoke(o,0x8002ff3cu,o->player,0x80065dd8u,1,0,0);
            if(error)return error==1?1:fail(o,error);
            return complete(o,o->result);
        default:return fail(o,GOOL_NATIVE_ZONE_TRANSITION_INVALID);
        }
    }
    return GOOL_NATIVE_ZONE_TRANSITION_PENDING;
}
int gool_native_zone_transition_close(gool_native_zone_transition *o) {
    if(!o)return GOOL_NATIVE_ZONE_TRANSITION_INVALID;
    if(!o->opened)return 0;
    int error;
    if(o->call_owned) {
        error=o->services.call_cancel(o->services.context);
        if(error)return error<0?error:GOOL_NATIVE_ZONE_TRANSITION_INVALID;
        o->call_owned=0;
    }
    error=gool_native_activation_path_close(&o->activation_path);if(error)return error;
    error=gool_native_zone_exit_close(&o->exit);if(error)return error;
    error=gool_native_activation_close(&o->activation);if(error)return error;
    if(o->candidate_retained) {
        error=zdat_resources_release(o->bank,o->candidate_lease);if(error)return error;
        o->candidate_retained=0;
    }
    if(o->previous_retained) {
        error=zdat_resources_release(o->bank,o->previous_lease);if(error)return error;
        o->previous_retained=0;
    }
    if(o->zone_retained) {
        error=zdat_resources_release(o->bank,o->zone_lease);if(error)return error;
        o->zone_retained=0;
    }
    *o=(gool_native_zone_transition){0};return 0;
}
