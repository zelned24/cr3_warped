/* Production C regression harness; run through tests/test_gool_c.py. */
#include <assert.h>
#include <stdio.h>
#include "gool_vm.h"
#ifdef NDEBUG
#error Assertions are required
#endif
static int32_t members[512];
static int read_member(void *ctx,const gool_operand *arg,int32_t *value) {
    (void)ctx;
    if(arg->kind!=GOOL_ARG_VARIABLE||arg->value<0||arg->value>=512)return GOOL_ARG_UNBOUND;
    *value=members[arg->value];return 0;
}
static int write_member(void *ctx,const gool_operand *arg,int32_t value) {
    (void)ctx;
    if(arg->kind!=GOOL_ARG_VARIABLE||arg->value<0||arg->value>=512)return GOOL_ARG_UNBOUND;
    members[arg->value]=value;return 0;
}
static void pack(uint8_t *p,uint32_t value){for(unsigned j=0;j<4;j++)p[j]=(uint8_t)(value>>(j*8));}
int main(void) {
    uint8_t code[28];
    const uint32_t program[]={0x47e26e25,0x2f000000,0x2f000000,0x2f000000,
                              0x11b7ee36,0x11b7fe26,0x31000000};
    for(unsigned n=0;n<7;n++)pack(code+n*4,program[n]);
    gool_data base={.code=code,.code_words=7,.stack_start=64,.lookup_index=0};
    gool_operand_context ctx={.base=&base,.read=read_member,.write=write_member};gool_vm vm;
    assert(gool_vm_init(&vm,&base,0,&ctx,NULL,0)==0);
    assert(gool_vm_address(&base,4,&members[0x25])==0);members[0x26]=512;
    vm.mode=0x1234000b;assert(gool_stack_push(&vm.stack,77)==0&&gool_stack_push(&vm.stack,-22)==0);
    assert(gool_vm_step(&vm)==0&&vm.pc==4&&vm.mode==0x12340002);
    assert(gool_vm_step(&vm)==0&&gool_vm_step(&vm)==0&&gool_vm_step(&vm)==0);
    assert(members[0x36]==77&&members[0x26]==-22);
    assert(vm.pc==1&&vm.mode==0x1234000b&&vm.stack.depth==1&&gool_stack_height(&vm.stack)==0);
    assert(gool_vm_init(&vm,&base,0,&ctx,NULL,0)==0);members[0x25]=0;members[0x26]=-1;
    vm.stack.words[vm.stack.top]=765;
    assert(gool_vm_step(&vm)==0&&gool_stack_height(&vm.stack)==1);
    int32_t value;assert(gool_stack_pop(&vm.stack,&value)==0&&value==765);
    assert(gool_vm_init(&vm,&base,0,&ctx,NULL,0)==0);members[0x26]=0x40000000;
    assert(gool_vm_step(&vm)==0&&gool_stack_height(&vm.stack)==0);
    pack(code,0x47e26b80);members[0x26]=1;
    assert(gool_vm_init(&vm,&base,0,&ctx,NULL,0)==0);
    assert(gool_stack_push(&vm.stack,99)==0);
    assert(gool_vm_step(&vm)==0&&gool_stack_height(&vm.stack)==0);
    pack(code,0x47e26b90);members[0x26]=0;
    assert(gool_vm_init(&vm,&base,0,&ctx,NULL,0)==0);
    assert(gool_vm_step(&vm)==GOOL_VM_UNSUPPORTED&&vm.pc==0);
    puts("PASS JALR/RETURN harness; production C execution");return 0;
}
