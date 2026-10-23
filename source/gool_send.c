#include "gool_send.h"

static int fail(gool_vm *vm,int status,int detail) {vm->detail=detail;return status;}
static int32_t signed_word(uint32_t n) {
    return n<=INT32_MAX?(int32_t)n:(int32_t)((int64_t)n-INT64_C(4294967296));
}
static int read_value(gool_vm *vm,uint32_t raw,int32_t *value) {
    gool_operand arg;
    int error=gool_operand_decode(raw,&arg);
    if(error)return error;
    if(arg.kind==GOOL_ARG_FRAME)return gool_stack_frame_read(&vm->stack,arg.value,value);
    if(arg.kind==GOOL_ARG_VARIABLE&&arg.value==31)return gool_stack_pop(&vm->stack,value);
    if(arg.kind==GOOL_ARG_VARIABLE&&arg.value==32)return gool_vm_address(&vm->code,vm->pc+1,value);
    return gool_operand_read(&vm->external,raw,value);
}
int gool_send_begin(gool_vm *vm,uint32_t inst) {
    if(!vm||!vm->send_enabled||vm->send_pending||(inst>>24)!=0x3c)return GOOL_VM_INVALID;
    gool_send_request request={.argument_count=(inst>>18)&7,.caller_mode=vm->mode};
    int error=read_value(vm,0xe00u|((inst>>21)&7),&request.target);
    if(error)return fail(vm,GOOL_VM_OPERAND,error);
    uint32_t raw=inst&0xfff;
    gool_operand arg;
    if(gool_operand_decode(raw,&arg))return GOOL_VM_INVALID;
    int address=1,cached=0;int32_t value=0;
    if(arg.kind==GOOL_ARG_BOOLEAN) {
        if(raw!=0xbe0)return fail(vm,GOOL_VM_UNSUPPORTED,GOOL_ARG_INVALID);
        address=0;
    } else if(arg.kind==GOOL_ARG_VARIABLE&&arg.value==31) {
        error=gool_stack_pop(&vm->stack,&value);cached=1;
        if(error)return fail(vm,GOOL_VM_STACK,error);
    } else if(arg.kind==GOOL_ARG_FRAME) {
        error=gool_stack_frame_read(&vm->stack,arg.value,&value);
        if(error)return fail(vm,GOOL_VM_STACK,error);
    } else if(arg.kind==GOOL_ARG_INDIRECT) {
        int32_t link;
        error=read_value(vm,0xe00u|arg.base,&link);
        if(error)return fail(vm,GOOL_VM_OPERAND,error);
        address=link!=0;
        if(address&&arg.value==31)return fail(vm,GOOL_VM_UNSUPPORTED,GOOL_ARG_UNBOUND);
    }
    int32_t collision,condition;
    error=read_value(vm,0xe1a,&collision);
    if(error)return fail(vm,GOOL_VM_OPERAND,error);
    error=gool_operand_write(&vm->external,0xe1a,signed_word((uint32_t)collision&~0x20000u));
    if(error)return fail(vm,GOOL_VM_OPERAND,error);
    error=read_value(vm,0xe00u|((inst>>12)&0x3f),&condition);
    if(error)return fail(vm,GOOL_VM_OPERAND,error);
    if(request.argument_count>gool_stack_height(&vm->stack))return fail(vm,GOOL_VM_STACK,GOOL_STACK_UNDERFLOW);
    if(!condition||!request.target||!address) {
        error=gool_operand_write(&vm->external,0xe25,0);
        if(error)return fail(vm,GOOL_VM_OPERAND,error);
        vm->stack.top-=request.argument_count;vm->pc++;return GOOL_VM_RUNNING;
    }
    for(uint32_t n=0;n<request.argument_count;n++)request.arguments[n]=vm->stack.words[vm->stack.top-request.argument_count+n];
    if(!cached) {
        error=read_value(vm,raw,&value);
        if(error)return fail(vm,GOOL_VM_OPERAND,error);
    }
    request.raw_signal=value;vm->send=request;vm->send_pending=1;vm->pc++;
    return GOOL_VM_SEND;
}
int gool_send_finish(gool_vm *vm) {
    if(!vm||!vm->send_pending||vm->status)return GOOL_VM_INVALID;
    int32_t collision;
    int error=read_value(vm,0xe1a,&collision);
    if(error)return fail(vm,GOOL_VM_OPERAND,error);
    if(!((uint32_t)collision&0x20000u)) {
        if(vm->send.argument_count>gool_stack_height(&vm->stack))return fail(vm,GOOL_VM_STACK,GOOL_STACK_UNDERFLOW);
        uint32_t old_top=vm->stack.top;
        vm->stack.top-=vm->send.argument_count;vm->send_pending=0;
        if(vm->frame_yield&&vm->frame_token_index>=vm->stack.top&&vm->frame_token_index<old_top)vm->frame_yield=0;
        return GOOL_VM_RUNNING;
    }
    if(!(vm->send.caller_mode&2)) {
        if(!gool_stack_height(&vm->stack))return fail(vm,GOOL_VM_STACK,GOOL_STACK_UNDERFLOW);
        uint32_t index=vm->stack.top-1;
        int32_t discarded;
        if(gool_stack_pop(&vm->stack,&discarded))return GOOL_VM_STACK;
        if(vm->frame_yield&&vm->frame_token_index==index)vm->frame_yield=0;
    }
    vm->send_pending=0;return GOOL_VM_SEND_REPLACED;
}
