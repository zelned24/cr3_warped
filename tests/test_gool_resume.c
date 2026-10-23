/* Production C regression harness; run through tests/test_gool_c.py. */
#include <assert.h>
#include <stdio.h>
#include "gool_vm.h"
#ifdef NDEBUG
#error Assertions are required
#endif
static int32_t callback;
static int read_member(void *ctx,const gool_operand *arg,int32_t *value) {
    (void)ctx;
    if(arg->kind!=GOOL_ARG_VARIABLE||arg->value!=0x25)return GOOL_ARG_UNBOUND;
    *value=callback;return 0;
}
static void pack(uint8_t *p,uint32_t n){for(unsigned j=0;j<4;j++)p[j]=(uint8_t)(n>>(j*8));}
int main(void) {
    uint8_t code[16];const uint32_t program[]={0x2f000000,0x47800e25,0x31000000,0x31000000};
    for(unsigned n=0;n<4;n++)pack(code+n*4,program[n]);
    gool_data base={.code=code,.code_words=4,.stack_start=64,.lookup_index=0};
    gool_operand_context ctx={.base=&base,.read=read_member};gool_vm vm;
    int32_t argument=77;
    assert(gool_vm_init(&vm,&base,0,&ctx,&argument,1)==0);
    assert(gool_vm_invoke(&vm,&base,2,0)==0);vm.mode=0x13;vm.respect_delay=0;
    assert(gool_vm_step(&vm)==GOOL_VM_RESUME_RETURN&&vm.stack.depth==1&&vm.pc==0);
    assert(vm.frame_yield&&vm.stack.words[vm.frame_token_index]==0&&vm.stack.words[0]==77);
    assert(gool_stack_height(&vm.stack)==1);
    assert(gool_vm_init(&vm,&base,0,&ctx,NULL,0)==0);
    assert(gool_vm_address(&base,3,&callback)==0);
    assert(gool_vm_invoke(&vm,&base,1,0)==0);vm.mode=0x13;vm.respect_delay=0;
    assert(gool_vm_step(&vm)==0&&vm.mode==0x12);
    assert(gool_vm_step(&vm)==0&&vm.mode==0x13&&!vm.frame_yield&&vm.pc==2);
    assert(gool_vm_step(&vm)==GOOL_VM_RESUME_RETURN&&vm.frame_yield&&vm.stack.depth==1);
    puts("PASS RESUME RETURN/token harness; production C execution");return 0;
}
