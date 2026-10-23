#include "gool_destroy.h"
#include <string.h>

static int fail(gool_destroy *c,int status) {
    if(status>=0)status=GOOL_DESTROY_INVALID;
    c->active=0;c->status=status;return status;
}
static int push(gool_destroy *c,uint32_t node) {
    if(c->depth==GOOL_TREE_DEPTH)return fail(c,GOOL_DESTROY_DEPTH);
    for(uint32_t i=0;i<c->depth;i++)if(c->frames[i].node==node)return fail(c,GOOL_DESTROY_CYCLE);
    c->frames[c->depth++]=(gool_destroy_frame){.node=node};return 0;
}
int gool_destroy_begin(gool_destroy *c,uint32_t object,uint32_t notify,
                       const gool_destroy_services *s) {
    if(!c||c->active||!s||!s->links.read||!s->links.write||
       !s->protected_object||!s->detached_object||!s->usage||!s->usage_count||
       !s->free_root||!s->scan_limit||!s->notify||!s->stop_audio)return GOOL_DESTROY_INVALID;
    gool_destroy_services providers=*s;
    memset(c,0,sizeof(*c));c->services=providers;c->notify=notify;
    c->active=1;c->status=GOOL_DESTROY_BUDGET;
    push(c,object);return GOOL_DESTROY_BUDGET;
}
static int retire(gool_destroy *c,uint32_t node) {
    gool_destroy_services *s=&c->services;gool_tree_services *l=&s->links;
    void *context=l->context;uint32_t raw,type,head;int error;
    error=s->stop_audio(context,node);if(error)return error;
    error=l->read(context,node,0xb8,&raw);if(error)return error;
    /* SRA8 is negative for any input with bit31 set. Never turn a malformed
     * signed index into a huge unsigned write into the occupancy table. */
    if(raw&0x80000000u||(raw>>8)>=s->usage_count)return GOOL_DESTROY_USAGE;
    s->usage[raw>>8]&=~1u;
    uint32_t protected_snapshot=*s->protected_object;
    error=l->write(context,node,0,0);if(error)return error;
    if(node!=protected_snapshot) {
        error=gool_tree_reparent(node,s->free_root,l,s->scan_limit);
        return error==2||error==-24?0:error;
    }
    /* A callback changed the protected slot after the initial guard. Native
     * code temporarily inserts the object, then restores the previous head. */
    error=l->read(context,s->free_root,0,&type);if(error)return error;
    error=l->read(context,s->free_root,type==2?4:0x4c,&head);if(error)return error;
    error=gool_tree_reparent(node,s->free_root,l,s->scan_limit);
    if(error!=2&&error!=-24)return error;
    error=l->read(context,s->free_root,0,&type);if(error)return error;
    error=l->write(context,s->free_root,type==2?4:0x4c,head);if(error)return error;
    error=l->write(context,*s->detached_object,0x44,0);if(error)return error;
    error=l->write(context,*s->detached_object,0x4c,0);if(error)return error;
    error=l->write(context,*s->detached_object,0x48,0);if(error)return error;
    *s->protected_object=0;return 0;
}
int gool_destroy_step(gool_destroy *c,uint32_t budget) {
    if(!c)return GOOL_DESTROY_INVALID;
    if(!c->active)return c->status;
    gool_destroy_services *s=&c->services;gool_tree_services *l=&s->links;
    while(budget--) {
        if(!c->depth){c->active=0;c->status=0;return 0;}
        gool_destroy_frame *f=&c->frames[c->depth-1];int error;
        if(f->phase==0) {
            if(!f->node||f->node==*s->protected_object){c->depth--;continue;}
            f->phase=1;
        } else if(f->phase==1) {
            if(c->notify) {
                error=s->notify(l->context,f->node,0x1a00);
                if(error==GOOL_DESTROY_PROVIDER_PENDING)return GOOL_DESTROY_BUDGET;
                if(error)return fail(c,error);
            }
            f->phase=2;
        } else if(f->phase==2) {
            error=l->read(l->context,f->node,0x4c,&f->next_child);
            if(error)return fail(c,error);
            f->phase=3;
        } else if(f->phase==3&&f->next_child) {
            uint32_t child=f->next_child;
            error=l->read(l->context,child,0x48,&f->next_child);
            if(error)return fail(c,error);
            if(push(c,child)<0)return c->status;
        } else {
            error=retire(c,f->node);
            if(error==GOOL_DESTROY_PROVIDER_PENDING)return GOOL_DESTROY_BUDGET;
            if(error)return fail(c,error);
            c->depth--;
        }
    }
    return GOOL_DESTROY_BUDGET;
}
