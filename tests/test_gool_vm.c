/* Native dispatcher regressions, including PS1 arithmetic corner cases. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "gool_vm.h"
#ifdef NDEBUG
#error Assertions are required
#endif
static int32_t members[512];static unsigned initialized[512];
static int read_member(void *ctx,const gool_operand *arg,int32_t *value) {
    (void)ctx;
    if(arg->kind!=GOOL_ARG_VARIABLE||arg->value<0||arg->value>=512||!initialized[arg->value])return GOOL_ARG_UNBOUND;
    *value=members[arg->value];return 0;
}
static int write_member(void *ctx,const gool_operand *arg,int32_t value) {
    (void)ctx;
    if(arg->kind!=GOOL_ARG_VARIABLE||arg->value<0||arg->value>=512)return GOOL_ARG_UNBOUND;
    members[arg->value]=value;initialized[arg->value]=1;return 0;
}
static void pack(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(value>>(8*i));
}
static uint32_t inst(uint32_t op,uint32_t left,uint32_t right) {
    return op<<24|right<<12|left;
}
int main(void) {
    uint8_t code[64];
    uint32_t program[]={inst(0x11,0xe1f,0xa07),inst(0x11,0xe1f,0xa02),
        inst(1,0xe1f,0xe1f),inst(0x11,0xe25,0xe1f),inst(0x17,0xe26,0xe25),0x31000000};
    for(unsigned i=0;i<6;i++)pack(code+4*i,program[i]);
    gool_data data={.code=code,.code_words=6,.stack_start=1};
    gool_operand_context ctx={.base=&data,.read=read_member,.write=write_member};
    gool_vm vm;
    assert(gool_vm_init(&vm,&data,0,&ctx,NULL,0)==0);
    assert(gool_vm_run(&vm,2)==GOOL_VM_BUDGET&&vm.pc==2);
    assert(gool_vm_run(&vm,20)==GOOL_VM_DONE);
    assert(members[0x25]==80&&members[0x26]==-81&&gool_stack_height(&vm.stack)==0);
    assert(gool_vm_step(&vm)==GOOL_VM_DONE);
    /* Original WEfOC state 18 begins with SET_GEO frame 0, six logic ticks. */
    pack(code,0x38c60000);pack(code+4,0x31000000);
    members[0xe]=4096;initialized[0xe]=1;
    assert(gool_vm_init(&vm,&data,0,&ctx,NULL,0)==0);
    vm.mode=4;
    assert(gool_vm_run(&vm,10)==GOOL_VM_WAIT&&vm.pc==1&&vm.next_update==6);
    assert(members[0x2a]==0x40000000&&members[0x2b]==0&&members[0xe]==4096);
    assert(gool_vm_resume_frame(&vm,5)==GOOL_VM_WAIT&&gool_vm_step(&vm)==GOOL_VM_WAIT);
    assert(gool_vm_set_tick(&vm,4,1)==GOOL_VM_INVALID);
    assert(gool_vm_resume_frame(&vm,6)==0&&gool_vm_step(&vm)==GOOL_VM_DONE);
    int32_t globals[8]={0};
    pack(code,inst(0x20,0x801,0xa09));
    assert(gool_vm_init(&vm,&data,0,&ctx,NULL,0)==0);
    assert(gool_vm_bind_globals(&vm,globals,8)==0);
    assert(gool_stack_push(&vm.stack,77)==0);
    int32_t previous;assert(gool_stack_pop(&vm.stack,&previous)==0&&previous==77);
    assert(gool_vm_step(&vm)==0&&globals[1]==144);
    assert(gool_stack_pop(&vm.stack,&previous)==0&&previous==77);
    pack(code,inst(0x1f,0x801,0xe1f));
    assert(gool_vm_init(&vm,&data,0,&ctx,NULL,0)==0);
    assert(gool_vm_bind_globals(&vm,globals,8)==0);
    assert(gool_stack_push(&vm.stack,55)==0&&gool_vm_step(&vm)==0);
    assert(gool_stack_pop(&vm.stack,&previous)==0&&previous==144);
    const uint32_t unsupported[]={0x10,0x4f,0x35,0x3a,0x49,0xff};
    for(unsigned i=0;i<sizeof(unsupported)/sizeof(unsupported[0]);i++) {
        pack(code,inst(unsupported[i],0xe1f,0xe1f));
        assert(gool_vm_init(&vm,&data,0,&ctx,NULL,0)==0);
        assert(gool_stack_push(&vm.stack,12)==0);
        assert(gool_vm_step(&vm)==GOOL_VM_UNSUPPORTED&&vm.pc==0&&vm.detail==(int)unsupported[i]);
        assert(gool_stack_height(&vm.stack)==1);
        assert(gool_vm_step(&vm)==GOOL_VM_UNSUPPORTED);
    }
    uint8_t constants[8];data.constants=constants;data.constant_words=2;
    const struct {uint32_t op;int32_t a,b,result;} cases[]={
        {0,INT32_MAX,1,INT32_MIN},{1,INT32_MIN,1,INT32_MAX},
        {3,-7,3,-2},{13,-7,3,-1},{3,INT32_MIN,-1,INT32_MIN},
        {0x15,INT32_MIN,-1,-1073741824},{0x15,1,33,2},
        {0x15,-3,-32,-3},{0x21,4095,1,2}};
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        pack(code,inst(cases[i].op,0,1));pack(constants,(uint32_t)cases[i].a);pack(constants+4,(uint32_t)cases[i].b);
        assert(gool_vm_init(&vm,&data,0,&ctx,NULL,0)==0);
        assert(gool_vm_step(&vm)==0);
        int32_t result;assert(gool_stack_pop(&vm.stack,&result)==0&&result==cases[i].result);
    }
    pack(code,inst(3,0xa01,0x800));
    assert(gool_vm_init(&vm,&data,0,&ctx,NULL,0)==0);
    /* PS1 DIV by zero returns -1 for this positive numerator. */
    assert(gool_vm_step(&vm)==GOOL_VM_RUNNING&&vm.pc==1);
    int32_t quotient;
    assert(gool_stack_pop(&vm.stack,&quotient)==0&&quotient==-1);
    /* JMP -1 loops at the same instruction and yields at the budget. */
    pack(code,0x320003ff);
    assert(gool_vm_init(&vm,&data,0,&ctx,NULL,0)==0);
    assert(gool_vm_run(&vm,1000)==GOOL_VM_BUDGET&&vm.pc==0);
    /* BNZ STACK consumes its condition, then discards only when taken. */
    pack(code,0x33000001u|(31u<<14)|(1u<<10));
    pack(code+4,0x2f000000);pack(code+8,0x31000000);
    for(int32_t condition=0;condition<=1;condition++) {
        assert(gool_vm_init(&vm,&data,0,&ctx,NULL,0)==0);
        assert(gool_stack_push(&vm.stack,99)==0&&gool_stack_push(&vm.stack,condition)==0);
        assert(gool_vm_step(&vm)==0&&vm.pc==(condition?2u:1u));
        assert(gool_stack_height(&vm.stack)==(condition?0u:1u));
    }
    assert(gool_vm_init(&vm,&data,6,&ctx,NULL,0)==GOOL_VM_INVALID);
    const uint32_t call_program[]={inst(0x11,0xe1f,0xa07),0x3b100004,
        inst(0x11,0xe25,0xe26),0x31000000,inst(0,0xb7f,0xa01),
        inst(0x11,0xe26,0xe1f),0x31000000};
    for(unsigned i=0;i<7;i++)pack(code+4*i,call_program[i]);
    data.code_words=7;
    assert(gool_vm_init(&vm,&data,0,&ctx,NULL,0)==0);
    assert(gool_vm_run(&vm,20)==GOOL_VM_DONE&&members[0x25]==128&&members[0x26]==128);
    assert(vm.stack.depth==1&&gool_stack_height(&vm.stack)==0);
    uint8_t alt_code[4];pack(alt_code,0x31000000);
    gool_data alt_data={.code=alt_code,.code_words=1,.lookup_index=1};
    ctx.alternate=&alt_data;
    pack(code,0x3b004000);pack(code+4,0x31000000);
    assert(gool_vm_init(&vm,&data,0,&ctx,NULL,0)==0);
    assert(gool_vm_step(&vm)==0&&vm.code.lookup_index==1&&vm.pc==0);
    assert(gool_vm_step(&vm)==0&&vm.code.lookup_index==0&&vm.pc==1);
    assert(gool_vm_step(&vm)==GOOL_VM_DONE);
    pack(code,inst(0x11,0xe25,0xb00));
    pack(code+4,inst(0x11,0xb03,0xa07));
    pack(code+8,inst(0x11,0xe26,0xb03));pack(code+12,0x31000000);
    data.code_words=4;data.stack_start=64;ctx.alternate=NULL;
    assert(gool_vm_init(&vm,&data,0,&ctx,NULL,0)==0);
    int32_t header;assert(gool_stack_frame_read(&vm.stack,2,&header)==0&&header==0x100);
    assert(gool_vm_run(&vm,20)==GOOL_VM_DONE&&members[0x25]==0xffff&&members[0x26]==112);
    pack(code,0x3b000002);pack(code+4,0x31000000);pack(code+8,0x31000000);
    assert(gool_vm_init(&vm,&data,0,&ctx,NULL,0)==0);
    vm.mode=0x12340009;
    assert(gool_vm_step(&vm)==0&&vm.mode==0x12340000);
    assert(gool_stack_frame_read(&vm.stack,0,&header)==0&&header==0x12340009);
    vm.mode=0xabcd0000;
    assert(gool_vm_step(&vm)==0&&vm.mode==0xabcd0009&&vm.pc==1);
    puts("PASS partial GOOL dispatcher including native three-word frame/header and call-flag checks");
    return 0;
}
