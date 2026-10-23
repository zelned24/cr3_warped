/* Production C regression harness; run through tests/test_gool_c.py. */
#include <assert.h>
#include <stdio.h>
#include "gool_send.h"
#ifdef NDEBUG
#error Assertions are required
#endif
static int32_t members[512];
static int read_member(void *unused,const gool_operand *arg,int32_t *out) {
    (void)unused;
    if(arg->kind!=GOOL_ARG_VARIABLE||arg->value<0||arg->value>=512)return GOOL_ARG_UNBOUND;
    *out=members[arg->value];return 0;
}
static int write_member(void *unused,const gool_operand *arg,int32_t value) {
    (void)unused;
    if(arg->kind!=GOOL_ARG_VARIABLE||arg->value<0||arg->value>=512)return GOOL_ARG_UNBOUND;
    members[arg->value]=value;return 0;
}
static void pack(uint8_t *p,uint32_t n) {for(unsigned j=0;j<4;j++)p[j]=(uint8_t)(n>>(j*8));}
int main(void) {
    uint8_t code[32]={0};
    gool_data base={.code=code,.code_words=8,.stack_start=64};
    gool_operand_context context={.base=&base,.read=read_member,.write=write_member};
    gool_vm vm;
    members[0]=0x60000000;members[8]=1;members[0x1a]=0x20020;members[0x25]=0;
    pack(code,0x3c000000|(2u<<18)|(8u<<12)|0xe25);
    assert(gool_vm_init(&vm,&base,0,&context,NULL,0)==0);
    vm.send_enabled=1;vm.mode=4;
    assert(gool_stack_push(&vm.stack,11)==0&&gool_stack_push(&vm.stack,22)==0);
    assert(gool_vm_step(&vm)==GOOL_VM_SEND&&vm.send_pending&&vm.pc==1);
    assert(vm.send.raw_signal==0&&vm.send.target==0x60000000&&vm.send.argument_count==2);
    assert(vm.send.arguments[0]==11&&vm.send.arguments[1]==22&&members[0x1a]==0x20);
    uint32_t top=vm.stack.top;
    assert(gool_vm_step(&vm)==GOOL_VM_SEND&&vm.stack.top==top&&vm.pc==1);
    members[0x25]=1;
    assert(gool_send_finish(&vm)==0&&!vm.send_pending&&gool_stack_height(&vm.stack)==0);
    /* Native service replacement keeps the new wait token only in mode&2. */
    for(unsigned mode=3;mode<=4;mode++) {
        members[0x1a]=0x20020;
        assert(gool_vm_init(&vm,&base,0,&context,NULL,0)==0);
        vm.send_enabled=1;vm.mode=mode;
        assert(gool_stack_push(&vm.stack,11)==0&&gool_stack_push(&vm.stack,22)==0);
        assert(gool_vm_step(&vm)==GOOL_VM_SEND);
        vm.stack.top=3;vm.pc=6;assert(gool_vm_seed_frame(&vm)==0);
        members[0x1a]|=0x20000;
        assert(gool_send_finish(&vm)==GOOL_VM_SEND_REPLACED&&vm.pc==6);
        assert(gool_stack_height(&vm.stack)==(mode==3?1u:0u));
        assert(vm.frame_yield==(mode==3));
    }
    assert(gool_vm_init(&vm,&base,0,&context,NULL,0)==0);vm.send_enabled=1;vm.mode=4;
    assert(gool_stack_push(&vm.stack,100)==0&&gool_stack_push(&vm.stack,200)==0);
    assert(gool_vm_step(&vm)==GOOL_VM_SEND);
    vm.stack.top=3;
    assert(gool_stack_push(&vm.stack,99)==0&&gool_vm_seed_frame(&vm)==0);
    assert(gool_send_finish(&vm)==GOOL_VM_RUNNING&&!vm.frame_yield);
    assert(gool_stack_height(&vm.stack)==0);
    assert(gool_vm_init(&vm,&base,0,&context,NULL,0)==0);
    assert(gool_vm_step(&vm)==GOOL_VM_UNSUPPORTED); /* no delivery owner */
    pack(code,0x3c000000|(8u<<12)|0xbe0);
    assert(gool_vm_init(&vm,&base,0,&context,NULL,0)==0);vm.send_enabled=1;
    assert(gool_vm_step(&vm)==0&&!vm.send_pending&&members[0x25]==0);
    puts("PASS deferred SEND frontend only; target delivery is not mocked as implemented");return 0;
}
