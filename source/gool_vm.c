#include "gool_vm.h"
#include "gool_send.h"
#include "gool_math.h"
#include "gool_trig.h"
#include "gool_atan.h"
#include <string.h>

static int32_t signed_word(uint32_t value) {
    return value<=INT32_MAX?(int32_t)value:(int32_t)((int64_t)value-INT64_C(4294967296));
}
static int fault(gool_vm *vm,int status,int detail) {
    vm->status=status;vm->detail=detail;vm->pc=vm->fault_pc;return status;
}
int gool_vm_address(const gool_data *code,uint32_t pc,int32_t *out) {
    if(!code||!out||code->lookup_index>=1024||pc>=0x4000)return -1;
    if(code->native_code_address) {
        uint64_t address=(uint64_t)code->native_code_address+pc*4u;
        if(address>UINT32_MAX)return -1;
        *out=signed_word((uint32_t)address);return 0;
    }
    *out=(int32_t)(0x50000000u|(code->lookup_index<<18)|(pc<<2));
    return 0;
}
int gool_vm_resolve(gool_vm *vm,int32_t pointer,gool_data *owner,uint32_t *pc) {
    if(!vm||!owner||!pc)return -1;
    uint32_t raw=(uint32_t)pointer;
    if((raw>>28)!=5&&vm->code.source&&vm->code.source->resolve) {
        gool_data next={0};uint32_t offset=0;
        if(vm->code.source->resolve(vm->code.source->context,raw,&next,&offset)||
           next.source!=vm->code.source||offset>=0x4000||offset>=next.code_words)return -1;
        *owner=next;*pc=offset;return 0;
    }
    if((raw>>28)!=5||(raw&3))return -1;
    uint32_t lookup=(raw&0x0fffffffu)>>18,offset=(raw>>2)&0xffff;
    const gool_data *base=vm->external.base,*alternate=vm->external.alternate;
    if(base&&base->lookup_index==lookup)*owner=*base;
    else if(alternate&&alternate->lookup_index==lookup)*owner=*alternate;
    else if(gool_data_lookup_relative(owner,&vm->code,lookup))return -1;
    if(offset>=0x4000||offset>=owner->code_words)return -1;
    *pc=offset;return 0;
}
static int read_dynamic(void *context,const gool_operand *arg,int32_t *value) {
    gool_vm *vm=context;
    if(arg->kind==GOOL_ARG_FRAME)return gool_stack_frame_read(&vm->stack,arg->value,value);
    if(arg->kind==GOOL_ARG_VARIABLE&&arg->value==0x1f)return gool_stack_pop(&vm->stack,value);
    if(arg->kind==GOOL_ARG_VARIABLE&&arg->value==0x20)
        return gool_vm_address(&vm->code,vm->pc+1,value)?GOOL_ARG_RANGE:0;
    if(!vm->external.read)return GOOL_ARG_UNBOUND;
    return vm->external.read(vm->external.context,arg,value);
}
static int write_dynamic(void *context,const gool_operand *arg,int32_t value) {
    gool_vm *vm=context;
    if(arg->kind==GOOL_ARG_FRAME)return gool_stack_frame_write(&vm->stack,arg->value,value);
    if(arg->kind==GOOL_ARG_VARIABLE&&arg->value==0x1f)return gool_stack_push(&vm->stack,value);
    if(!vm->external.write)return GOOL_ARG_UNBOUND;
    return vm->external.write(vm->external.context,arg,value);
}
int gool_vm_init(gool_vm *vm,const gool_data *code,uint32_t pc,
                 const gool_operand_context *ctx,const int32_t *args,uint32_t argc) {
    if(!vm||!code||!code->code||pc>=code->code_words||!ctx||
       argc>GOOL_STACK_WORDS-GOOL_FRAME_WORDS||(argc&&!args))return GOOL_VM_INVALID;
    gool_vm next={0};next.code=*code;next.pc=pc;next.external=*ctx;next.respect_delay=1;
    gool_stack_reset(&next.stack);
    const gool_data *base=ctx->base?ctx->base:code;
    if(gool_stack_set_origin(&next.stack,base->stack_start))return GOOL_VM_INVALID;
    for(uint32_t i=0;i<argc;i++)gool_stack_push(&next.stack,args[i]);
    if(gool_stack_enter(&next.stack,argc,0))return GOOL_VM_STACK;
    *vm=next;return GOOL_VM_RUNNING;
}
static int supported_binary(uint32_t op) {
    return op<=0x10 || op==0x15 || op==0x1d || op==0x1f || op==0x20 || op==0x21;
}
static int supported_store(uint32_t op) {
    return op==0x11||op==0x12||op==0x17||op==0x19||op==0x27||op==0x2b||op==0x2c;
}
static int return_from_call(gool_vm *vm,int restore_flags) {
    if(vm->stack.depth==1){vm->pc++;vm->status=GOOL_VM_DONE;return vm->status;}
    if(vm->stack.depth<2||vm->stack.depth>GOOL_STACK_FRAMES)return fault(vm,GOOL_VM_STACK,0x31);
    uint32_t frame=vm->stack.frames[vm->stack.depth-1];
    if(frame>gool_stack_capacity(&vm->stack)-GOOL_FRAME_WORDS)return fault(vm,GOOL_VM_STACK,0x31);
    gool_data owner;uint32_t offset;
    int resolved=gool_vm_resolve(vm,vm->stack.words[frame+1],&owner,&offset)==0;
    /* Native399FC restores S5 without fetching it. Engine handler/resume
     * returns can therefore restore NULL or a non-code word before exiting.
     * Ordinary bytecode calls still require executable target code. */
    if(!resolved&&!(vm->mode&9u))return fault(vm,GOOL_VM_INVALID,0x31);
    int32_t pointer;
    uint32_t flags;
    int error=gool_stack_return_flags(&vm->stack,&pointer,&flags);
    if(error)return fault(vm,GOOL_VM_STACK,error);
    if(restore_flags&&flags!=0xffff)vm->mode=(vm->mode&0xffff0000u)|flags;
    if(resolved){vm->code=owner;vm->pc=offset;vm->has_pending_pc=0;}
    else {vm->pending_pc=(uint32_t)pointer;vm->has_pending_pc=1;}
    return GOOL_VM_RUNNING;
}
int gool_vm_step(gool_vm *vm) {
    if(!vm)return GOOL_VM_INVALID;
    if(vm->status)return vm->status;
    if(vm->send_pending)return GOOL_VM_SEND;
    if(vm->geometry_pending)return GOOL_VM_GEOMETRY;
    if(vm->respect_delay&&vm->frame_yield)return GOOL_VM_WAIT;
    vm->fault_pc=vm->pc;
    if(vm->has_pending_pc)return fault(vm,GOOL_VM_INVALID,0x31);
    if(gool_data_instruction(&vm->code,vm->pc,&vm->instruction))return fault(vm,GOOL_VM_INVALID,0);
    uint32_t inst=vm->instruction,op=inst>>24;
    if((op==0x3fu||op==0x46u)&&vm->spawn_enabled) {vm->pc++;return GOOL_VM_SPAWN;}
    if(op==0x40u&&vm->resource_enabled){vm->pc++;return GOOL_VM_RESOURCE;}
    if(op==0x41||op==0x42) {
        if(!vm->sound)return fault(vm,GOOL_VM_UNSUPPORTED,(int)op);
        int status=vm->sound(vm->sound_context,inst);
        if(status)return fault(vm,status<0?status:GOOL_VM_INVALID,(int)op);
        return GOOL_VM_RUNNING;
    }
    if(op==0x1c) {
        if(!vm->geometry)return fault(vm,GOOL_VM_UNSUPPORTED,(int)op);
        int status=vm->geometry(vm->geometry_context,inst);
        if(status==GOOL_VM_GEOMETRY) {
            vm->geometry_pending=1;vm->pc++;return GOOL_VM_GEOMETRY;
        }
        if(status)return fault(vm,status<0?status:GOOL_VM_INVALID,(int)op);
        vm->pc++;return GOOL_VM_RUNNING;
    }
    if(!supported_binary(op)&&!supported_store(op)&&(op<0x2f||op>0x39)&&op!=0x3b&&op!=0x3c&&op!=0x3d&&op!=0x3e&&op!=0x47&&op!=0x2a&&op!=0x4e&&op!=0x18&&op!=0x22&&op!=0x25&&op!=0x2d&&op!=0x23&&op!=0x24&&op!=0x13&&op!=0x14&&op!=0x16&&op!=0x26&&op!=0x1b&&op!=0x1e)
        return fault(vm,GOOL_VM_UNSUPPORTED,(int)op);
    if(op==0x10&&!vm->random_state)return fault(vm,GOOL_VM_UNSUPPORTED,(int)op);
    if(op==0x3c) {
        if(!vm->send_enabled)return fault(vm,GOOL_VM_UNSUPPORTED,(int)op);
        int result=gool_send_begin(vm,inst);
        return result<0?fault(vm,result,vm->detail):result;
    }
    if(op>=0x35&&op<=0x37&&!vm->change_state)return fault(vm,GOOL_VM_UNSUPPORTED,(int)op);
    gool_operand_context ctx=vm->external;
    ctx.context=vm;ctx.read=read_dynamic;ctx.write=write_dynamic;
    int32_t right,left,result=0;int error;
    if(op==0x14||op==0x16||op==0x26||op==0x13) {
        uint32_t address;
        if(op==0x13) {
            int32_t rate=256;
            if(((inst>>12)&0xfffu)==0xbf0u) {
                error=gool_stack_pop(&vm->stack,&rate);if(error)return fault(vm,GOOL_VM_STACK,error);
                error=gool_stack_pop(&vm->stack,&right);if(error)return fault(vm,GOOL_VM_STACK,error);
            } else {
                error=gool_operand_read(&ctx,(inst>>12)&0xfff,&right);
                if(error)return fault(vm,GOOL_VM_OPERAND,error);
            }
            error=gool_operand_address(&vm->external,inst&0xfff,0x18,&address);
            if(error)return fault(vm,GOOL_VM_OPERAND,error);
            error=vm->read_address?vm->read_address(vm->address_context,address,&left):
                gool_memory_read(vm->memory,address,&left);
            if(error)return fault(vm,GOOL_VM_OPERAND,error);
            int32_t next;result=gool_math_oscillate(left,right,rate,&next);
            error=vm->write_address?vm->write_address(vm->address_context,address,next):
                gool_memory_write(vm->memory,address,next);
            if(error)return fault(vm,GOOL_VM_OPERAND,error);
            error=gool_stack_push(&vm->stack,result);if(error)return fault(vm,GOOL_VM_STACK,error);
        } else {
            error=gool_operand_address(&vm->external,(inst>>12)&0xfff,0x1c,&address);
            if(error)return fault(vm,GOOL_VM_OPERAND,error);
            if(op==0x14) {
                uint32_t destination;
                error=gool_operand_simple(inst&0xfff,&destination);
                if(error)return fault(vm,GOOL_VM_OPERAND,error);
                error=gool_operand_write(&ctx,destination,signed_word(address));
                if(error)return fault(vm,GOOL_VM_OPERAND,error);
            } else if(op==0x16) {
                error=gool_operand_read(&ctx,inst&0xfff,&left);
                if(error)return fault(vm,GOOL_VM_OPERAND,error);
                if(address) {
                    error=vm->read_address?vm->read_address(vm->address_context,address,&right):
                        gool_memory_read(vm->memory,address,&right);
                    if(error)return fault(vm,GOOL_VM_OPERAND,error);
                }
                error=gool_stack_push(&vm->stack,left);if(error)return fault(vm,GOOL_VM_STACK,error);
                if(address){error=gool_stack_push(&vm->stack,right);if(error)return fault(vm,GOOL_VM_STACK,error);}
            } else {
                uint32_t lower;
                error=gool_operand_address(&vm->external,inst&0xfff,0x18,&lower);
                if(error)return fault(vm,GOOL_VM_OPERAND,error);
                error=gool_stack_push(&vm->stack,signed_word(lower));if(error)return fault(vm,GOOL_VM_STACK,error);
                if(address){error=gool_stack_push(&vm->stack,signed_word(address));if(error)return fault(vm,GOOL_VM_STACK,error);}
            }
        }
    } else if(op==0x1b||op==0x1e) {
        const int32_t *source=op==0x1b?vm->frame_factor:vm->phase_source;
        if(!source)return fault(vm,GOOL_VM_OPERAND,GOOL_ARG_UNBOUND);
        int32_t factor=*source;
        error=gool_operand_read(&ctx,(inst>>12)&0xfff,&right);
        if(error)return fault(vm,GOOL_VM_OPERAND,error);
        error=gool_operand_read(&ctx,inst&0xfff,&left);
        if(error)return fault(vm,GOOL_VM_OPERAND,error);
        if(op==0x1b)result=gool_math_integrate(left,right,factor);
        else {
            int32_t numerator=signed_word((uint32_t)right+(uint32_t)factor);
            result=left?(int32_t)((int64_t)numerator%left):numerator;
        }
        error=gool_stack_push(&vm->stack,result);if(error)return fault(vm,GOOL_VM_STACK,error);
    } else if(op==0x23||op==0x24) {
        /* Native SH resolves the target link after evaluating its low source;
         * the source may itself change that link via a provider/STACK read. */
        if(op==0x24) {
            error=gool_operand_read(&ctx,inst&0xfff,&result);
            if(error)return fault(vm,GOOL_VM_OPERAND,error);
        }
        gool_operand half={.kind=GOOL_ARG_HALFWORD,.value=(int32_t)((inst>>15)&63),.base=(inst>>12)&7};
        if(op==0x23) {
            if(!vm->external.read)return fault(vm,GOOL_VM_OPERAND,GOOL_ARG_UNBOUND);
            error=vm->external.read(vm->external.context,&half,&result);
            if(error)return fault(vm,GOOL_VM_OPERAND,error);
            error=gool_stack_push(&vm->stack,result);
            if(error)return fault(vm,GOOL_VM_STACK,error);
        } else {
            if(!vm->external.write)return fault(vm,GOOL_VM_OPERAND,GOOL_ARG_UNBOUND);
            error=vm->external.write(vm->external.context,&half,result);
            if(error)return fault(vm,GOOL_VM_OPERAND,error);
        }
    } else if(op==0x3d||op==0x3e) {
        if(!(vm->mode&8))return fault(vm,GOOL_VM_INVALID,-14);
        if(!vm->receiver)return fault(vm,GOOL_VM_OPERAND,GOOL_ARG_UNBOUND);
        gool_signal_receiver *receiver=vm->receiver;
        uint32_t source_op=(inst>>20)&3,action=(inst>>22)&3,argc=(inst>>10)&15;
        /* Native sourceOp 3 depends on an unassigned CPU temporary. Do not
         * replace it with the viewer's invented nonzero condition. */
        if(source_op==3)return fault(vm,GOOL_VM_UNSUPPORTED,(int)op);
        int matches=1;
        if(source_op) {
            error=gool_operand_read(&ctx,0xe00u|((inst>>14)&0x3f),&left);
            if(error)return fault(vm,GOOL_VM_OPERAND,error);
            matches=source_op==2?left==0:left!=0;
        }
        if(!matches) {
            if(!action) {
                int32_t offset=(int32_t)(inst&0x3ff);
                if(offset&0x200)offset-=1024;
                int64_t pc=(int64_t)vm->pc+1+offset;
                if(pc<0||pc>=vm->code.code_words)return fault(vm,GOOL_VM_INVALID,(int)op);
                if(argc>gool_stack_height(&vm->stack))return fault(vm,GOOL_VM_STACK,GOOL_STACK_UNDERFLOW);
                vm->stack.top-=argc;
                vm->pc=(uint32_t)pc;return GOOL_VM_RUNNING;
            }
        } else {
            if(action==1&&argc>gool_stack_height(&vm->stack))return fault(vm,GOOL_VM_STACK,GOOL_STACK_UNDERFLOW);
            vm->mode|=0x20;
            receiver->was_b=op==0x3e;receiver->has_ack=1;
            if(action==1) {
                receiver->argument_count=argc;
                for(uint32_t n=0;n<argc;n++)receiver->arguments[n]=vm->stack.words[vm->stack.top-argc+n];
                vm->stack.top-=argc;
            }
            if(action==1||action==2) {
                int returned=return_from_call(vm,0);
                if(returned<0)return returned;
                if(returned==GOOL_VM_DONE)return returned;
                receiver->new_state=action==1?(int32_t)(inst&0x3ff):255;
                return GOOL_VM_SIGNAL_RETURN;
            }
        }
    } else if(op>=0x35&&op<=0x37) {
        int meets=1;
        if(op!=0x35) {
            error=gool_operand_read(&ctx,0xe00u|((inst>>14)&0x3f),&left);
            if(error)return fault(vm,GOOL_VM_OPERAND,error);
            meets=(left!=0)==(op==0x36);
        }
        int changed=vm->change_state(vm->state_context,inst&0x3ff,(inst>>10)&0xf,meets);
        if(changed==GOOL_VM_NEW_STATE)return changed;
        if(changed==GOOL_VM_STATE_REQUEST){vm->pc++;return changed;}
        if(changed!=GOOL_VM_RUNNING)return fault(vm,changed<0?changed:GOOL_VM_INVALID,(int)op);
    } else if(op==0x2d) {
        error=gool_operand_read(&ctx,(inst>>12)&0xfff,&right);
        if(error)return fault(vm,GOOL_VM_OPERAND,error);
        if(vm->external.address) {
            uint32_t address;
            error=gool_operand_address(&vm->external,inst&0xfff,0x18,&address);
            if(error)return fault(vm,GOOL_VM_OPERAND,error);
            error=vm->read_address?vm->read_address(vm->address_context,address,&left):
                gool_memory_read(vm->memory,address,&left);
            if(error)return fault(vm,GOOL_VM_OPERAND,error);
        } else {
            gool_operand arg;
            if(gool_operand_decode(inst&0xfff,&arg))return fault(vm,GOOL_VM_INVALID,0x2d);
            /* Native lower operand resolves an address before LW. Boolean/null
             * addresses are not literal zero/one values for this instruction. */
            if(arg.kind==GOOL_ARG_BOOLEAN)return fault(vm,GOOL_VM_OPERAND,GOOL_ARG_UNBOUND);
            if(arg.kind==GOOL_ARG_INDIRECT) {
                if(arg.value==31)return fault(vm,GOOL_VM_UNSUPPORTED,0x2d);
                int32_t link;
                error=gool_operand_read(&ctx,0xe00u|arg.base,&link);
                if(error||!link)return fault(vm,GOOL_VM_OPERAND,error?error:GOOL_ARG_UNBOUND);
            }
            error=gool_operand_read(&ctx,inst&0xfff,&left);
            if(error)return fault(vm,GOOL_VM_OPERAND,error);
        }
        if(gool_atan2(left,right,&result))return fault(vm,GOOL_VM_ARITHMETIC,0x2d);
        error=gool_stack_push(&vm->stack,result);
        if(error)return fault(vm,GOOL_VM_STACK,error);
    } else if(op==0x22||op==0x25) {
        uint32_t high=(inst>>12)&0xfff;
        int32_t rate=256;
        if(high==0xbf0) {
            /* Native custom form pops rate, then target, then reads current. */
            error=gool_stack_pop(&vm->stack,&rate);
            if(error)return fault(vm,GOOL_VM_STACK,error);
            error=gool_stack_pop(&vm->stack,&right);
            if(error)return fault(vm,GOOL_VM_STACK,error);
        } else {
            error=gool_operand_read(&ctx,high,&right);
            if(error)return fault(vm,GOOL_VM_OPERAND,error);
        }
        error=gool_operand_read(&ctx,inst&0xfff,&left);
        if(error)return fault(vm,GOOL_VM_OPERAND,error);
        if(op==0x22)result=gool_math_approach(left,right,rate);
        else {
            if(!vm->frame_factor)return fault(vm,GOOL_VM_OPERAND,GOOL_ARG_UNBOUND);
            result=gool_math_approach_angle(left,right,rate,*vm->frame_factor);
        }
        error=gool_stack_push(&vm->stack,result);
        if(error)return fault(vm,GOOL_VM_STACK,error);
    } else if(supported_binary(op)) {
        /* Upper/right is evaluated first; STACK operands pop in this order. */
        error=gool_operand_read(&ctx,(inst>>12)&0xfff,&right);
        if(error)return fault(vm,GOOL_VM_OPERAND,error);
        error=gool_operand_read(&ctx,inst&0xfff,&left);
        if(error)return fault(vm,GOOL_VM_OPERAND,error);
        uint32_t a=(uint32_t)left,b=(uint32_t)right;
        if(op==0x1f||op==0x20) {
            uint32_t index=a>>8;
            if(!vm->globals)return fault(vm,GOOL_VM_OPERAND,GOOL_ARG_UNBOUND);
            if(index>=vm->global_count)return fault(vm,GOOL_VM_OPERAND,GOOL_ARG_RANGE);
            if(op==0x1f) {
                error=gool_stack_push(&vm->stack,vm->globals[index]);
            } else {
                /* binary SET_GLOBAL's reference path ends in push(undefined),
                 * which reserves the old slot rather than pushing right/zero.
                 * Reserve first so overflow cannot leave a changed global. */
                error=gool_stack_reserve(&vm->stack);
                if(!error)vm->globals[index]=right;
            }
            if(error)return fault(vm,GOOL_VM_STACK,error);
            vm->pc++;return GOOL_VM_RUNNING;
        }
        switch(op) {
            case 0:result=signed_word(a+b);break;
            case 1:result=signed_word(a-b);break;
            case 2:result=signed_word(a*b);break; /* native MULT/MFLO, no fixed-point shift */
            case 3:
                result=!right?(left<0?1:-1):signed_word((uint32_t)((int64_t)left/right));break;
            case 4:result=left==right;break;
            case 5:result=right?left:0;break; /* high operand gates the low value */
            case 6:case 8:result=signed_word(a|b);break;
            case 7:result=signed_word(a&b);break;
            case 9:result=left<right;break;
            case 10:result=left<=right;break;
            case 11:result=left>right;break;
            case 12:result=left>=right;break;
            case 13:
                result=!right?left:(int32_t)((int64_t)left%right);break;
            case 14:result=signed_word(a^b);break;
            case 15:result=(a&b)==b;break;
            case 0x10: {
                /* Native 0x80039168: high operand first, then low; state
                 * arithmetic and range subtraction wrap at 32 bits. DIVU
                 * with zero range leaves the numerator in HI on PS1. */
                uint32_t old_a=vm->random_state[0],old_b=vm->random_state[1];
                uint32_t next_a=(old_a>>8)|(old_b<<24);
                uint32_t sum=old_a+(old_b>>8)+(next_a>>8);
                uint32_t next_b=(old_b>>8)|(sum<<24),range=b-a;
                vm->random_state[0]=next_a;vm->random_state[1]=next_b;
                result=signed_word(a+(range?next_b%range:next_b));break;
            }
            case 0x15: {
                if(right>=0)result=signed_word(a<<(b&31));
                else {
                    uint32_t shift=(0u-b)&31u;
                    uint32_t shifted=a>>shift;
                    if(left<0&&shift)shifted|=UINT32_MAX<<(32-shift);
                    result=signed_word(shifted);
                }
                break;
            }
            case 0x1d: {
                /* Original393A0: signed DIV after SLL11, sine lookup, then
                 * threshold-dependent MULT/MFLO and arithmetic scale. */
                int32_t numerator=signed_word(a<<11);
                int32_t quotient=!right?(numerator<0?1:-1):
                    signed_word((uint32_t)((int64_t)numerator/right));
                uint32_t shift=right>65535?2u:0u;
                int32_t sine=gool_trig_sin(signed_word((uint32_t)quotient-0x400u));
                uint32_t wave=(uint32_t)(sine+0x1000)>>shift;
                uint32_t product=wave*b,scale=13u-shift,bits=product>>scale;
                if(product&0x80000000u)bits|=UINT32_MAX<<(32u-scale);
                result=signed_word(bits);break;
            }
            case 0x21: {
                uint32_t angle=(b-a)&0xfff;
                result=angle&0x800?(int32_t)angle-4096:(int32_t)angle;break;
            }
        }
        error=gool_stack_push(&vm->stack,result);
        if(error)return fault(vm,GOOL_VM_STACK,error);
    } else if(op==0x2a||op==0x4e) {
        if(!vm->memory&&(!vm->read_address||!vm->write_address))
            return fault(vm,GOOL_VM_OPERAND,GOOL_MEMORY_UNBOUND);
        /* Native 0x80039724/54: index (high operand), base (low operand),
         * logical >>8 and <<2, wrapping uint32 address; STORE then pops data. */
        error=gool_operand_read(&ctx,(inst>>12)&0xfff,&right);
        if(error)return fault(vm,GOOL_VM_OPERAND,error);
        error=gool_operand_read(&ctx,inst&0xfff,&left);
        if(error)return fault(vm,GOOL_VM_OPERAND,error);
        uint32_t address=(uint32_t)left+(((uint32_t)right>>8)<<2);
        if(op==0x2a) {
            error=vm->read_address?vm->read_address(vm->address_context,address,&result):
                gool_memory_read(vm->memory,address,&result);
            if(error)return fault(vm,GOOL_VM_OPERAND,error);
            error=gool_stack_push(&vm->stack,result);
            if(error)return fault(vm,GOOL_VM_STACK,error);
        } else {
            error=gool_stack_pop(&vm->stack,&result);
            if(error)return fault(vm,GOOL_VM_STACK,error);
            error=vm->write_address?vm->write_address(vm->address_context,address,result):
                gool_memory_write(vm->memory,address,result);
            if(error)return fault(vm,GOOL_VM_OPERAND,error);
        }
    } else if(op==0x18) {
        uint32_t destination=(inst>>18)&0x3f,pc=inst&0x3fff;
        uint32_t slot=vm->stack.top;
        if(destination==31) {
            error=gool_stack_reserve(&vm->stack);
            if(error)return fault(vm,GOOL_VM_STACK,error);
        }
        result=0;
        if(pc!=0x3fff) {
            const gool_data *owner=(inst&0x4000)?vm->external.alternate:vm->external.base;
            /* Formation is address arithmetic; do not fetch/check target PC
             * here. Invocation validates code extent when actually used. */
            if(!owner||!owner->code||gool_vm_address(owner,pc,&result))return fault(vm,GOOL_VM_INVALID,0x18);
        }
        if(destination==31)vm->stack.words[slot]=result;
        else {
            error=gool_operand_write(&ctx,0xe00u|destination,result);
            if(error)return fault(vm,GOOL_VM_OPERAND,error);
        }
    } else if(supported_store(op)) {
        error=gool_operand_read(&ctx,(inst>>12)&0xfff,&right);
        if(error)return fault(vm,GOOL_VM_OPERAND,error);
        switch(op) {
            case 0x11:result=right;break;
            case 0x12:result=right==0;break;
            case 0x17:result=signed_word(~(uint32_t)right);break;
            case 0x19:result=right<0?signed_word(0u-(uint32_t)right):right;break;
            case 0x27:result=signed_word(0x40000000u|(((uint32_t)right>>6)<<2));break;
            case 0x2b:result=gool_trig_sin(right);break;
            case 0x2c:result=gool_trig_cos(right);break;
        }
        uint32_t dest;
        if(gool_operand_simple(inst&0xfff,&dest))return fault(vm,GOOL_VM_INVALID,0);
        error=gool_operand_write(&ctx,dest,result);
        if(error)return fault(vm,GOOL_VM_OPERAND,error);
    } else if(op==0x38||op==0x39) {
        int32_t frame,scale;
        uint32_t duration=(inst>>16)&0x3f,mirror=(inst>>22)&3;
        if(op==0x38)frame=(int32_t)(inst&0x7f)*256;
        else {
            error=gool_operand_read(&ctx,inst&0xfff,&frame);
            if(error)return fault(vm,GOOL_VM_OPERAND,error);
        }
        if(op==0x38) {
            uint32_t model=(inst>>5)&0x7fc;
            error=gool_operand_write(&ctx,0xe2a,signed_word(0x40000000u|(model<<2)));
            if(error)return fault(vm,GOOL_VM_OPERAND,error);
        }
        error=gool_operand_write(&ctx,0xe2b,frame);
        if(error)return fault(vm,GOOL_VM_OPERAND,error);
        uint32_t token_index=vm->stack.top;
        error=gool_stack_push(&vm->stack,signed_word((uint32_t)vm->tick|(duration<<24)));
        if(error)return fault(vm,GOOL_VM_STACK,error);
        if(mirror!=3) {
            error=gool_operand_read(&ctx,0xe0e,&scale);
            if(error)return fault(vm,GOOL_VM_OPERAND,error);
            uint32_t absolute=scale<0?0u-(uint32_t)scale:(uint32_t)scale;
            if(mirror==0)scale=signed_word(absolute);
            else if(mirror==1)scale=signed_word(0u-absolute);
            else scale=signed_word(0u-(uint32_t)scale);
            error=gool_operand_write(&ctx,0xe0e,scale);
            if(error)return fault(vm,GOOL_VM_OPERAND,error);
        }
        vm->next_update=duration>UINT64_MAX-vm->tick?UINT64_MAX:vm->tick+duration; /* diagnostic only */
        if(vm->mode&4) {
            vm->frame_yield=1;vm->frame_token_index=token_index;vm->pc++;
            return GOOL_VM_WAIT;
        }
    } else if(op>=0x32&&op<=0x34) {
        int taken=op==0x32;
        if(!taken) {
            /* Branch condition is a direct six-bit member, not a generic
             * 12-bit operand. STACK here must pop exactly once. */
            uint32_t condition=0xe00u|((inst>>14)&0x3f);
            error=gool_operand_read(&ctx,condition,&right);
            if(error)return fault(vm,GOOL_VM_OPERAND,error);
            taken=(right!=0)==(op==0x33);
        }
        if(taken) {
            int32_t offset=(int32_t)(inst&0x3ff);
            if(offset&0x200)offset-=1024;
            int64_t target=(int64_t)vm->pc+1+offset;
            uint32_t discard=(inst>>10)&0xf;
            if(target<0||target>=vm->code.code_words)return fault(vm,GOOL_VM_INVALID,0x32);
            if(discard>gool_stack_height(&vm->stack))return fault(vm,GOOL_VM_STACK,GOOL_STACK_UNDERFLOW);
            for(uint32_t i=0;i<discard;i++) {
                error=gool_stack_pop(&vm->stack,&left);
                if(error)return fault(vm,GOOL_VM_STACK,error);
            }
            vm->pc=(uint32_t)target;return GOOL_VM_RUNNING;
        }
    } else if(op==0x47) {
        /* Native JALR 0x80039978. The high operand is read before resolving
         * the target's address. Fast address resolver boolean aliases differ
         * from generic SEND resolution. Native providers resolve remote SP
         * through the actual object address; the legacy provider stays guarded. */
        error=gool_operand_read(&ctx,(inst>>12)&0xfff,&right);
        if(error)return fault(vm,GOOL_VM_OPERAND,error);
        if(vm->external.address) {
            uint32_t address;
            error=gool_operand_address(&vm->external,inst&0xfff,0x18,&address);
            if(error)return fault(vm,GOOL_VM_OPERAND,error);
            left=0;
            if(address) {
                error=vm->read_address?vm->read_address(vm->address_context,address,&left):
                    gool_memory_read(vm->memory,address,&left);
                if(error)return fault(vm,GOOL_VM_OPERAND,error);
            }
        } else {
            gool_operand arg;
            if(gool_operand_decode(inst&0xfff,&arg))return fault(vm,GOOL_VM_INVALID,0x47);
            int address=1;
            if(arg.kind==GOOL_ARG_BOOLEAN) {
                if(arg.value)return fault(vm,GOOL_VM_UNSUPPORTED,0x47); /* native address 1 */
                address=0;
            } else if(arg.kind==GOOL_ARG_INDIRECT) {
                int32_t link;
                error=gool_operand_read(&ctx,0xe00u|arg.base,&link);
                if(error)return fault(vm,GOOL_VM_OPERAND,error);
                address=link!=0;
                if(address&&arg.value==31)return fault(vm,GOOL_VM_UNSUPPORTED,0x47);
            }
            left=0;
            if(address) {
                error=gool_operand_read(&ctx,inst&0xfff,&left);
                if(error)return fault(vm,GOOL_VM_OPERAND,error);
            }
        }
        if(!left) {
            /* Null target uses RAW count, not count>>8. sll/subu wrap can
             * reserve old words for negative counts; never fabricate zeros. */
            uint32_t bytes=vm->stack.top*4u-((uint32_t)right<<2);
            uint32_t floor=vm->stack.top-gool_stack_height(&vm->stack);
            if(bytes>gool_stack_capacity(&vm->stack)*4u||bytes<floor*4u)return fault(vm,GOOL_VM_STACK,GOOL_STACK_UNDERFLOW);
            vm->stack.top=bytes/4u;
        } else {
            if(right<0)return fault(vm,GOOL_VM_STACK,GOOL_STACK_UNDERFLOW);
            uint32_t argc=(uint32_t)right>>8;
            gool_data target;uint32_t offset;
            if(gool_vm_resolve(vm,left,&target,&offset))return fault(vm,GOOL_VM_INVALID,0x47);
            int32_t return_pointer;
            if(gool_vm_address(&vm->code,vm->pc+1,&return_pointer))return fault(vm,GOOL_VM_INVALID,0x47);
            error=gool_stack_enter_flags(&vm->stack,argc,return_pointer,vm->mode);
            if(error)return fault(vm,GOOL_VM_STACK,error);
            vm->mode&=~9u;vm->code=target;vm->pc=offset;return GOOL_VM_RUNNING;
        }
    } else if(op==0x3b) {
        uint32_t address=inst&0x7fff,argc=(inst>>20)&0xf;
        const gool_data *target=(address&0x4000)?vm->external.alternate:vm->external.base;
        uint32_t offset=address&0x3fff;
        if(!target||!target->code||offset>=target->code_words)return fault(vm,GOOL_VM_INVALID,0x3b);
        int32_t return_pointer;
        if(gool_vm_address(&vm->code,vm->pc+1,&return_pointer))return fault(vm,GOOL_VM_INVALID,0x3b);
        error=gool_stack_enter_flags(&vm->stack,argc,return_pointer,vm->mode);
        if(error)return fault(vm,GOOL_VM_STACK,error);
        vm->mode&=~9u;
        vm->code=*target;vm->pc=offset;return GOOL_VM_RUNNING;
    } else if(op==0x31) {
        uint32_t mode=vm->mode;
        if((mode&8)&&!vm->receiver)return fault(vm,GOOL_VM_OPERAND,GOOL_ARG_UNBOUND);
        int returned=return_from_call(vm,!(mode&9u));
        if(returned<0)return returned;
        if((mode&0x11u)==0x11u&&!(mode&8u)&&returned!=GOOL_VM_DONE) {
            int seeded=gool_vm_seed_frame(vm);
            if(seeded)return fault(vm,seeded,0x31);
            return GOOL_VM_RESUME_RETURN;
        }
        if(mode&8) {
            /* Native399FC root result is overridden by the signal return. */
            if(returned==GOOL_VM_DONE)vm->status=GOOL_VM_RUNNING;
            vm->receiver->new_state=(mode&0x20)?255:-1;
            return (mode&0x20)?GOOL_VM_SIGNAL_RETURN:GOOL_VM_FAILED_SIGNAL;
        }
        return returned;
    }
    vm->pc++;return GOOL_VM_RUNNING;
}
int gool_vm_run(gool_vm *vm,uint32_t budget) {
    if(!vm)return GOOL_VM_INVALID;
    for(uint32_t i=0;i<budget;i++) {
        int result=gool_vm_step(vm);
        if(result)return result;
    }
    /* Budget yield is resumable, not a permanent error or silent completion. */
    return vm->status?vm->status:GOOL_VM_BUDGET;
}
int gool_vm_set_tick(gool_vm *vm,uint64_t tick,int respect_delay) {
    if(!vm||tick<vm->tick)return GOOL_VM_INVALID;
    vm->tick=tick;vm->respect_delay=respect_delay!=0;return GOOL_VM_RUNNING;
}
int gool_vm_resume_frame(gool_vm *vm,uint64_t tick) {
    if(!vm||tick<vm->tick)return GOOL_VM_INVALID;
    if(vm->frame_yield) {
        if(!gool_stack_height(&vm->stack)||vm->frame_token_index!=vm->stack.top-1)return GOOL_VM_STACK;
        uint32_t token=(uint32_t)vm->stack.words[vm->frame_token_index];
        uint32_t duration=token>>24,age=(uint32_t)tick-(token&0xffffffu);
        vm->tick=tick;vm->respect_delay=1;
        if(duration>=2&&age<duration)return GOOL_VM_WAIT;
        int32_t discarded;
        if(gool_stack_pop(&vm->stack,&discarded))return GOOL_VM_STACK;
        vm->frame_yield=0;
    } else {vm->tick=tick;vm->respect_delay=1;}
    return GOOL_VM_RUNNING;
}
int gool_vm_seed_frame(gool_vm *vm) {
    if(!vm||vm->status)return GOOL_VM_INVALID;
    uint32_t index=vm->stack.top;
    if(gool_stack_push(&vm->stack,0))return GOOL_VM_STACK;
    vm->frame_token_index=index;vm->frame_yield=1;return GOOL_VM_RUNNING;
}
int gool_vm_bind_globals(gool_vm *vm,int32_t *globals,uint32_t count) {
    if(!vm||(!globals&&count)||(!count&&globals))return GOOL_VM_INVALID;
    vm->globals=globals;vm->global_count=count;return GOOL_VM_RUNNING;
}
int gool_vm_bind_receiver(gool_vm *vm,gool_signal_receiver *receiver) {
    if(!vm)return GOOL_VM_INVALID;
    vm->receiver=receiver;return GOOL_VM_RUNNING;
}
int gool_vm_bind_memory(gool_vm *vm,gool_memory *memory) {
    if(!vm||vm->status)return GOOL_VM_INVALID;
    vm->memory=memory;return GOOL_VM_RUNNING;
}
int gool_vm_bind_address_space(gool_vm *vm,void *context,gool_vm_address_read read,gool_vm_address_write write) {
    if(!vm||vm->status||((read==NULL)!=(write==NULL)))return GOOL_VM_INVALID;
    vm->address_context=context;vm->read_address=read;vm->write_address=write;return GOOL_VM_RUNNING;
}
int gool_vm_bind_frame_factor(gool_vm *vm,const int32_t *factor) {
    if(!vm||vm->status)return GOOL_VM_INVALID;
    vm->frame_factor=factor;return GOOL_VM_RUNNING;
}
int gool_vm_bind_random(gool_vm *vm,uint32_t state[2]) {
    if(!vm||vm->status)return GOOL_VM_INVALID;
    vm->random_state=state;return GOOL_VM_RUNNING;
}
int gool_vm_invoke(gool_vm *vm,const gool_data *code,uint32_t pc,uint32_t argc) {
    if(!vm||vm->status||!code||!code->code||code->lookup_index>=1024||
       pc>=code->code_words||pc>=0x4000||vm->pc>=vm->code.code_words)return GOOL_VM_INVALID;
    int32_t pointer;
    if(vm->has_pending_pc)pointer=signed_word(vm->pending_pc);
    else if(gool_vm_address(&vm->code,vm->pc,&pointer))return GOOL_VM_INVALID;
    int result=gool_stack_enter(&vm->stack,argc,pointer);
    if(result)return GOOL_VM_STACK;
    vm->code=*code;vm->pc=pc;vm->has_pending_pc=0;return GOOL_VM_RUNNING;
}

int gool_vm_bind_phase_source(gool_vm *vm,const int32_t *source) {
    if(!vm||vm->status)return GOOL_VM_INVALID;
    vm->phase_source=source;return GOOL_VM_RUNNING;
}
