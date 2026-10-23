#include "gool_native_dispatch.h"

static int finish(gool_native_dispatch *d,int status,int32_t result) {
    if(!d->actor->attached||!d->actor->call_references)return GOOL_NATIVE_DISPATCH_INVALID;
    int error=gool_native_actor_stop(d->actor);
    if(error){d->error=error;d->status=error;return error;}
    d->actor->vm->mode=d->previous_mode;d->actor->vm->respect_delay=d->previous_delay;
    d->actor->vm->geometry_pending=d->previous_geometry_pending;
    d->actor->vm->geometry_instruction=d->previous_geometry_instruction;
    d->actor->vm->geometry_operand=d->previous_geometry_operand;
    d->actor->call_references--;
    d->native_result=result;d->active=0;d->status=status;return status;
}
int gool_native_dispatch_begin(gool_native_dispatch *d,gool_native_actor *a,uint32_t mode) {
    if(!d||d->active||!a||!a->attached||a->enabled||a->executing||a->error||
       !a->vm||a->call_references==UINT32_MAX||a->vm->status||a->vm->send_pending||((mode&8u)&&!a->vm->receiver))
        return GOOL_NATIVE_DISPATCH_INVALID;
    uint32_t previous_mode=a->vm->mode;int previous_delay=a->vm->respect_delay;
    int error=gool_native_actor_start(a);if(error)return error;
    /* Token setters use the current engine counter, including on previously
     * attached actors. The caller keeps this timeline stable until the entire
     * nested call tree has completed; creation time is not a frame clock. */
    if(a->tick_source)a->vm->tick=*a->tick_source;
    *d=(gool_native_dispatch){.actor=a,.previous_mode=previous_mode,.previous_delay=previous_delay,
        .previous_geometry_pending=a->vm->geometry_pending,
        .previous_geometry_instruction=a->vm->geometry_instruction,
        .previous_geometry_operand=a->vm->geometry_operand,
        .active=1,.status=GOOL_NATIVE_DISPATCH_BUDGET};
    a->vm->geometry_pending=0;
    a->call_references++;
    a->vm->mode=mode;
    /* Native38E28 itself does not run the UPDATE gate. Existing main tokens
     * may remain live while a signal/pre-run handler executes above them. */
    a->vm->respect_delay=0;return GOOL_NATIVE_DISPATCH_BUDGET;
}
int gool_native_dispatch_abort(gool_native_dispatch *d,int error) {
    if(!d||!d->active||!d->actor||error>=0)return GOOL_NATIVE_DISPATCH_INVALID;
    d->error=error;return finish(d,error,0);
}
int gool_native_dispatch_step(gool_native_dispatch *d,uint32_t instructions) {
    if(!d||!d->actor)return GOOL_NATIVE_DISPATCH_INVALID;
    if(!d->active)return d->status;
    if(d->status==GOOL_NATIVE_DISPATCH_SEND||d->status==GOOL_NATIVE_DISPATCH_STATE||
       d->status==GOOL_NATIVE_DISPATCH_GEOMETRY||d->status==GOOL_NATIVE_DISPATCH_SPAWN||d->status==GOOL_NATIVE_DISPATCH_RESOURCE)return d->status;
    if(d->status!=GOOL_NATIVE_DISPATCH_BUDGET||!d->actor->enabled||d->actor->executing)
        return GOOL_NATIVE_DISPATCH_INVALID;
    gool_vm *vm=d->actor->vm;
    while(instructions--) {
        uint32_t mode=vm->mode;
        int result=gool_native_actor_step(d->actor,1);
        if(result==GOOL_VM_BUDGET&&!vm->status&&!d->actor->error) {
            if((vm->instruction>>24)==0x31&&(mode&1u))return finish(d,0,-255);
            continue;
        }
        if(result<0)return gool_native_dispatch_abort(d,result);
        if(result==GOOL_VM_RUNNING) {
            /* Original399FC..39A78 returns from mode bit1 on this RETURN,
             * even though popping its frame leaves runnable parent bytecode. */
            if((vm->instruction>>24)==0x31&&(mode&1u))return finish(d,0,-255);
            continue;
        }
        if(result==GOOL_VM_DONE)return finish(d,0,-25);
        if(result==GOOL_VM_WAIT||result==GOOL_VM_SIGNAL_RETURN||result==GOOL_VM_RESUME_RETURN)
            return finish(d,0,-255);
        if(result==GOOL_VM_FAILED_SIGNAL)return finish(d,0,-26);
        if(result==GOOL_VM_SEND||result==GOOL_VM_NEW_STATE||result==GOOL_VM_STATE_REQUEST||result==GOOL_VM_GEOMETRY||result==GOOL_VM_SPAWN||result==GOOL_VM_RESOURCE) {
            int error=gool_native_actor_stop(d->actor);if(error)return gool_native_dispatch_abort(d,error);
            d->event_mode=mode;
            d->status=result==GOOL_VM_SEND?GOOL_NATIVE_DISPATCH_SEND:
                result==GOOL_VM_GEOMETRY?GOOL_NATIVE_DISPATCH_GEOMETRY:
                result==GOOL_VM_SPAWN?GOOL_NATIVE_DISPATCH_SPAWN:
                result==GOOL_VM_RESOURCE?GOOL_NATIVE_DISPATCH_RESOURCE:GOOL_NATIVE_DISPATCH_STATE;
            return d->status;
        }
        return gool_native_dispatch_abort(d,GOOL_NATIVE_DISPATCH_INVALID);
    }
    return GOOL_NATIVE_DISPATCH_BUDGET;
}
int gool_native_dispatch_resume(gool_native_dispatch *d,int complete,int32_t result) {
    if(!d||!d->active||!d->actor||(complete!=0&&complete!=1)||
       (d->status!=GOOL_NATIVE_DISPATCH_SEND&&d->status!=GOOL_NATIVE_DISPATCH_STATE&&
        d->status!=GOOL_NATIVE_DISPATCH_GEOMETRY&&d->status!=GOOL_NATIVE_DISPATCH_SPAWN&&d->status!=GOOL_NATIVE_DISPATCH_RESOURCE)||
       d->actor->enabled||d->actor->executing||d->actor->error||d->actor->vm->status<0||
       d->actor->vm->send_pending||d->actor->vm->geometry_pending)
        return GOOL_NATIVE_DISPATCH_INVALID;
    if(complete)return finish(d,0,result);
    int error=gool_native_actor_start(d->actor);if(error)return gool_native_dispatch_abort(d,error);
    d->status=GOOL_NATIVE_DISPATCH_BUDGET;return d->status;
}
