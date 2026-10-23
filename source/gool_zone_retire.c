#include "gool_zone_retire.h"

static int read_word(gool_zone_retire *o,uint32_t address,uint32_t *value) {
    return gool_domain_read32(o->services.domain,address,value);
}
static int push(gool_zone_retire *o,uint32_t node,int root) {
    if(!node||(node&3u)||(uint64_t)node+0x104u>UINT64_C(0x100000000)||
       o->depth==GOOL_ZONE_RETIRE_DEPTH||o->visited>=o->services.max_nodes)
        return GOOL_ZONE_RETIRE_LIMIT;
    for(uint32_t i=0;i<o->depth;i++)if(o->frames[i].node==node)return GOOL_ZONE_RETIRE_LIMIT;
    o->frames[o->depth++]=(gool_zone_retire_frame){.node=node,.root=root};
    o->visited++;return 0;
}
int gool_zone_retire_begin(gool_zone_retire *o,uint32_t entry,const gool_zone_retire_services *s) {
    if(!o||o->opened||!entry||(entry&3u)||!s||!s->domain||!s->max_nodes||
       !s->signal||!s->retire||!s->cancel)return GOOL_ZONE_RETIRE_INVALID;
    gool_zone_retire_services captured=*s;
    *o=(gool_zone_retire){.services=captured,.entry=entry,.opened=1,.status=GOOL_ZONE_RETIRE_PENDING};
    return o->status;
}
static void complete(gool_zone_retire *o,int32_t native_result) {
    o->depth--;o->native_result=native_result;
    if(!o->depth)o->root_index++;
    else if(native_result==-25)o->frames[o->depth-1u].phase=6;
}
int gool_zone_retire_step(gool_zone_retire *o,uint32_t budget) {
    if(!o||!o->opened)return GOOL_ZONE_RETIRE_INVALID;
    if(o->status!=GOOL_ZONE_RETIRE_PENDING)return o->status;
    while(budget--) {
        int error;uint32_t value;
        if(!o->depth) {
            if(o->root_index==8u)return o->status=GOOL_ZONE_RETIRE_DONE;
            error=push(o,0x80068ea0u+o->root_index*8u,1);
            if(error)return o->status=error;
            continue;
        }
        gool_zone_retire_frame *f=&o->frames[o->depth-1u];
        if(f->phase==0) {
            uint32_t offset=0x4cu;
            if(f->root) {
                error=read_word(o,f->node,&value);if(error)return o->status=error;
                if(value==2u)offset=4u;
            }
            error=read_word(o,f->node+offset,&f->next_child);if(error)return o->status=error;
            f->phase=1;
        } else if(f->phase==1) {
            if(f->next_child) {
                uint32_t child=f->next_child;
                if((uint64_t)child+0x4cu>UINT64_C(0x100000000))return o->status=GOOL_ZONE_RETIRE_INVALID;
                error=read_word(o,child+0x48u,&f->next_child);if(error)return o->status=error;
                error=push(o,child,0);if(error)return o->status=error;
            } else if(f->root)complete(o,-255);
            else f->phase=2;
        } else if(f->phase==2) {
            error=read_word(o,f->node+0xb8u,&value);if(error)return o->status=error;
            if(value){complete(o,-255);continue;}
            error=read_word(o,f->node,&value);if(error)return o->status=error;
            if(!value){complete(o,-255);continue;}
            error=read_word(o,f->node+0x18u,&value);if(error)return o->status=error;
            if(value!=o->entry){complete(o,-255);continue;}
            error=read_word(o,f->node+0xacu,&value);if(error)return o->status=error;
            if(value&0x01000000u){complete(o,-255);continue;}
            error=read_word(o,f->node+0x100u,&value);if(error)return o->status=error;
            if(value&0x10000u){complete(o,-255);continue;}
            f->phase=3;
        } else if(f->phase==3) {
            o->callback_active=1;o->retiring=0;o->callback_node=f->node;
            error=o->services.signal(o->services.context,f->node,0x1a00u);
            if(error==1)return GOOL_ZONE_RETIRE_PENDING;
            if(error)return o->status=error<0?error:GOOL_ZONE_RETIRE_INVALID;
            o->callback_active=0;f->phase=4;
        } else if(f->phase==4) {
            error=read_word(o,f->node+0x18u,&value);if(error)return o->status=error;
            if(value!=o->entry) {
                error=read_word(o,0x80065d60u,&value);if(error)return o->status=error;
                if(value!=UINT32_MAX){complete(o,-255);continue;}
            }
            f->phase=5;
        } else if(f->phase==5||f->phase==7) {
            o->callback_active=1;o->retiring=1;o->callback_node=f->node;
            error=o->services.retire(o->services.context,f->node,0,&o->native_result);
            if(error==1)return GOOL_ZONE_RETIRE_PENDING;
            if(error)return o->status=error<0?error:GOOL_ZONE_RETIRE_INVALID;
            o->callback_active=0;
            if(f->phase==5)complete(o,o->native_result);else f->phase=1;
        } else if(f->phase==6) {
            if(f->root) {
                error=read_word(o,f->node,&value);if(error)return o->status=error;
                if(value==2u){f->phase=1;continue;}
            }
            f->phase=7;
        } else return o->status=GOOL_ZONE_RETIRE_INVALID;
    }
    return GOOL_ZONE_RETIRE_PENDING;
}
int gool_zone_retire_close(gool_zone_retire *o) {
    if(!o)return GOOL_ZONE_RETIRE_INVALID;
    if(o->callback_active) {
        int error=o->services.cancel(o->services.context,o->callback_node,o->retiring);
        if(error)return error<0?error:GOOL_ZONE_RETIRE_PENDING;
    }
    *o=(gool_zone_retire){0};return 0;
}
