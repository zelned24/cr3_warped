#include "gool_slot.h"

static int end(gool_slot *c,int result){c->active=0;c->status=result;return result;}
static int status(int error){return error>0?GOOL_SLOT_INVALID:error;}
static int read_word(gool_slot *c,uint32_t address,uint32_t *out) {
    if(!address||(address&3u))return GOOL_SLOT_INVALID;
    return status(c->services.read(c->services.context,address,out));
}
static int write_word(gool_slot *c,uint32_t address,uint32_t value) {
    if(!address||(address&3u))return GOOL_SLOT_INVALID;
    return status(c->services.write(c->services.context,address,value));
}
int gool_slot_begin(gool_slot *c,const gool_admission_result *a,const gool_slot_services *s) {
    if(!c||c->active||!a||!a->admitted||a->native_result||!s||!s->read||!s->write||
       !s->search||!s->retire||!s->reparent||!s->max_reclaims||
       !s->protected_address||(s->protected_address&3u))return GOOL_SLOT_INVALID;
    *c=(gool_slot){.services=*s,.admission=*a,.active=1,.status=GOOL_SLOT_BUDGET};
    return GOOL_SLOT_BUDGET;
}
int gool_slot_step(gool_slot *c,uint32_t budget) {
    if(!c)return GOOL_SLOT_INVALID;
    if(!c->active)return c->status;
    while(budget--) {
        int error;uint32_t value;
        switch(c->phase) {
        case 0:
            if(c->admission.protected_slot) {
                error=read_word(c,0x80068e98,&c->slot);if(error)return end(c,error);
                error=write_word(c,c->services.protected_address,c->slot);if(error)return end(c,error);
                c->parent=0x80068ed0;c->phase=4;
            } else c->phase=1;
            break;
        case 1:
            error=read_word(c,0x80068ee0,&value);if(error)return end(c,error);
            error=read_word(c,0x80068ee0+(value==2?4u:0x4cu),&c->slot);if(error)return end(c,error);
            if(c->slot){c->parent=0x80068eb8;c->phase=4;}
            else {c->root_index=7;c->root=0x80068ed8;c->phase=2;}
            break;
        case 2:
            error=read_word(c,0x8006087c,&value);if(error)return end(c,error);
            error=status(c->services.search(c->services.context,c->root,0x80019cc0,
                                            value?0x420000u:0x20000u,&c->candidate));
            if(error)return end(c,error);
            if(c->candidate)c->phase=3;
            else if(c->root_index){c->root_index--;c->root-=8;}
            else {c->native_result=-22;return end(c,GOOL_SLOT_DONE);}
            break;
        case 3:
            if(c->reclaims==c->services.max_reclaims)return end(c,GOOL_SLOT_LIMIT);
            if(c->candidate&3u)return end(c,GOOL_SLOT_INVALID);
            error=status(c->services.retire(c->services.context,c->candidate,0));
            if(error)return end(c,error);
            c->reclaims++;c->phase=1;break;
        case 4:
            if(!c->slot||(c->slot&3u))return end(c,GOOL_SLOT_INVALID);
            error=status(c->services.reparent(c->services.context,c->slot,c->parent));
            if(error)return end(c,error);
            return end(c,GOOL_SLOT_DONE);
        default:return end(c,GOOL_SLOT_INVALID);
        }
    }
    return GOOL_SLOT_BUDGET;
}
