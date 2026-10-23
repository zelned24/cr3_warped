/* Production C regression harness; run through tests/test_gool_c.py. */
#include <assert.h>
#include <stdio.h>
#include "gool_vm.h"
#ifdef NDEBUG
#error Assertions are required
#endif
int main(void) {
    /* Native SET_GEO model 0/frame 0, mirror NONE, duration 6. */
    uint8_t code[]={0,0,0xc6,0x38,0,0,0,0x31};
    const uint8_t constants[4]={0};
    gool_data base={.code=code,.code_words=2,.constants=constants,.constant_words=1,.stack_start=64};
    gool_operand_context context={.base=&base};
    gool_vm vm;
    /* Frame setters need real member write providers. Gate/seed can be
     * checked independently without faking successful renderer writes. */
    assert(gool_vm_init(&vm,&base,0,&context,NULL,0)==0);
    assert(gool_vm_seed_frame(&vm)==0&&vm.frame_yield&&gool_stack_height(&vm.stack)==1);
    assert(gool_vm_step(&vm)==GOOL_VM_WAIT);
    assert(gool_vm_resume_frame(&vm,17)==0&&!vm.frame_yield&&gool_stack_height(&vm.stack)==0);
    assert(gool_vm_resume_frame(&vm,16)==GOOL_VM_INVALID);
    assert(gool_stack_push(&vm.stack,0x06000011)==0);
    vm.frame_token_index=vm.stack.top-1;vm.frame_yield=1;
    assert(gool_vm_resume_frame(&vm,22)==GOOL_VM_WAIT);
    assert(gool_vm_resume_frame(&vm,23)==0&&gool_stack_height(&vm.stack)==0);
    assert(gool_stack_push(&vm.stack,0x01000011)==0);
    vm.frame_token_index=vm.stack.top-1;vm.frame_yield=1;
    assert(gool_vm_resume_frame(&vm,23)==0); /* count 1 does not gate */
    assert(gool_vm_seed_frame(&vm)==0);
    assert(gool_stack_push(&vm.stack,88)==0);
    uint32_t top=vm.stack.top;
    assert(gool_vm_resume_frame(&vm,23)==GOOL_VM_STACK&&vm.stack.top==top);
    puts("PASS native frame gate fixtures only; renderer/member integration pending");return 0;
}
