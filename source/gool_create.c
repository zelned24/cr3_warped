#include "gool_create.h"
#include "gool_search.h"
#include "gool_tree.h"
#include <limits.h>

static int32_t signed_word(uint32_t value) {
    return value<=INT32_MAX?(int32_t)value:-1-(int32_t)(UINT32_MAX-value);
}
static int finish(gool_create *c,int result){c->active=0;c->status=result;return result;}
static int status(int error){return error>0?GOOL_CREATE_INVALID:error;}
static int rd(gool_create *c,uint32_t base,uint32_t offset,uint32_t *out) {
    if(!base||(base&3u)||offset>UINT32_MAX-base)return GOOL_CREATE_INVALID;
    return status(c->services.read(c->services.context,base+offset,out));
}
static int wr(gool_create *c,uint32_t base,uint32_t offset,uint32_t value) {
    if(!base||(base&3u)||offset>UINT32_MAX-base)return GOOL_CREATE_INVALID;
    return status(c->services.write(c->services.context,base+offset,value));
}
static int read_bridge(void *context,uint32_t address,uint32_t *value) {
    return rd(context,address,0,value);
}
static int write_bridge(void *context,uint32_t address,uint32_t value) {
    return wr(context,address,0,value);
}
static int scalar_bridge(void *context,uint32_t database,uint32_t key,uint32_t *value) {
    gool_create *c=context;
    return status(c->services.scalar(c->services.context,database,key,value));
}
static int path_bridge(void *context,uint32_t database,uint32_t *address) {
    gool_create *c=context;
    return status(c->services.path(c->services.context,database,address));
}
static int tree_read(void *context,uint32_t node,uint32_t offset,uint32_t *value) {
    gool_create *c=context;int error=rd(c,node,offset,value);
    if(error)c->tree_provider_error=error;
    return error;
}
static int tree_write(void *context,uint32_t node,uint32_t offset,uint32_t value) {
    gool_create *c=context;int error=wr(c,node,offset,value);
    if(error)c->tree_provider_error=error;
    return error;
}
static int reparent_bridge(void *context,uint32_t node,uint32_t parent) {
    gool_create *c=context;
    gool_tree_services services={.context=c,.read=tree_read,.write=tree_write};
    c->tree_provider_error=0;
    int result=gool_tree_reparent(node,parent,&services,c->services.scan_limit);
    if(c->tree_provider_error)return c->tree_provider_error;
    /* Native creation ignores the reparent routine's2/-24 return values;
     * portable address/cycle/provider failures must still stop the owner. */
    return result==2||result==-24?0:result<0?result:GOOL_CREATE_INVALID;
}
static int search_bridge(void *context,uint32_t root,uint32_t predicate,uint32_t flags,uint32_t *candidate) {
    gool_create *c=context;
    if(predicate!=0x80019cc0u)return GOOL_CREATE_INVALID;
    return gool_search_mask(root,flags,c->services.scan_limit,read_bridge,c,candidate);
}
static int retire_bridge(void *context,uint32_t node,uint32_t caller_argument) {
    gool_create *c=context;(void)caller_argument;
    return status(c->services.retire(c->services.context,node,1));
}
static int geometry_bridge(void *context,uint32_t database,uint32_t *value) {
    return scalar_bridge(context,database,0x47,value);
}
static int load_bridge(void *context,uint32_t address,uint32_t *value) {
    gool_create *c=context;
    return status(c->services.load(c->services.context,address,value));
}
static int state_bridge(void *context,uint32_t node,uint32_t state,uint32_t argc,
                        uint32_t arguments,uint32_t *value) {
    gool_create *c=context;
    return c->services.enter_state(c->services.context,node,state,argc,arguments,value);
}
static int usage_address(gool_create *c,uint32_t index,uint32_t *address) {
    if(index>=c->services.usage_count||
       (uint64_t)c->services.usage_address+(uint64_t)index*4>UINT32_MAX)return GOOL_CREATE_RANGE;
    *address=c->services.usage_address+index*4;return 0;
}
#define TRY(expression) do{int error=(expression);if(error)return error;}while(0)
static int prepare(gool_create *c) {
    uint32_t value;
    TRY(wr(c,c->node,4,3));
    TRY(scalar_bridge(c,c->admission.database,0x9f,&value));
    TRY(wr(c,c->node,0xb8,value<<8));
    for(unsigned offset=0x20;offset<0x40;offset+=4)TRY(wr(c,c->node,offset,0));
    TRY(wr(c,c->node,0xf0,c->admission.database));
    TRY(status(c->services.path_count(c->services.context,c->admission.database,&value)));
    TRY(wr(c,c->node,0xf8,value<<8));
    TRY(status(c->services.parameters(c->services.context,c->admission.database,
                                      &c->parameters,&c->parameter_count)));
    if(!c->parameters)c->parameter_count=0;
    TRY(scalar_bridge(c,c->admission.database,0xa9,&c->class_index));
    return scalar_bridge(c,c->admission.database,0xaa,&c->object_id);
}
static int publish(gool_create *c) {
    uint32_t index,unused,address,value;
    TRY(scalar_bridge(c,c->admission.database,0x9f,&index));
    TRY(usage_address(c,index,&address));
    TRY(scalar_bridge(c,c->admission.database,0x9f,&unused));
    TRY(rd(c,address,0,&value));TRY(wr(c,address,0,value|1u));
    TRY(wr(c,c->node,0x18,c->entry));
    uint32_t output;
    if(c->node>UINT32_MAX-0x60u)return GOOL_CREATE_INVALID;
    output=c->node+0x60;
    return status(c->services.position(c->services.context,c->node,0,output));
}
static int checkpoint(gool_create *c) {
    uint32_t player;
    TRY(rd(c,c->services.protected_address,0,&player));
    if(c->node!=player)return 0;
    uint32_t index,nsd,count;
    TRY(rd(c,0x80069104,0,&index));TRY(rd(c,0x80061044,0,&nsd));
    TRY(rd(c,nsd,0,&count));
    if(index>=count||(uint64_t)index*24+0x2f0>UINT32_MAX)return GOOL_CREATE_RANGE;
    uint32_t offset=index*24,position[3];
    for(unsigned i=0;i<3;i++)TRY(rd(c,nsd,0x2e8+offset+i*4,&position[i]));
    for(unsigned i=0;i<3;i++)TRY(wr(c,c->node,0x60+i*4,position[i]));
    uint32_t value;TRY(rd(c,0x80060ac0,0,&value));
    if(value==UINT32_MAX) {
        TRY(wr(c,0x8006900c,0,UINT32_MAX));
        TRY(status(c->services.player_event(c->services.context,c->node,0x80065dd8,1,0)));
    }
    return 0;
}
static int category(gool_create *c) {
    uint32_t behavior,header,value;
    TRY(rd(c,c->node,0xc,&behavior));TRY(rd(c,behavior,0x10,&header));TRY(rd(c,header,4,&value));
    return value==0x300?reparent_bridge(c,c->node,0x80068ec0):0;
}
static int chain(gool_create *c) {
    uint32_t value;TRY(rd(c,c->node,0xb4,&value));
    if((value>>16)!=0x22)return 0;
    int preserve=0;
    if(c->parameters) {
        TRY(rd(c,c->parameters,4,&value));preserve=(value&0x80000u)!=0;
    }
    if(!preserve) {
        TRY(wr(c,c->node,0x84,c->admission.previous_chain));TRY(wr(c,c->node,0x88,0));
        if(c->admission.previous_chain)TRY(wr(c,c->admission.previous_chain,0x88,c->node));
        uint32_t y,height;
        TRY(rd(c,c->node,0x64,&y));TRY(rd(c,0x80069214,0,&height));
        TRY(wr(c,c->node,0x64,y+0x19000u-height));
    }
    return wr(c,0x80069210,0,c->node);
}
#undef TRY
int gool_create_begin(gool_create *c,uint32_t entry,uint32_t entity_index,
                        const gool_create_services *s) {
    if(!c||c->active||!s||!s->read||!s->write||!s->scalar||!s->path||!s->path_count||
       !s->parameters||!s->load||!s->enter_state||!s->position||!s->retire||!s->player_event||
       !s->protected_address||(s->protected_address&3u)||!s->usage_address||
       (s->usage_address&3u)||!s->usage_count||!s->scan_limit||!s->max_reclaims)
        return GOOL_CREATE_INVALID;
    gool_create_services copy=*s;
    *c=(gool_create){.services=copy,.entry=entry,.entity_index=entity_index,
                     .active=1,.status=GOOL_CREATE_BUDGET,.native_result=-14};
    return GOOL_CREATE_BUDGET;
}
int gool_create_step(gool_create *c,uint32_t budget) {
    if(!c)return GOOL_CREATE_INVALID;
    if(!c->active)return c->status;
    while(budget--) {
        int error;
        switch(c->phase) {
        case 0: {
            gool_admission_services a={.context=c,.read=read_bridge,.write=write_bridge,
                .scalar=scalar_bridge,.path=path_bridge,.protected_address=c->services.protected_address,
                .usage_address=c->services.usage_address,.usage_count=c->services.usage_count};
            error=gool_admission(c->entry,c->entity_index,&a,&c->admission);
            if(error)return finish(c,error);
            if(!c->admission.admitted){c->native_result=c->admission.native_result;return finish(c,0);}
            gool_slot_services s={.context=c,.read=read_bridge,.write=write_bridge,.search=search_bridge,
                .retire=retire_bridge,.reparent=reparent_bridge,.protected_address=c->services.protected_address,
                .max_reclaims=c->services.max_reclaims};
            error=gool_slot_begin(&c->slot,&c->admission,&s);
            if(error!=GOOL_SLOT_BUDGET)return finish(c,error<0?error:GOOL_CREATE_INVALID);
            c->phase=1;break;
        }
        case 1:
            error=gool_slot_step(&c->slot,1);if(error<0)return finish(c,error);
            if(error==GOOL_SLOT_BUDGET)break;
            if(c->slot.native_result){c->native_result=c->slot.native_result;return finish(c,0);}
            c->node=c->slot.slot;c->phase=2;break;
        case 2:
            error=prepare(c);if(error)return finish(c,error);c->phase=3;break;
        case 3: {
            gool_initialize_services s={.context=c,.read=read_bridge,.write=write_bridge,
                .geometry=geometry_bridge,.load=load_bridge,.enter_state=state_bridge};
            gool_initialize_request r={.node=c->node,.class_index=c->class_index,.object_id=c->object_id,
                .argument_count=c->parameter_count,.arguments=c->parameters,.placed=1};
            error=gool_initialize_begin(&c->initialize,&r,&s);
            if(error!=GOOL_INITIALIZE_BUDGET)return finish(c,error<0?error:GOOL_CREATE_INVALID);
            c->phase=8;break;
        }
        case 8: {
            error=gool_initialize_step(&c->initialize,1);
            if(error<0)return finish(c,error);
            if(error==GOOL_INITIALIZE_WAIT)return GOOL_CREATE_BUDGET;
            if(error==GOOL_INITIALIZE_BUDGET)break;
            int32_t result=c->initialize.native_result;
            if(result>=-254) {
                error=reparent_bridge(c,c->node,0x80068ee0);if(error)return finish(c,error);
                c->native_result=result;return finish(c,0);
            }
            c->phase=4;break;
        }
        case 4:error=publish(c);if(error)return finish(c,error);c->phase=5;break;
        case 5:error=checkpoint(c);if(error)return finish(c,error);c->phase=6;break;
        case 6:error=category(c);if(error)return finish(c,error);c->phase=7;break;
        case 7:
            error=chain(c);if(error)return finish(c,error);
            c->native_result=signed_word(c->node);return finish(c,GOOL_CREATE_DONE);
        default:return finish(c,GOOL_CREATE_INVALID);
        }
    }
    return GOOL_CREATE_BUDGET;
}
