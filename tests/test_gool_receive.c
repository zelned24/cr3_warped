/* Production C regression harness; run through tests/test_gool_c.py. */
#include <assert.h>
#include <stdio.h>
#include "gool_vm.h"
#ifdef NDEBUG
#error Assertions are required
#endif
static void pack(uint8_t *p,uint32_t n) {
    for(unsigned j=0;j<4;j++)p[j]=(uint8_t)(n>>(j*8));
}
static void setup(gool_vm *vm,gool_data *data,gool_operand_context *ctx,
                  uint8_t *code,uint32_t inst,gool_signal_receiver *result) {
    for(unsigned j=0;j<8;j++)pack(code+j*4,0x2f000000);
    pack(code+16,inst);pack(code+24,0x31000000);
    *data=(gool_data){.code=code,.code_words=8,.stack_start=1};
    *ctx=(gool_operand_context){.base=data};
    assert(gool_vm_init(vm,data,0,ctx,NULL,0)==0);
    *result=(gool_signal_receiver){.new_state=-1};
    assert(gool_vm_bind_receiver(vm,result)==0);
    assert(gool_stack_push(&vm->stack,0x1800)==0);
    assert(gool_stack_push(&vm->stack,0)==0); /* sender argument pointer, opaque */
    assert(gool_vm_invoke(vm,data,4,2)==0);
    vm->mode=8;
    assert(gool_vm_set_tick(vm,20,0)==0);vm->next_update=99;
}
int main(int argc,char **argv) {
    uint8_t code[32];gool_vm vm;gool_data data;gool_operand_context ctx;
    gool_signal_receiver result;
    for(uint32_t state=0;state<1024;state++) {
        setup(&vm,&data,&ctx,code,0x3e400000|state,&result);
        assert(gool_vm_step(&vm)==GOOL_VM_SIGNAL_RETURN);
        assert(result.new_state==(int32_t)state&&result.was_b);
        assert(vm.pc==0&&vm.stack.depth==1&&gool_stack_height(&vm.stack)==0);
        assert(vm.status==0&&vm.next_update==99);
    }
    for(unsigned action=0;action<4;action+=3) {
        setup(&vm,&data,&ctx,code,0x3d000000|(action<<22),&result);
        assert(gool_vm_step(&vm)==0&&vm.pc==5&&result.new_state==-1&&vm.mode==40);
        assert(gool_vm_run(&vm,2)==GOOL_VM_SIGNAL_RETURN&&result.new_state==255&&vm.pc==0);
    }
    for(unsigned count=0;count<16;count++) {
        setup(&vm,&data,&ctx,code,0x3e400007|(count<<10),&result);
        for(unsigned j=0;j<count;j++)assert(gool_stack_push(&vm.stack,(int32_t)j-100)==0);
        assert(gool_vm_step(&vm)==GOOL_VM_SIGNAL_RETURN&&result.new_state==7);
        assert(result.argument_count==count);
        for(unsigned j=0;j<count;j++)assert(result.arguments[j]==(int32_t)j-100);
    }
    setup(&vm,&data,&ctx,code,0x3d500007|(31u<<14)|(2u<<10),&result);
    assert(gool_stack_push(&vm.stack,11)==0&&gool_stack_push(&vm.stack,22)==0&&gool_stack_push(&vm.stack,1)==0);
    assert(gool_vm_step(&vm)==GOOL_VM_SIGNAL_RETURN&&result.argument_count==2);
    assert(result.arguments[0]==11&&result.arguments[1]==22);
    setup(&vm,&data,&ctx,code,0x3d100001|(31u<<14)|(2u<<10),&result);
    assert(gool_stack_push(&vm.stack,11)==0&&gool_stack_push(&vm.stack,22)==0&&gool_stack_push(&vm.stack,0)==0);
    assert(gool_vm_step(&vm)==0&&vm.pc==6&&gool_stack_height(&vm.stack)==0&&result.new_state==-1);
    setup(&vm,&data,&ctx,code,0x31000000,&result);
    assert(gool_vm_step(&vm)==GOOL_VM_FAILED_SIGNAL&&result.new_state==-1&&vm.pc==0);
    setup(&vm,&data,&ctx,code,0x3d400000,&result);vm.mode=0;
    assert(gool_vm_step(&vm)==GOOL_VM_INVALID&&result.new_state==-1);
    setup(&vm,&data,&ctx,code,0x3d700000,&result);
    assert(gool_vm_step(&vm)==GOOL_VM_UNSUPPORTED&&result.new_state==-1);
    setup(&vm,&data,&ctx,code,0x3d400000,&result);
    assert(gool_vm_bind_receiver(&vm,NULL)==0);
    assert(gool_vm_step(&vm)==GOOL_VM_OPERAND);
    if(argc==2) {
        cga_archive archive={0};gool_data original;
        assert(cga_open(&archive,argv[1],0x0B)==0);
        assert(gool_data_open(&original,&archive,0x4ac42e4d)==0);
        ctx=(gool_operand_context){.base=&original};
        assert(gool_vm_init(&vm,&original,0,&ctx,NULL,0)==0);
        result=(gool_signal_receiver){.new_state=-1};
        assert(gool_vm_bind_receiver(&vm,&result)==0);
        assert(gool_stack_push(&vm.stack,0x1800)==0&&gool_stack_push(&vm.stack,0)==0);
        assert(gool_vm_invoke(&vm,&original,1184,2)==0);vm.mode=8;
        assert(gool_vm_step(&vm)==GOOL_VM_SIGNAL_RETURN&&result.new_state==34&&result.was_b&&vm.pc==0);
        cga_close(&archive);
    }
    puts("PASS native receive decisions; SEND transport/application remain pending");return 0;
}
