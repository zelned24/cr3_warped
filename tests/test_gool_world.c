/* Production world admission dispatches a real per-instance UPDATE driver. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "gool_world.h"
#include "gool_instance.h"
#ifdef NDEBUG
#error Assertions are required
#endif

typedef struct {
    gool_instance *current;
    int phase,bind_error,stamp_error,dispatches;
} owner;

static int bind_current(void *context,void *object) {
    owner *state=context;
    if(state->bind_error)return state->bind_error;
    state->current=object;state->phase=1;
    return 0;
}
static int stamp_update(void *context,void *object,uint32_t tick) {
    owner *state=context;gool_instance *instance=object;
    assert(state->phase==1&&state->current==instance);
    if(state->stamp_error)return state->stamp_error;
    state->phase=2;
    return gool_object_set(&instance->object,0x27,(int32_t)tick);
}
static int dispatch(void *context,void *object,const gool_world_decision *decision) {
    owner *state=context;gool_instance *instance=object;
    assert(state->current==instance);
    assert(state->phase==(decision->run_update?2:1));
    state->dispatches++;
    if(decision->run_update)return gool_instance_update(instance,decision->tick,100);
    return GOOL_INSTANCE_SKIPPED;
}
static int32_t member(gool_instance *instance,uint32_t index) {
    int32_t result;
    assert(gool_object_get(&instance->object,index,&result)==0);
    return result;
}
int main(void) {
    /* Same raw pre-run/main fixture used by the instance-driver regression. */
    const uint32_t words[]={0x11e26e25,0x39c30a00,0x320003fd,0x2f000000,
                            0x00a01e26,0x11e1fe26,0x31000000};
    uint8_t code[sizeof(words)];
    for(unsigned i=0;i<7;i++)for(unsigned byte=0;byte<4;byte++)
        code[i*4+byte]=(uint8_t)(words[i]>>(byte*8));
    gool_instance instance={0};gool_object *objects[]={&instance.object};
    instance.context=(gool_object_context){.objects=objects,.count=1,.self=0};
    instance.base=(gool_data){.code=code,.code_words=7,.stack_start=512};
    instance.alternate=instance.base;instance.alternate.lookup_index=1;
    gool_operand_context args={.base=&instance.base,.alternate=&instance.alternate,
        .context=&instance.context,.read=gool_object_read,.write=gool_object_write};
    assert(gool_vm_init(&instance.vm,&instance.base,0,&args,NULL,0)==0);
    gool_object_reset(&instance.object,&instance.vm);
    assert(gool_object_set(&instance.object,0x26,0)==0);
    assert(gool_object_set(&instance.object,0xe,4096)==0);
    assert(gool_object_set(&instance.object,0x22,0x50000010)==0);
    assert(gool_object_set(&instance.object,0x27,-1)==0);
    instance.ready=instance.active=1;
    owner state={0};
    gool_world_services services={.context=&state,.bind_current=bind_current,
        .stamp_update=stamp_update,.dispatch=dispatch};
    gool_world_input input={.world_flags=0x28,.requested_update=1,.category=0x100};
    assert(gool_world_step(&instance,&input,&services)==GOOL_VM_WAIT);
    assert(member(&instance,0x26)==16&&member(&instance,0x27)==0);
    input.counter=34;input.world_flags=8;
    uint32_t pc=instance.vm.pc;
    assert(gool_world_step(&instance,&input,&services)==GOOL_INSTANCE_SKIPPED);
    assert(instance.vm.pc==pc&&instance.last_tick==0);
    assert(member(&instance,0x26)==16&&member(&instance,0x27)==0);
    input.world_flags=0x28;input.requested_update=0;input.status_100=0x40000;
    assert(gool_world_step(&instance,&input,&services)==GOOL_VM_WAIT);
    assert(member(&instance,0x26)==32&&member(&instance,0x27)==1);
    assert(state.dispatches==3);
    state.bind_error=-700;
    assert(gool_world_step(&instance,&input,&services)==-700);
    assert(state.dispatches==3);
    state.bind_error=0;state.stamp_error=-701;
    assert(gool_world_step(&instance,&input,&services)==-701);
    assert(state.dispatches==3&&member(&instance,0x27)==1);
    gool_world_services missing=services;missing.dispatch=NULL;
    state.phase=99;
    assert(gool_world_step(&instance,&input,&missing)==GOOL_WORLD_INVALID);
    assert(state.phase==99&&state.dispatches==3);
    assert(gool_world_step(NULL,&input,&services)==GOOL_WORLD_INVALID);
    assert(gool_world_step(&instance,NULL,&services)==GOOL_WORLD_INVALID);
    assert(gool_world_step(&instance,&input,NULL)==GOOL_WORLD_INVALID);
    puts("PASS production world tick/admission, real instance UPDATE, required providers and failures");
    return 0;
}
