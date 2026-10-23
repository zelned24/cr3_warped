#include "gool_vm.h"
#include "audio_native.h"
#include <assert.h>
#include <stdio.h>

static int outcome,calls;
static uint32_t captured;
static audio_native_services services;
static uint32_t next_pc;
static int control(void *context,uint32_t target,uint32_t selector,uint32_t source,
                   uint32_t node,int32_t *result) {
    gool_native_actor *actor=context;
    assert(!target&&!selector&&node==actor->node);
    assert(source==actor->stack_address+(actor->vm->stack.top-1u)*4u);
    *result=0x12345678;
    return gool_domain_write32(actor->registry->domain,node+0xc0,next_pc);
}
static int actor_sound(void *context,uint32_t instruction) {
    return gool_native_actor_sound(context,instruction,&services);
}
static void actor_wrapper(void) {
    uint8_t code[]={0x1f,0x0e,0,0x42,0,0,0,0x31,0,0,0,0x31};
    gool_data data={.code=code,.code_words=3,.stack_start=64};
    gool_operand_context context={.base=&data};
    for(unsigned pending=0;pending<2;pending++) {
        gool_vm vm;assert(!gool_vm_init(&vm,&data,0,&context,NULL,0));
        assert(!gool_stack_push(&vm.stack,123));
        gool_domain domain={0};uint8_t object[2048]={0};gool_domain_lease lease,stack_lease;
        assert(!gool_domain_map_mutable(&domain,0x81200000,object,sizeof(object),&lease));
        gool_native_actor actor={.vm=&vm,.node=0x81200000,.attached=1,.enabled=1,.executing=1,
            .stack_address=0x81200140,.stack_words=GOOL_STACK_WORDS};
        gool_native_actor *actors[]={&actor};
        gool_native_registry registry={.domain=&domain,.actors=actors,.count=1};actor.registry=&registry;
        assert(!gool_domain_map_overlay(&domain,lease,actor.stack_address,vm.stack.words,sizeof(vm.stack.words),&stack_lease));
        services=(audio_native_services){.context=&actor,.control=control};
        vm.sound=actor_sound;vm.sound_context=&actor;
        next_pc=pending?0:0x50000008;
        assert(!gool_domain_write32(&domain,actor.node+0xc0,0x50000004));
        uint32_t top=vm.stack.top;
        assert(!gool_vm_step(&vm)&&vm.stack.top==top-1);
        assert(vm.has_pending_pc==(int)pending);
        if(pending)assert(!vm.pending_pc);
        else assert(vm.pc==2);
        uint32_t result;assert(!gool_domain_read32(&domain,actor.node+0xd4,&result)&&result==0x12345678);
        assert(!gool_domain_release(&domain,stack_lease));assert(!gool_domain_release(&domain,lease));
    }
}
static int sound(void *context,uint32_t instruction) {
    gool_vm *vm=context;calls++;captured=instruction;
    int32_t value;assert(!gool_stack_pop(&vm->stack,&value)&&value==123);
    if(!outcome)vm->pc=2;
    return outcome;
}
int main(void) {
    actor_wrapper();
    uint8_t code[]={0x1f,0xfe,0x1f,0x41,0,0,0,0x31,0,0,0,0x31};
    gool_data data={.code=code,.code_words=3,.stack_start=64};
    gool_operand_context context={.base=&data};gool_vm vm;
    for(unsigned op=0x41;op<=0x42;op++) {
        code[3]=(uint8_t)op;
        assert(!gool_vm_init(&vm,&data,0,&context,NULL,0));
        assert(!gool_stack_push(&vm.stack,123));uint32_t top=vm.stack.top;
        assert(gool_vm_step(&vm)==GOOL_VM_UNSUPPORTED&&vm.pc==0&&vm.stack.top==top);
        for(outcome=-77;outcome<=1;outcome++) {
            if(outcome!=-77&&outcome!=0&&outcome!=1)continue;
            assert(!gool_vm_init(&vm,&data,0,&context,NULL,0));
            assert(!gool_stack_push(&vm.stack,123));
            vm.sound=sound;vm.sound_context=&vm;calls=0;
            int result=gool_vm_step(&vm);
            assert(calls==1&&captured==((op<<24)|0x1ffe1fu));
            assert(vm.stack.top==top-1);
            if(!outcome) {
                assert(!result&&vm.pc==2&&!vm.status);
                assert(gool_vm_step(&vm)==GOOL_VM_DONE&&calls==1);
            } else assert(result==(outcome<0?outcome:GOOL_VM_INVALID)&&vm.pc==0);
        }
    }
    puts("PASS GOOL sound: provider requirement, raw instruction, exact next PC, failure and non-replay stack effects");
    return 0;
}
