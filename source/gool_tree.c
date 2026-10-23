#include "gool_tree.h"
#include <string.h>

static int fail(gool_tree *tree,int status) {
    if(status>=0)status=GOOL_TREE_INVALID;
    tree->active=0;tree->status=status;return status;
}
static int push(gool_tree *tree,uint32_t node) {
    if(tree->depth==GOOL_TREE_DEPTH)return fail(tree,GOOL_TREE_DEPTH_LIMIT);
    for(uint32_t i=0;i<tree->depth;i++)
        if(tree->frames[i].node==node)return fail(tree,GOOL_TREE_CYCLE);
    tree->frames[tree->depth++]=(gool_tree_frame){.node=node};
    return GOOL_TREE_BUDGET;
}
int gool_tree_begin(gool_tree *tree,const uint32_t roots[GOOL_TREE_ROOTS],
                    const gool_tree_services *services) {
    if(!tree||tree->active||!roots||!services||!services->read||
       !services->visit||!services->erase)return GOOL_TREE_INVALID;
    uint32_t copy[GOOL_TREE_ROOTS];memcpy(copy,roots,sizeof(copy));
    for(uint32_t i=0;i<GOOL_TREE_ROOTS;i++)if(!copy[i])return GOOL_TREE_INVALID;
    gool_tree_services providers=*services;
    memset(tree,0,sizeof(*tree));memcpy(tree->roots,copy,sizeof(copy));
    tree->services=providers;tree->active=1;tree->status=GOOL_TREE_BUDGET;
    return GOOL_TREE_BUDGET;
}
int gool_tree_step(gool_tree *tree,uint32_t budget) {
    if(!tree)return GOOL_TREE_INVALID;
    if(!tree->active)return tree->status;
    gool_tree_services *s=&tree->services;
    while(budget--) {
        int error;uint32_t value;
        if(!tree->depth) {
            if(tree->root==GOOL_TREE_ROOTS){tree->active=0;tree->status=0;return 0;}
            if(!tree->root_loaded) {
                uint32_t root=tree->roots[tree->root];
                error=s->read(s->context,root,0,&value);
                if(error)return fail(tree,error);
                error=s->read(s->context,root,value==2?4:0x4c,&tree->next_root_child);
                if(error)return fail(tree,error);
                tree->root_loaded=1;
            } else if(tree->next_root_child) {
                uint32_t node=tree->next_root_child;
                error=s->read(s->context,node,0x48,&value);
                if(error)return fail(tree,error);
                tree->next_root_child=value;
                if(push(tree,node)<0)return tree->status;
            } else {tree->root++;tree->root_loaded=0;}
            continue;
        }
        gool_tree_frame *frame=&tree->frames[tree->depth-1];
        if(!frame->phase) {
            int32_t result;
            error=s->visit(s->context,frame->node,&result);
            if(error==GOOL_TREE_PROVIDER_PENDING)return GOOL_TREE_BUDGET;
            if(error)return fail(tree,error);
            frame->phase=result==-25?3:1;
        } else if(frame->phase==3) {
            error=s->erase(s->context,frame->node);
            if(error==GOOL_TREE_PROVIDER_PENDING)return GOOL_TREE_BUDGET;
            if(error)return fail(tree,error);
            tree->depth--;
        } else if(frame->phase==1) {
            error=s->read(s->context,frame->node,0x4c,&frame->next_child);
            if(error)return fail(tree,error);
            frame->phase=2;
        } else if(frame->next_child) {
            uint32_t node=frame->next_child;
            error=s->read(s->context,node,0,&value);
            if(error)return fail(tree,error);
            if(!value){tree->depth--;continue;}
            error=s->read(s->context,node,0x48,&value);
            if(error)return fail(tree,error);
            frame->next_child=value;
            if(push(tree,node)<0)return tree->status;
        } else tree->depth--;
    }
    return GOOL_TREE_BUDGET;
}

int gool_tree_reparent(uint32_t node,uint32_t parent,
                       const gool_tree_services *s,uint32_t limit) {
    if(!node||!parent||node==parent||!s||!s->read||!s->write)return GOOL_TREE_INVALID;
    uint32_t old,head,type,next,previous=0;int error;
    error=s->read(s->context,node,0x44,&old);if(error)return error<0?error:GOOL_TREE_INVALID;
    if(old) {
        error=s->read(s->context,old,0,&type);if(error)return error<0?error:GOOL_TREE_INVALID;
        uint32_t offset=type==2?4:0x4c;
        error=s->read(s->context,old,offset,&head);if(error)return error<0?error:GOOL_TREE_INVALID;
        if(head!=node) {
            while(head) {
                if(!limit--)return GOOL_TREE_DEPTH_LIMIT;
                error=s->read(s->context,head,0x48,&next);if(error)return error<0?error:GOOL_TREE_INVALID;
                if(next==node){previous=head;break;}
                head=next;
            }
            if(!head)return -24;
        }
        error=s->read(s->context,node,0x48,&next);if(error)return error<0?error:GOOL_TREE_INVALID;
        error=s->write(s->context,previous?previous:old,previous?0x48:offset,next);
        if(error)return error<0?error:GOOL_TREE_INVALID;
    }
    error=s->write(s->context,node,0x44,parent);if(error)return error<0?error:GOOL_TREE_INVALID;
    error=s->read(s->context,parent,0,&type);if(error)return error<0?error:GOOL_TREE_INVALID;
    error=s->read(s->context,parent,type==2?4:0x4c,&head);if(error)return error<0?error:GOOL_TREE_INVALID;
    error=s->write(s->context,node,0x48,head);if(error)return error<0?error:GOOL_TREE_INVALID;
    /* Native reloads parent type after storing the node's sibling. */
    error=s->read(s->context,parent,0,&type);if(error)return error<0?error:GOOL_TREE_INVALID;
    error=s->write(s->context,parent,type==2?4:0x4c,node);
    return error?(error<0?error:GOOL_TREE_INVALID):2;
}
