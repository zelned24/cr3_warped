#include "gool_delivery.h"
#include <string.h>

static void restore(gool_delivery *d) {
    if(!d->context_bound)return;
    gool_vm *vm=&d->target->vm;
    vm->mode=d->saved_mode;vm->respect_delay=d->saved_delay;
    vm->receiver=d->saved_receiver;vm->send_pending=d->saved_pending;
    vm->send=d->saved_target_send;d->context_bound=0;
}
static int release_span(gool_delivery *d) {
    if(!d->span_memory)return 0;
    int result=gool_memory_release(d->span_memory,d->span_pointer);
    d->span_memory=NULL;d->span_pointer=0;
    return result?GOOL_VM_OPERAND:0;
}
static int failed(gool_delivery *d,int status) {
    restore(d);release_span(d);d->active=0;d->status=status;return status;
}
static int ack(gool_delivery *d,int value) {
    return gool_object_set(&d->sender->object,0x25,value)?GOOL_VM_OPERAND:0;
}
static void bind(gool_delivery *d) {
    gool_vm *vm=&d->target->vm;
    d->saved_mode=vm->mode;d->saved_delay=vm->respect_delay;
    d->saved_receiver=vm->receiver;d->saved_pending=vm->send_pending;
    d->saved_target_send=vm->send;d->saved_depth=vm->stack.depth;
    d->generation=d->target->state_generation;d->context_bound=1;
    vm->send_pending=0;
}
static int finished(gool_delivery *d) {
    restore(d);
    int result=gool_send_finish(&d->sender->vm);
    if(result>=0&&d->replaced&&d->target->busy&&d->target->context.self==d->current_index)
        d->target->phase=d->target->vm.frame_yield?2:3;
    int released=release_span(d);
    if(result>=0&&released)result=released;
    d->active=0;d->status=result;return result;
}
int gool_delivery_begin(gool_delivery *out,gool_instance *sender,gool_instance *target,
                        int32_t argument_pointer,uint64_t tick) {
    if(!out||out->active||out->span_memory||!sender||!sender->ready||!sender->active||
       !sender->vm.send_pending||sender->vm.status||tick!=sender->vm.tick)return GOOL_VM_INVALID;
    gool_delivery next={.sender=sender,.target=target,.request=sender->vm.send,
        .receiver={.new_state=-1},.active=1,.phase=2};
    *out=next;
    if(!target||!target->ready||!target->active||target->vm.status) {
        if(ack(out,0))return failed(out,GOOL_VM_OPERAND);
        return finished(out);
    }
    int32_t pointer;
    if(gool_object_pointer(target->context.self,&pointer)||pointer!=out->request.target||
       sender->context.objects!=target->context.objects||sender->context.count!=target->context.count||
       target->context.self>=target->context.count||target->context.objects[target->context.self]!=&target->object)
        return failed(out,GOOL_VM_INVALID);
    if(((uint32_t)out->request.raw_signal>>8)>=target->base.signal_count) {
        if(ack(out,0))return failed(out,GOOL_VM_OPERAND);
        return finished(out);
    }
    if(gool_vm_set_tick(&target->vm,tick,target->vm.respect_delay))return failed(out,GOOL_VM_INVALID);
    if(ack(out,1)||gool_object_pointer(sender->context.self,&pointer)||
       gool_object_set(&target->object,7,pointer))return failed(out,GOOL_VM_OPERAND);
    int32_t on_signal;
    if(gool_object_get(&target->object,0x23,&on_signal))return failed(out,GOOL_VM_OPERAND);
    if(!on_signal)return GOOL_VM_RUNNING;
    gool_data code;uint32_t pc;
    if(!argument_pointer||tick<target->vm.tick||gool_vm_resolve(&target->vm,on_signal,&code,&pc))
        return failed(out,GOOL_VM_INVALID);
    if((uint64_t)target->vm.stack.top+5>gool_stack_capacity(&target->vm.stack))return failed(out,GOOL_VM_STACK);
    bind(out);
    gool_stack_push(&target->vm.stack,out->request.raw_signal);
    gool_stack_push(&target->vm.stack,argument_pointer);
    int result=gool_vm_invoke(&target->vm,&code,pc,2);
    if(result)return failed(out,result);
    gool_vm_set_tick(&target->vm,tick,0);
    target->vm.receiver=&out->receiver;target->vm.mode=8;
    out->phase=1;return GOOL_VM_RUNNING;
}
int gool_delivery_begin_mapped(gool_delivery *out,gool_instance *sender,gool_instance *target,
                               gool_memory *memory,uint64_t tick) {
    if(!out||out->active||out->span_memory||!sender||!memory||sender->vm.memory!=memory||
       sender->vm.send.argument_count>7||
       (target&&target->ready&&target->active&&target->vm.memory!=memory))return GOOL_VM_INVALID;
    int32_t pointer;
    int result=gool_memory_borrow(memory,out->request.arguments,sender->vm.send.argument_count,1,&pointer);
    if(result)return GOOL_VM_OPERAND;
    result=gool_delivery_begin(out,sender,target,pointer,tick);
    if(result<0||!out->active)gool_memory_release(memory,pointer);
    else {out->span_memory=memory;out->span_pointer=pointer;}
    return result;
}
static int select_route(gool_delivery *d) {
    restore(d);
    if(d->receiver.new_state==-1) {
        if(gool_signal_lookup(&d->target->base,(uint32_t)d->request.raw_signal,&d->route))
            return failed(d,GOOL_VM_INVALID);
        if(ack(d,d->route.kind!=GOOL_SIGNAL_NONE&&d->route.kind!=GOOL_SIGNAL_OUTSIDE_TABLE))
            return failed(d,GOOL_VM_OPERAND);
    } else {
        if(d->receiver.new_state<0)return failed(d,GOOL_VM_INVALID);
        d->route=(gool_signal_route){.kind=d->receiver.new_state==255?GOOL_SIGNAL_NONE:GOOL_SIGNAL_STATE,
            .signal=(uint32_t)d->request.raw_signal>>8,.raw=(uint32_t)d->receiver.new_state,
            .value=(uint32_t)d->receiver.new_state};
        if(ack(d,d->receiver.was_b!=0))return failed(d,GOOL_VM_OPERAND);
    }
    if(d->route.kind==GOOL_SIGNAL_NONE||d->route.kind==GOOL_SIGNAL_OUTSIDE_TABLE)return finished(d);
    if(d->route.kind==GOOL_SIGNAL_SCRIPT) {
        gool_data owner;uint32_t pc;
        if(gool_signal_script(&d->target->base,&d->route,&owner,&pc))return failed(d,GOOL_VM_INVALID);
    } else {
        gool_state_data state;int32_t interact;
        if(gool_data_state(&d->target->base,d->route.value,&state)||
           gool_object_get(&d->target->object,0x1c,&interact))return failed(d,GOOL_VM_INVALID);
        uint32_t flags=(uint32_t)interact;
        int32_t raw=d->request.raw_signal;
        if(raw==0x1800||raw==0x2600||raw==0x1900||raw==0x2500)flags&=~2u;
        if(d->target->context.has_player&&d->target->context.self==d->target->context.player) {
            int32_t player_mode;
            if(gool_object_get(&d->target->object,0x32,&player_mode))return failed(d,GOOL_VM_OPERAND);
            if(player_mode>=2&&player_mode<=4)flags|=0x402;
        }
        if(flags&state.motion_flags) {
            if(ack(d,0))return failed(d,GOOL_VM_OPERAND);
            return finished(d);
        }
        if(raw==0x1800||raw==0x1900) {
            int32_t collision;
            if(gool_object_get(&d->target->object,0x1a,&collision))return failed(d,GOOL_VM_OPERAND);
            uint32_t bits=(uint32_t)collision|0x10000u;
            collision=bits<=INT32_MAX?(int32_t)bits:(int32_t)((int64_t)bits-INT64_C(4294967296));
            gool_object_set(&d->target->object,0x1a,collision);
        }
    }
    gool_object_set(&d->target->object,0x37,d->request.raw_signal);
    d->generation=d->target->state_generation;
    d->active=0;d->status=GOOL_DELIVERY_ROUTE;return d->status;
}
int gool_delivery_step(gool_delivery *d,uint32_t budget) {
    if(!d)return GOOL_VM_INVALID;
    if(d->active&&(d->phase==3||d->phase==4||d->phase==5))return gool_delivery_apply(d,d->current_index,budget);
    if(!d->active)return d->status;
    if(!budget)return GOOL_VM_BUDGET;
    while(d->phase==1&&budget) {
        if(d->target->state_generation!=d->generation)return failed(d,GOOL_VM_UNSUPPORTED);
        budget--;
        int result=gool_vm_step(&d->target->vm);
        if(result<0){d->target->active=0;return failed(d,result);}
        if(result==GOOL_VM_SEND)return result;
        if(result==GOOL_VM_NEW_STATE||result==GOOL_VM_DONE||result==GOOL_VM_WAIT||result==GOOL_VM_RESUME_RETURN)
            return failed(d,GOOL_VM_UNSUPPORTED);
        if(result==GOOL_VM_SIGNAL_RETURN||result==GOOL_VM_FAILED_SIGNAL) {
            if(d->target->vm.stack.depth!=d->saved_depth)return failed(d,GOOL_VM_INVALID);
            d->phase=2;break;
        }
    }
    return d->phase==2?select_route(d):GOOL_VM_BUDGET;
}
static int start_pre(gool_delivery *d) {
    gool_instance *target=d->target;gool_vm *vm=&target->vm;
    d->generation=target->state_generation;d->replaced=1;
    if(target->has_resume) {
        d->saved_depth=vm->stack.depth;
        int result=gool_instance_start_resume(target);
        if(result)return failed(d,result);
        d->phase=5;return GOOL_VM_BUDGET;
    }
    if(target->context.self!=d->current_index)return finished(d);
    int32_t pointer;
    if(gool_object_get(&target->object,0x22,&pointer))return failed(d,GOOL_VM_OPERAND);
    if(!pointer)return finished(d);
    gool_data owner;uint32_t pc;
    if(gool_vm_resolve(vm,pointer,&owner,&pc))return failed(d,GOOL_VM_INVALID);
    d->saved_depth=vm->stack.depth;
    int result=gool_vm_invoke(vm,&owner,pc,0);
    if(result)return failed(d,result);
    vm->mode=3;vm->respect_delay=0;d->phase=4;return GOOL_VM_BUDGET;
}
int gool_delivery_apply(gool_delivery *d,uint32_t current_index,uint32_t budget) {
    if(!d)return GOOL_VM_INVALID;
    if(d->status<0)return d->status;
    int continuing=d->active&&(d->phase==3||d->phase==4||d->phase==5);
    if(!continuing&&d->status!=GOOL_DELIVERY_ROUTE)return GOOL_VM_INVALID;
    if(continuing&&d->current_index!=current_index)return GOOL_VM_INVALID;
    if(!budget)return GOOL_VM_BUDGET;
    gool_instance *target=d->target;gool_vm *vm=&target->vm;
    if(!continuing) {
        if(d->request.argument_count>7)return failed(d,GOOL_VM_INVALID);
        if(target->state_generation!=d->generation||!target->active||vm->status||!d->sender->vm.send_pending)
            return failed(d,GOOL_VM_INVALID);
        if(vm->receiver||(target->busy&&target->context.self!=current_index))return failed(d,GOOL_VM_UNSUPPORTED);
        d->current_index=current_index;bind(d);d->active=1;
        if(d->route.kind==GOOL_SIGNAL_STATE) {
            int result=gool_instance_enter_state(target,d->route.value,d->request.arguments,d->request.argument_count);
            if(result!=GOOL_VM_NEW_STATE)return failed(d,result<0?result:GOOL_VM_INVALID);
            result=start_pre(d);
            if(!d->active)return result;
        } else if(d->route.kind==GOOL_SIGNAL_SCRIPT) {
            gool_data owner;uint32_t pc;
            if(gool_signal_script(&target->base,&d->route,&owner,&pc))return failed(d,GOOL_VM_INVALID);
            if(d->request.argument_count>7||(uint64_t)vm->stack.top+GOOL_FRAME_WORDS+d->request.argument_count>gool_stack_capacity(&vm->stack))
                return failed(d,GOOL_VM_STACK);
            for(uint32_t n=0;n<d->request.argument_count;n++) {
                int result=gool_stack_push(&vm->stack,d->request.arguments[n]);
                if(result)return failed(d,GOOL_VM_STACK);
            }
            int result=gool_vm_invoke(vm,&owner,pc,d->request.argument_count);
            if(result)return failed(d,result);
            vm->mode=3;vm->respect_delay=0;d->phase=3;
        } else return failed(d,GOOL_VM_INVALID);
    }
    while(budget--) {
        if(target->state_generation!=d->generation)return failed(d,GOOL_VM_UNSUPPORTED);
        if(vm->send_pending)return GOOL_VM_SEND;
        int result=gool_vm_step(vm);
        if(result<0){target->active=0;return failed(d,result);}
        if(result==GOOL_VM_SEND)return result;
        if(result==GOOL_VM_RESUME_RETURN) {
            if(d->phase!=5||!target->resume_active||vm->stack.depth!=d->saved_depth)return failed(d,GOOL_VM_INVALID);
            target->resume_active=0;result=start_pre(d);
            if(!d->active)return result;
            continue;
        }
        if(result==GOOL_VM_NEW_STATE) {
            result=start_pre(d);
            if(!d->active)return result;
            continue;
        }
        if(result==GOOL_VM_DONE||result==GOOL_VM_WAIT||result==GOOL_VM_SIGNAL_RETURN||result==GOOL_VM_FAILED_SIGNAL)
            return failed(d,GOOL_VM_UNSUPPORTED);
        if(vm->stack.depth==d->saved_depth) {
            if(d->phase==5)return failed(d,GOOL_VM_INVALID);
            return finished(d);
        }
        if(vm->stack.depth<d->saved_depth)return failed(d,GOOL_VM_INVALID);
    }
    return GOOL_VM_BUDGET;
}
int gool_delivery_abort(gool_delivery *d,int status) {
    if(!d||status>=0)return GOOL_VM_INVALID;
    return failed(d,status);
}
