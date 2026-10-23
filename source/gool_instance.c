#include "gool_instance.h"
#include <string.h>

static int32_t signed_bits(uint32_t bits) {
    return bits<=INT32_MAX?(int32_t)bits:(int32_t)((int64_t)bits-INT64_C(4294967296));
}
int gool_instance_enter_state(gool_instance *i,uint32_t index,const int32_t *arguments,uint32_t argc) {
    if(!i||!i->ready||!i->active||i->vm.status||index==255||i->state_generation==UINT64_MAX||
       (uint64_t)argc+GOOL_FRAME_WORDS+1>gool_stack_capacity(&i->vm.stack)||(argc&&!arguments))return GOOL_VM_INVALID;
    if(i->has_resume||i->resume_active)return GOOL_VM_UNSUPPORTED;
    gool_vm *vm=&i->vm;gool_state_data state;
    if(gool_data_state(&i->base,index,&state))return GOOL_VM_INVALID;
    int32_t resume;
    if(gool_object_get(&i->object,0x24,&resume))return GOOL_VM_OPERAND;
    gool_data resume_code={0};uint32_t resume_pc=0;
    if(resume&&gool_vm_resolve(vm,resume,&resume_code,&resume_pc))return GOOL_VM_INVALID;
    if(resume&&(uint64_t)argc+GOOL_FRAME_WORDS*2>gool_stack_capacity(&vm->stack))return GOOL_VM_STACK;
    /* Resolve everything before replacing the current frame/state. No guesses
     * for a missing owner or handler. Globals and external providers survive. */
    gool_data alternate,main_code,pre_code,signal_code;
    uint32_t main_pc,pre_pc=0,signal_pc=0;
    if(gool_data_open_relative(&alternate,&i->base,state.alternate_eid)||
       gool_data_script(&i->base,index,GOOL_SCRIPT_MAIN,&main_code,&main_pc))return GOOL_VM_INVALID;
    int pre=gool_data_script(&i->base,index,GOOL_SCRIPT_PRE_RUN,&pre_code,&pre_pc);
    int signal=gool_data_script(&i->base,index,GOOL_SCRIPT_SIGNAL,&signal_code,&signal_pc);
    int32_t pre_pointer=0,signal_pointer=0,main_pointer,collision;
    if(pre<0||signal<0||
       (!pre&&gool_vm_address(&pre_code,pre_pc,&pre_pointer))||
       (!signal&&gool_vm_address(&signal_code,signal_pc,&signal_pointer))||
       gool_vm_address(&main_code,main_pc,&main_pointer))return GOOL_VM_INVALID;
    if(gool_object_get(&i->object,0x1a,&collision))return GOOL_VM_OPERAND;
    if((uint64_t)argc+GOOL_FRAME_WORDS+1>gool_stack_capacity(&i->vm.stack))return GOOL_VM_STACK;
    if(argc)memmove(vm->stack.words,arguments,argc*sizeof(int32_t));
    vm->stack.top=argc;vm->stack.depth=0;
    /* Already capacity-checked. Do not erase old unused slots: the reference
     * reuses them, including the reserve-slot semantics of SET_GLOBAL. */
    gool_stack_enter(&vm->stack,argc,main_pointer);
    vm->frame_yield=0;
    if(!resume)gool_vm_seed_frame(vm);
    i->has_resume=resume!=0;
    if(resume){i->resume_run=resume_code;i->resume_pc=resume_pc;}
    i->alternate=alternate;i->state=index;i->state_generation++;i->has_pre_run=!pre;
    if(!pre){i->pre_run=pre_code;i->pre_pc=pre_pc;}
    vm->code=main_code;vm->pc=main_pc;vm->has_pending_pc=0;vm->next_update=0;
    gool_object_set(&i->object,0x28,signed_bits((uint32_t)vm->tick));
    gool_object_set(&i->object,0x1c,signed_bits(state.interact_flags));
    gool_object_set(&i->object,0x30,signed_bits(state.motion_flags));
    gool_object_set(&i->object,0x1a,signed_bits((uint32_t)collision|0x20020));
    gool_object_set(&i->object,0x22,pre_pointer);
    gool_object_set(&i->object,0x23,signal_pointer);
    gool_object_set(&i->object,0x24,0);
    return GOOL_VM_NEW_STATE;
}
int gool_instance_start_resume(gool_instance *i) {
    if(!i||!i->ready||!i->active||!i->has_resume||i->resume_active||i->vm.status)return GOOL_VM_INVALID;
    int result=gool_vm_invoke(&i->vm,&i->resume_run,i->resume_pc,0);
    if(result)return result;
    i->has_resume=0;i->resume_active=1;i->vm.mode=0x13;i->vm.respect_delay=0;
    return GOOL_VM_RUNNING;
}
static int change_state(void *context,uint32_t index,uint32_t argc,int meets) {
    gool_instance *i=context;
    if(!i||!i->ready||!i->active)return GOOL_VM_INVALID;
    gool_vm *vm=&i->vm;
    if(!vm->stack.depth||vm->stack.depth>GOOL_STACK_FRAMES||vm->stack.top>gool_stack_capacity(&vm->stack)||
       vm->stack.frames[vm->stack.depth-1]>gool_stack_capacity(&vm->stack)-GOOL_FRAME_WORDS||
       vm->stack.top<vm->stack.frames[vm->stack.depth-1]+GOOL_FRAME_WORDS||
       argc>gool_stack_height(&vm->stack)||argc>15)return GOOL_VM_STACK;
    if(!meets) {
        vm->stack.top-=argc;return GOOL_VM_RUNNING;
    }
    /* Native wrapper pops/copies arguments before compatibility. Errors after
     * this point retain that stack effect, including unbound engine members. */
    int32_t arguments[15];
    if(argc)memcpy(arguments,vm->stack.words+vm->stack.top-argc,argc*sizeof(int32_t));
    vm->stack.top-=argc;
    gool_state_data state;int32_t interact,mode;
    if(gool_data_state(&i->base,index,&state))return GOOL_VM_INVALID;
    if(gool_object_get(&i->object,0x32,&mode)||gool_object_get(&i->object,0x1c,&interact))return GOOL_VM_OPERAND;
    uint32_t flags=(uint32_t)interact;
    if((uint32_t)mode-2u<3u)flags|=0x402u;
    if(flags&state.motion_flags)return GOOL_VM_RUNNING;
    return gool_instance_enter_state(i,index,arguments,argc);
}
int gool_instance_init_data(gool_instance *out,const gool_data *base,
                            const gool_object_context *context,const gool_placement *p) {
    if(!out||out->ready||!base||!context||!p||base->eid!=p->behavior_eid||!context->objects||context->self>=context->count||
       context->objects[context->self]!=&out->object||p->class_index>65535||p->object_id>65535||
       context->self>0xffffff||p->path_points>0xffffff||
       p->parameter_count>GOOL_STACK_WORDS-GOOL_FRAME_WORDS-1||(p->parameter_count&&!p->parameters))return GOOL_VM_INVALID;
    gool_instance next={0};next.context=*context;next.base=*base;
    int32_t state_index;
    if(gool_data_object(&next.base,p->object_id,&state_index)||state_index<0||state_index==255)return GOOL_VM_INVALID;
    gool_state_data state;
    if(gool_data_state(&next.base,(uint32_t)state_index,&state)||
       gool_data_open_relative(&next.alternate,&next.base,state.alternate_eid))return GOOL_VM_INVALID;
    gool_data main_code,signal_code;uint32_t main_pc,signal_pc;
    if(gool_data_script(&next.base,(uint32_t)state_index,GOOL_SCRIPT_MAIN,&main_code,&main_pc))return GOOL_VM_INVALID;
    int pre=gool_data_script(&next.base,(uint32_t)state_index,GOOL_SCRIPT_PRE_RUN,&next.pre_run,&next.pre_pc);
    int signal=gool_data_script(&next.base,(uint32_t)state_index,GOOL_SCRIPT_SIGNAL,&signal_code,&signal_pc);
    if(pre<0||signal<0)return GOOL_VM_INVALID;
    int32_t pre_pointer=0,signal_pointer=0;
    if((!pre&&gool_vm_address(&next.pre_run,next.pre_pc,&pre_pointer))||
       (!signal&&gool_vm_address(&signal_code,signal_pc,&signal_pointer)))return GOOL_VM_INVALID;
    /* Initialization reads the base stack origin immediately. Bind the staged
     * state until it is committed, then rebind all borrowed pointers below. */
    gool_operand_context args={.base=&next.base,.alternate=&next.alternate,
        .context=&next.context,.read=gool_object_read,.write=gool_object_write};
    if(gool_vm_init(&next.vm,&main_code,main_pc,&args,p->parameters,p->parameter_count))return GOOL_VM_INVALID;
    int32_t main_pointer;
    if(gool_vm_address(&main_code,main_pc,&main_pointer))return GOOL_VM_INVALID;
    next.vm.stack.words[next.vm.stack.frames[0]+1]=main_pointer;
    if(gool_vm_seed_frame(&next.vm))return GOOL_VM_STACK;
    gool_object_reset(&next.object,&out->vm);
    for(uint32_t i=0;i<3;i++) {
        gool_object_set(&next.object,8+i,p->position[i]);
        gool_object_set(&next.object,11+i,0);
        gool_object_set(&next.object,14+i,4096);
    }
    gool_object_set(&next.object,0x1a,0x20020); /* initial collision + IN_NEW_STATE */
    gool_object_set(&next.object,0x1c,signed_bits(state.interact_flags));
    gool_object_set(&next.object,0x1d,signed_bits(p->class_index<<16|p->object_id));
    gool_object_set(&next.object,0x1e,signed_bits(context->self<<8));
    gool_object_set(&next.object,0x22,pre_pointer);
    gool_object_set(&next.object,0x23,signal_pointer);
    gool_object_set(&next.object,0x24,0); /* no prior state RESUME on placement init */
    gool_object_set(&next.object,0x27,-1);
    gool_object_set(&next.object,0x28,signed_bits((uint32_t)p->tick));
    gool_object_set(&next.object,0x2e,signed_bits(p->path_points<<8));
    gool_object_set(&next.object,0x30,signed_bits(state.motion_flags));
    gool_object_set(&next.object,0x38,signed_bits(p->geo_format));
    gool_object_set(&next.object,0x3b,-2);
    gool_object_set(&next.object,0x3e,65535);
    next.state=(uint32_t)state_index;next.state_generation=1;
    next.has_pre_run=!pre;next.ready=next.active=1;
    next.vm.tick=p->tick;
    next.vm.change_state=change_state;next.vm.state_context=out;
    *out=next;
    out->vm.external.base=&out->base;
    out->vm.external.alternate=&out->alternate;
    out->vm.external.context=&out->context;
    return GOOL_VM_RUNNING;
}
int gool_instance_init(gool_instance *out,const cga_archive *archive,
                       const gool_object_context *context,const gool_placement *p) {
    gool_data base;
    if(!p||gool_data_open(&base,archive,p->behavior_eid))return GOOL_VM_INVALID;
    return gool_instance_init_data(out,&base,context,p);
}
static int state_phase(gool_instance *i,uint64_t tick) {
    if(i->has_resume) {
        i->phase=4;i->handler_depth=i->vm.stack.depth;
        return gool_instance_start_resume(i);
    }
    int32_t pointer;gool_data handler;uint32_t pc;
    if(gool_object_get(&i->object,0x22,&pointer))return GOOL_VM_OPERAND;
    if(pointer&&gool_vm_resolve(&i->vm,pointer,&handler,&pc))return GOOL_VM_INVALID;
    i->phase=pointer?1:2;i->vm.mode=pointer?3:4;
    gool_vm_set_tick(&i->vm,tick,!pointer);
    if(pointer) {
        i->handler_depth=i->vm.stack.depth;
        return gool_vm_invoke(&i->vm,&handler,pc,0);
    }
    return GOOL_VM_RUNNING;
}
int gool_instance_update(gool_instance *i,uint64_t tick,uint32_t budget) {
    if(!i||!i->ready)return GOOL_VM_INVALID;
    if(!i->active)return i->vm.status?i->vm.status:GOOL_VM_DONE;
    if(!budget)return GOOL_VM_BUDGET;
    if(i->resume_active&&(!i->busy||i->phase!=4))return GOOL_VM_UNSUPPORTED;
    if(i->busy) {
        if(tick!=i->update_tick)return GOOL_VM_INVALID;
    } else {
        if(i->has_resume&&tick!=i->vm.tick)return GOOL_VM_INVALID;
        if(i->has_last_tick&&tick<i->last_tick)return GOOL_VM_INVALID;
        if(i->has_last_tick&&tick==i->last_tick)return i->has_resume?GOOL_VM_UNSUPPORTED:GOOL_INSTANCE_SKIPPED;
        /* PRE_RUN is mutable script state. Resolve its live value each update
         * rather than silently invoking the handler from the initial state. */
        int32_t pointer;
        int result=i->has_resume?0:gool_object_get(&i->object,0x22,&pointer);
        if(result)return GOOL_VM_OPERAND;
        if(i->has_resume)pointer=0;
        gool_data handler;uint32_t handler_pc=0;
        if(pointer&&gool_vm_resolve(&i->vm,pointer,&handler,&handler_pc))return GOOL_VM_INVALID;
        result=gool_object_begin_update(&i->object,tick);
        if(result)return result;
        i->update_tick=tick;i->busy=1;i->phase=pointer?1:2;
        i->vm.mode=pointer?3:4;
        if(i->has_resume) {
            result=state_phase(i,tick);
            if(result){i->active=0;i->vm.status=result;return result;}
        } else if(pointer) {
            i->handler_depth=i->vm.stack.depth;
            result=gool_vm_invoke(&i->vm,&handler,handler_pc,0);
            if(result){i->active=0;i->vm.status=result;return result;}
            gool_vm_set_tick(&i->vm,tick,0);
        }
    }
    for(uint32_t n=0;n<budget;n++) {
        if(i->phase==2) {
            i->vm.mode=4;
            int gate=gool_vm_resume_frame(&i->vm,tick);
            if(gate==GOOL_VM_WAIT) {
                i->busy=0;i->has_last_tick=1;i->last_tick=tick;return gate;
            }
            if(gate<0){i->active=0;i->vm.status=gate;return gate;}
            i->phase=3;
        }
        int result=gool_vm_step(&i->vm);
        if(result<0){i->active=0;return result;}
        if(result==GOOL_VM_SIGNAL_RETURN||result==GOOL_VM_FAILED_SIGNAL)return result;
        if(result==GOOL_VM_SEND||result==GOOL_VM_SEND_REPLACED)return result;
        if(result==GOOL_VM_NEW_STATE) {
            result=state_phase(i,tick);
            if(result<0){i->active=0;i->vm.status=result;return result;}
            continue;
        }
        if(result==GOOL_VM_RESUME_RETURN) {
            if(i->phase!=4||!i->resume_active||i->vm.stack.depth!=i->handler_depth)result=GOOL_VM_INVALID;
            else {i->resume_active=0;result=state_phase(i,tick);}
            if(result<0){i->active=0;i->vm.status=result;return result;}
            continue;
        }
        if(result==GOOL_VM_DONE){i->active=0;i->busy=0;return result;}
        if(i->phase==1&&i->vm.stack.depth==i->handler_depth) {
            i->phase=2;gool_vm_set_tick(&i->vm,tick,1);
        }
        if(result==GOOL_VM_WAIT) {
            i->busy=0;i->has_last_tick=1;i->last_tick=tick;return result;
        }
    }
    return GOOL_VM_BUDGET;
}
