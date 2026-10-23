#include "gool_search.h"
static int finish(gool_search *s,int result){s->active=0;s->status=result;return result;}
static int rd(gool_search *s,uint32_t base,uint32_t offset,uint32_t *out) {
    if(!base||(base&3u)||offset>UINT32_MAX-base)return GOOL_SEARCH_INVALID;
    int error=s->read(s->context,base+offset,out);
    return error>0?GOOL_SEARCH_INVALID:error;
}
static int push(gool_search *s,uint32_t node) {
    if(s->depth==GOOL_SEARCH_DEPTH)return finish(s,GOOL_SEARCH_DEPTH_LIMIT);
    if(!node||(node&3u))return finish(s,GOOL_SEARCH_INVALID);
    for(uint32_t i=0;i<s->depth;i++)if(s->frames[i].node==node)return finish(s,GOOL_SEARCH_CYCLE);
    if(s->visited==s->max_nodes)return finish(s,GOOL_SEARCH_LIMIT);
    s->visited++;s->frames[s->depth++]=(gool_search_frame){.node=node};return GOOL_SEARCH_BUDGET;
}
int gool_search_begin(gool_search *s,uint32_t root,uint32_t mask,uint32_t max_nodes,
                        gool_search_read read,void *context) {
    if(!s||s->active||!read||!max_nodes)return GOOL_SEARCH_INVALID;
    *s=(gool_search){.context=context,.read=read,.mask=mask,.max_nodes=max_nodes,
                    .active=1,.status=GOOL_SEARCH_BUDGET};
    if(!root)return finish(s,GOOL_SEARCH_DONE);
    return push(s,root);
}
int gool_search_step(gool_search *s,uint32_t budget) {
    if(!s)return GOOL_SEARCH_INVALID;
    if(!s->active)return s->status;
    while(budget--) {
        if(!s->depth)return finish(s,GOOL_SEARCH_DONE);
        gool_search_frame *f=&s->frames[s->depth-1];int error;uint32_t type,value;
        if(!f->opened) {
            error=rd(s,f->node,0,&type);if(error)return finish(s,error);
            if(type!=2) {
                int matched;
                if(s->predicate) {
                    matched=0;error=s->predicate(s->predicate_context,f->node,&matched);
                    if(error==2&&s->predicate_resumable)return GOOL_SEARCH_BUDGET;
                    if(error)return finish(s,error<0?error:GOOL_SEARCH_INVALID);
                    if(matched!=0&&matched!=1)return finish(s,GOOL_SEARCH_INVALID);
                } else {
                    error=rd(s,f->node,s->serial_match?0xb8u:0x100u,&value);if(error)return finish(s,error);
                    matched=s->serial_match?value==s->mask:(value&s->mask)!=0;
                }
                if(matched){s->result=f->node;return finish(s,GOOL_SEARCH_DONE);}
                error=rd(s,f->node,0,&type);if(error)return finish(s,error);
            }
            error=rd(s,f->node,type==2?4:0x4c,&f->next_child);if(error)return finish(s,error);
            f->opened=1;
        } else if(f->next_child) {
            uint32_t child=f->next_child;
            error=rd(s,child,0x48,&f->next_child);if(error)return finish(s,error);
            error=push(s,child);if(error<0)return error;
        } else s->depth--;
    }
    return GOOL_SEARCH_BUDGET;
}
int gool_search_mask(uint32_t root,uint32_t mask,uint32_t max_nodes,
                       gool_search_read read,void *context,uint32_t *result) {
    if(!result)return GOOL_SEARCH_INVALID;
    gool_search search={0};int status=gool_search_begin(&search,root,mask,max_nodes,read,context);
    while(status==GOOL_SEARCH_BUDGET)status=gool_search_step(&search,32);
    if(status<0)return status;
    *result=search.result;return 0;
}
int gool_search_serial(uint32_t root,uint32_t serial,uint32_t max_nodes,
                       gool_search_read read,void *context,uint32_t *result) {
    if(!result)return GOOL_SEARCH_INVALID;
    gool_search search={0};int status=gool_search_begin(&search,root,serial,max_nodes,read,context);
    search.serial_match=1;
    while(status==GOOL_SEARCH_BUDGET)status=gool_search_step(&search,32);
    if(status<0)return status;
    *result=search.result;return 0;
}
int gool_search_visit(uint32_t root,uint32_t max_nodes,gool_search_read read,
    void *read_context,gool_search_predicate predicate,void *predicate_context,uint32_t *result) {
    if(!predicate||!result)return GOOL_SEARCH_INVALID;
    gool_search search={0};int status=gool_search_begin(&search,root,0,max_nodes,read,read_context);
    search.predicate=predicate;search.predicate_context=predicate_context;
    while(status==GOOL_SEARCH_BUDGET)status=gool_search_step(&search,32);
    if(status<0)return status;
    *result=search.result;return 0;
}
