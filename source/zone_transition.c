#include "zone_transition.h"

static int address(uint32_t base,uint32_t offset,uint32_t *out) {
    if((base&3u)||!base||offset>UINT32_MAX-base)return ZONE_TRANSITION_INVALID;
    *out=base+offset;return 0;
}
static int read_at(const zone_transition_services *s,uint32_t base,uint32_t offset,uint32_t *value) {
    uint32_t at;int error=address(base,offset,&at);if(error)return error;
    error=s->read(s->context,at,value);
    return error>0?ZONE_TRANSITION_INVALID:error;
}
static int write_at(const zone_transition_services *s,uint32_t base,uint32_t offset,uint32_t value) {
    uint32_t at;int error=address(base,offset,&at);if(error)return error;
    error=s->write(s->context,at,value);
    return error>0?ZONE_TRANSITION_INVALID:error;
}
static int finish(zone_transition_cursor *c,int status) {
    c->active=0;c->status=status;return status;
}
int zone_transition_contains_begin(zone_transition_cursor *c,uint32_t entry,
    uint32_t candidate,uint32_t scan_limit,const zone_transition_services *s) {
    if(!c||c->active||!s||!s->read||!scan_limit)return ZONE_TRANSITION_INVALID;
    *c=(zone_transition_cursor){.services=*s,.entry=entry,.candidate=candidate,
        .scan_limit=scan_limit,.active=1,.status=ZONE_TRANSITION_BUDGET};
    return ZONE_TRANSITION_BUDGET;
}
int zone_transition_contains_step(zone_transition_cursor *c,uint32_t budget) {
    if(!c)return ZONE_TRANSITION_INVALID;
    if(!c->active)return c->status;
    while(budget--) {
        int error;
        if(c->phase==0) {
            error=read_at(&c->services,c->entry,0x10,&c->header);
            if(error)return finish(c,error);
            error=read_at(&c->services,c->header,0x190,&c->count);
            if(error)return finish(c,error);
            if(!c->count||(c->count&0x80000000u))return finish(c,ZONE_TRANSITION_DONE);
            if(c->count>c->scan_limit||c->count>(UINT32_MAX-0x194u)/4u)
                return finish(c,ZONE_TRANSITION_LIMIT);
            c->phase=1;
        } else if(c->phase==1) {
            error=read_at(&c->services,c->candidate,4,&c->candidate_eid);
            if(error)return finish(c,error);
            c->phase=2;
        } else {
            uint32_t reference,eid;
            error=read_at(&c->services,c->header,0x194+c->index*4,&reference);
            if(error)return finish(c,error);
            if(reference&1u)eid=reference;
            else {
                error=read_at(&c->services,reference,4,&eid);
                if(error)return finish(c,error);
            }
            if(eid==c->candidate_eid) {
                c->contains=1;return finish(c,ZONE_TRANSITION_DONE);
            }
            if(++c->index==c->count)return finish(c,ZONE_TRANSITION_DONE);
        }
    }
    return ZONE_TRANSITION_BUDGET;
}
int zone_transition_enter(uint32_t entry,const zone_transition_services *s) {
    if(!s||!s->read||!s->write)return ZONE_TRANSITION_INVALID;
    uint32_t header,flags;int error=read_at(s,entry,0x10,&header);if(error)return error;
    error=read_at(s,header,0x2bc,&flags);if(error)return error;
    if(flags&1u)return 0;
    error=write_at(s,ZONE_TRANSITION_CHAIN_HEAD,0,0);if(error)return error;
    error=write_at(s,ZONE_TRANSITION_CHAIN_SOURCE,0,0);if(error)return error;
    error=write_at(s,ZONE_TRANSITION_CHAIN_HEIGHT,0,0x19000);if(error)return error;
    error=read_at(s,header,0x2bc,&flags);if(error)return error;
    return write_at(s,header,0x2bc,flags|1u);
}

