#include "gool_pump.h"
#include <string.h>

static int check(const gool_pump *p,const gool_instance *i) {
    if(!i||!i->ready||i->context.self>=p->count||p->instances[i->context.self]!=i||
       i->context.objects!=p->objects||i->context.count!=p->count||
       p->objects[i->context.self]!=&i->object||i->vm.memory!=p->memory)return GOOL_VM_INVALID;
    return 0;
}
static int validate(const gool_pump *p) {
    if(check(p,p->root)||p->root->vm.tick!=p->tick)return GOOL_VM_INVALID;
    if(p->current_index!=UINT32_MAX&&
       (p->current_index>=p->count||check(p,p->instances[p->current_index])||
        p->instances[p->current_index]->context.self!=p->current_index))return GOOL_VM_INVALID;
    for(uint32_t n=0;n<p->depth;n++) {
        const gool_delivery *d=&p->jobs[n];
        if(check(p,d->sender)||d->sender->vm.tick!=p->tick)return GOOL_VM_INVALID;
        if(d->target&&(check(p,d->target)||d->target->vm.tick!=p->tick))return GOOL_VM_INVALID;
    }
    return 0;
}
int gool_pump_abort(gool_pump *p,int status) {
    if(!p||status>=0||status==GOOL_VM_BUDGET)return GOOL_VM_INVALID;
    while(p->depth) {
        p->depth--;gool_delivery_abort(&p->jobs[p->depth],status);
        memset(&p->jobs[p->depth],0,sizeof(p->jobs[p->depth]));
    }
    p->pending=NULL;p->active=0;p->status=status;return status;
}
int gool_pump_begin(gool_pump *out,gool_instance **instances,uint32_t count,
                    gool_instance *sender,gool_memory *memory,uint64_t tick,uint32_t current_index) {
    if(!out||out->active||out->depth||!instances||!count||count>0x04000000u||!sender||
       !sender->ready||!sender->active||sender->vm.status||!sender->vm.send_pending||
       sender->vm.tick!=tick||!memory||!sender->context.objects)return GOOL_VM_INVALID;
    /* Validate borrowed tables before replacing the idle pump. No job pointer
     * has been lent yet; never copy a pump after borrowing its job storage. */
    if(sender->context.self>=count||instances[sender->context.self]!=sender||
       sender->context.count!=count||sender->context.objects[sender->context.self]!=&sender->object||
       sender->vm.memory!=memory)return GOOL_VM_INVALID;
    if(current_index!=UINT32_MAX) {
        if(current_index>=count)return GOOL_VM_INVALID;
        gool_instance *current=instances[current_index];
        if(!current||!current->ready||current->context.self!=current_index||
           current->context.objects!=sender->context.objects||current->context.count!=count||
           current->context.objects[current_index]!=&current->object||current->vm.memory!=memory)return GOOL_VM_INVALID;
    }
    memset(out,0,sizeof(*out));out->instances=instances;out->count=count;
    out->objects=sender->context.objects;out->memory=memory;out->root=sender;
    out->pending=sender;out->tick=tick;out->current_index=current_index;
    out->active=1;out->status=GOOL_VM_BUDGET;return GOOL_VM_BUDGET;
}
static int resolve(gool_pump *p,int32_t pointer,gool_instance **target) {
    uint32_t raw=(uint32_t)pointer,index=(raw&0x0fffffffu)>>2;
    if((raw>>28)!=6||(raw&3)||index>=p->count)return GOOL_VM_OPERAND;
    if(!p->objects[index]){*target=NULL;return 0;}
    gool_instance *i=p->instances[index];
    if(!i||check(p,i)||i->context.self!=index)return GOOL_VM_OPERAND;
    *target=i;return 0;
}
int gool_pump_step(gool_pump *p,uint32_t budget) {
    if(!p)return GOOL_VM_INVALID;
    if(!p->active)return p->status;
    if(!budget)return GOOL_VM_BUDGET;
    while(budget--) {
        int result=validate(p);
        if(result)return gool_pump_abort(p,result);
        gool_delivery *job;
        if(p->pending) {
            if(p->depth>=GOOL_PUMP_DEPTH)return gool_pump_abort(p,GOOL_VM_STACK);
            gool_instance *sender=p->pending,*target=NULL;
            if(check(p,sender)||sender->vm.tick!=p->tick)return gool_pump_abort(p,GOOL_VM_INVALID);
            result=resolve(p,sender->vm.send.target,&target);
            if(result)return gool_pump_abort(p,result);
            job=&p->jobs[p->depth++];memset(job,0,sizeof(*job));
            result=gool_delivery_begin_mapped(job,sender,target,p->memory,p->tick);
            p->pending=NULL;
            if(result>=0&&job->active)result=GOOL_VM_BUDGET;
        } else {
            if(!p->depth)return gool_pump_abort(p,GOOL_VM_INVALID);
            job=&p->jobs[p->depth-1];
            result=job->status==GOOL_DELIVERY_ROUTE&&!job->active?
                gool_delivery_apply(job,p->current_index,1):gool_delivery_step(job,1);
        }
        if(result<0&&result!=GOOL_VM_BUDGET)return gool_pump_abort(p,result);
        if(result==GOOL_VM_SEND)p->pending=job->target;
        else if(result==GOOL_VM_BUDGET||result==GOOL_DELIVERY_ROUTE)continue;
        else if(!job->active&&(result==GOOL_VM_RUNNING||result==GOOL_VM_SEND_REPLACED)) {
            p->depth--;memset(job,0,sizeof(*job));
            if(!p->depth){p->active=0;p->status=result;return result;}
        } else return gool_pump_abort(p,GOOL_VM_INVALID);
    }
    return GOOL_VM_BUDGET;
}
