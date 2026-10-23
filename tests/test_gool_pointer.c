/* Production C regression harness; run through tests/test_gool_c.py. */
#include <assert.h>
#include <stdio.h>
#include "gool_vm.h"
#ifdef NDEBUG
#error Assertions are required
#endif
static int32_t members[2];
static int32_t code_pointer;
static int read_member(void *ctx,const gool_operand *arg,int32_t *value) {
    (void)ctx;
    if(arg->kind!=GOOL_ARG_VARIABLE||arg->value<0x25||arg->value>0x26)return GOOL_ARG_UNBOUND;
    *value=members[arg->value-0x25];return 0;
}
static int write_member(void *ctx,const gool_operand *arg,int32_t value) {
    (void)ctx;
    if(arg->kind!=GOOL_ARG_VARIABLE)return GOOL_ARG_UNBOUND;
    /* Native392B8/38DB0 forms object+40+4*destination; index20 is C0.
     * The executing VM's cached PC continues after the raw provider store. */
    if(arg->value==0x20){code_pointer=value;return 0;}
    if(arg->value<0x25||arg->value>0x26)return GOOL_ARG_UNBOUND;
    members[arg->value-0x25]=value;return 0;
}
static void pack(uint8_t *p,uint32_t n){for(unsigned j=0;j<4;j++)p[j]=(uint8_t)(n>>(j*8));}
int main(void) {
    uint8_t code[20];
    uint32_t program[]={0x187c0004,0x11e1fe25,0x47800e25,0x2f000000,0x31000000};
    for(unsigned j=0;j<5;j++)pack(code+j*4,program[j]);
    gool_data base={.code=code,.code_words=5,.stack_start=64,.lookup_index=0};
    gool_data alternate={.code=code,.code_words=5,.stack_start=64,.lookup_index=1};
    gool_operand_context ctx={.base=&base,.read=read_member,.write=write_member};gool_vm vm;
    assert(gool_vm_init(&vm,&base,0,&ctx,NULL,0)==0);vm.mode=3;
    for(unsigned j=0;j<4;j++)assert(gool_vm_step(&vm)==0);
    assert(members[0]==0x50000010&&vm.pc==3&&vm.mode==3&&vm.stack.depth==1);
    assert(gool_stack_height(&vm.stack)==0);
    /* Pointer arithmetic may form an unfetched PC beyond the code view. */
    pack(code,0x18000000|(0x26u<<18)|0x4009);ctx.alternate=&alternate;
    assert(gool_vm_init(&vm,&base,0,&ctx,NULL,0)==0);
    assert(gool_vm_step(&vm)==0&&members[1]==0x50040024);
    pack(code,0x47800e26);
    assert(gool_vm_init(&vm,&base,0,&ctx,NULL,0)==0);
    assert(gool_vm_step(&vm)==GOOL_VM_INVALID&&vm.pc==0);
    /* Null sentinel precedes the otherwise absent alternate owner. */
    pack(code,0x187c7fff);ctx.alternate=NULL;
    assert(gool_vm_init(&vm,&base,0,&ctx,NULL,0)==0);
    assert(gool_vm_step(&vm)==0);int32_t value;
    assert(gool_stack_pop(&vm.stack,&value)==0&&value==0);
    /* Destination allocation precedes source-owner failure. */
    pack(code,0x187c4000);
    assert(gool_vm_init(&vm,&base,0,&ctx,NULL,0)==0);
    uint32_t slot=vm.stack.top;vm.stack.words[slot]=765;
    assert(gool_vm_step(&vm)==GOOL_VM_INVALID&&vm.stack.top==slot+1&&vm.stack.words[slot]==765);
    pack(code,0x18800000); /* live SCRIPT_PTR destination: provider store, no fake jump */
    assert(gool_vm_init(&vm,&base,0,&ctx,NULL,0)==0);
    assert(gool_vm_step(&vm)==0&&vm.pc==1&&code_pointer==0x50000000);
    puts("PASS code-pointer/JALR harness; production C execution");return 0;
}