static int exit_finish(zone_exit_cursor *c,int status) {
    c->active=0;c->status=status;return status;
}
static int exit_status(int status) {
    return status>ZONE_TRANSITION_BUDGET?ZONE_TRANSITION_INVALID:status;
}
int zone_exit_begin(zone_exit_cursor *c,uint32_t entry,const zone_exit_services *s) {
    if(!c||c->active||c->entry_lease||c->player_lease||c->retirement_owned||
       !entry||(entry&3u)||!s||!s->memory.read||!s->memory.write||
       !s->retain||!s->release||!s->retire_begin||!s->retire_step||!s->retire_cancel)
        return ZONE_TRANSITION_INVALID;
    uint64_t lease=0;int error=s->retain(s->memory.context,entry,&lease);
    if(error)return error>0?ZONE_TRANSITION_INVALID:error;
    if(!lease)return ZONE_TRANSITION_INVALID;
    *c=(zone_exit_cursor){.services=*s,.entry=entry,.entry_lease=lease,
        .active=1,.status=ZONE_TRANSITION_BUDGET};
    return ZONE_TRANSITION_BUDGET;
}
int zone_exit_step(zone_exit_cursor *c,uint32_t budget) {
    if(!c)return ZONE_TRANSITION_INVALID;
    if(!c->active)return c->status;
    zone_exit_services *s=&c->services;zone_transition_services *m=&s->memory;
    while(budget--) {
        int error;
        switch(c->phase) {
        case 0:
            error=read_at(m,0x80060a90u,0,&c->player);if(error)return exit_finish(c,error);
            error=s->retain(m->context,c->player,&c->player_lease);
            if(error)return exit_finish(c,error>0?ZONE_TRANSITION_INVALID:error);
            if(!c->player_lease)return exit_finish(c,ZONE_TRANSITION_INVALID);
            error=read_at(m,c->player,0x18,&c->player_zone);if(error)return exit_finish(c,error);
            error=read_at(m,c->entry,0x10,&c->header);if(error)return exit_finish(c,error);
            c->phase=1;break;
        case 1:
            if(c->player_zone==c->entry) {
                uint32_t replacement;
                error=read_at(m,0x80065d60u,0,&replacement);if(error)return exit_finish(c,error);
                error=write_at(m,c->player,0x18,replacement);if(error)return exit_finish(c,error);
            }
            c->phase=2;break;
        case 2:
            error=read_at(m,c->header,0x2bc,&c->flags);if(error)return exit_finish(c,error);
            if(!(c->flags&1u))return exit_finish(c,ZONE_TRANSITION_DONE);
            c->phase=3;break;
        case 3:
            /* Close must quiesce a provider even when begin fails after its
             * first real effect. A failed callback is never begun again. */
            c->retirement_owned=1;
            error=exit_status(s->retire_begin(m->context,c->entry));
            if(error<0)return exit_finish(c,error);
            c->phase=error==ZONE_TRANSITION_DONE?5u:4u;
            if(error==ZONE_TRANSITION_BUDGET)return ZONE_TRANSITION_BUDGET;
            break;
        case 4:
            error=exit_status(s->retire_step(m->context,1));
            if(error<0)return exit_finish(c,error);
            if(error==ZONE_TRANSITION_BUDGET)return ZONE_TRANSITION_BUDGET;
            c->phase=5;break;
        case 5:
            error=s->retire_cancel(m->context);
            if(error)return exit_finish(c,error>0?ZONE_TRANSITION_INVALID:error);
            c->retirement_owned=0;c->phase=6;break;
        case 6:
            error=read_at(m,c->header,0x2bc,&c->flags);if(error)return exit_finish(c,error);
            c->phase=7;break;
        case 7:
            error=write_at(m,c->header,0x2bc,c->flags&~3u);if(error)return exit_finish(c,error);
            return exit_finish(c,ZONE_TRANSITION_DONE);
        default:return exit_finish(c,ZONE_TRANSITION_INVALID);
        }
    }
    return ZONE_TRANSITION_BUDGET;
}
int zone_exit_close(zone_exit_cursor *c) {
    if(!c)return ZONE_TRANSITION_INVALID;
    zone_exit_services *s=&c->services;int error;
    c->active=0;
    if(c->retirement_owned) {
        error=s->retire_cancel(s->memory.context);
        if(error)return error>0?ZONE_TRANSITION_INVALID:error;
        c->retirement_owned=0;
    }
    if(c->player_lease) {
        error=s->release(s->memory.context,c->player_lease);
        if(error)return error>0?ZONE_TRANSITION_INVALID:error;
        c->player_lease=0;
    }
    if(c->entry_lease) {
        error=s->release(s->memory.context,c->entry_lease);
        if(error)return error>0?ZONE_TRANSITION_INVALID:error;
        c->entry_lease=0;
    }
    *c=(zone_exit_cursor){0};return 0;
}
