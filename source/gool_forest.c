#include "gool_forest.h"
#include <string.h>

static int read_node(void *context,uint32_t object,uint32_t offset,uint32_t *value) {
    gool_forest *f=context;
    return f->services.links.read(f->services.links.context,object,offset,value);
}
static int visit_node(void *context,uint32_t object,int32_t *result) {
    gool_forest *f=context;
    return f->services.links.visit(f->services.links.context,object,result);
}
static int erase_node(void *context,uint32_t object) {
    gool_forest *f=context;int result;
    if(!f->deletion_pending) {
        result=gool_destroy_begin(&f->destroy,object,0,&f->services);
        if(result!=GOOL_DESTROY_BUDGET)return result;
        f->deleting=object;f->deletion_pending=1;
    } else if(f->deleting!=object)return GOOL_TREE_INVALID;
    result=gool_destroy_step(&f->destroy,1);
    if(result==GOOL_DESTROY_BUDGET)return GOOL_TREE_PROVIDER_PENDING;
    if(result<0)return result;
    f->deletion_pending=0;return 0;
}
int gool_forest_begin(gool_forest *f,const uint32_t roots[GOOL_TREE_ROOTS],
                      const gool_destroy_services *s) {
    if(!f||f->tree.active||f->destroy.active||!s||!s->links.read||!s->links.write||
       !s->links.visit||!s->protected_object||!s->detached_object||!s->usage||
       !s->usage_count||!s->free_root||!s->scan_limit||!s->notify||!s->stop_audio||!roots)
        return GOOL_TREE_INVALID;
    uint32_t root_copy[GOOL_TREE_ROOTS];memcpy(root_copy,roots,sizeof(root_copy));
    for(uint32_t i=0;i<GOOL_TREE_ROOTS;i++)if(!root_copy[i])return GOOL_TREE_INVALID;
    gool_destroy_services providers=*s;
    memset(f,0,sizeof(*f));f->services=providers;
    gool_tree_services tree={.context=f,.read=read_node,.visit=visit_node,.erase=erase_node};
    return gool_tree_begin(&f->tree,root_copy,&tree);
}
int gool_forest_step(gool_forest *f,uint32_t budget) {
    return f?gool_tree_step(&f->tree,budget):GOOL_TREE_INVALID;
}
