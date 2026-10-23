#include "gool_object.h"
#include <string.h>

void gool_object_reset(gool_object *object,gool_vm *vm) {
    if(!object)return;
    memset(object,0,sizeof(*object));object->vm=vm;
}
int gool_object_set(gool_object *object,uint32_t member,int32_t value) {
    if(!object||member>=GOOL_OBJECT_MEMBERS)return GOOL_ARG_INVALID;
    object->members[member]=value;object->known[member/32]|=1u<<(member%32);
    return 0;
}
int gool_object_get(const gool_object *object,uint32_t member,int32_t *value) {
    if(!object||!value||member>=GOOL_OBJECT_MEMBERS)return GOOL_ARG_INVALID;
    if(!(object->known[member/32]&(1u<<(member%32))))return GOOL_ARG_UNBOUND;
    *value=object->members[member];return 0;
}
int gool_object_lighting_set(gool_object *object,uint32_t selector,int32_t value) {
    if(!object||selector>=16)return GOOL_ARG_INVALID;
    object->lighting[selector]=(uint16_t)(uint32_t)value;
    object->lighting_known|=(uint16_t)(1u<<selector);return 0;
}
int gool_object_lighting_get(const gool_object *object,uint32_t selector,int32_t *value) {
    if(!object||!value||selector>=16)return GOOL_ARG_INVALID;
    if(!(object->lighting_known&(1u<<selector)))return GOOL_ARG_UNBOUND;
    uint32_t bits=object->lighting[selector];
    *value=bits&0x8000u?(int32_t)bits-65536:(int32_t)bits;return 0;
}
int gool_object_pointer(uint32_t index,int32_t *value) {
    if(!value||index>=0x04000000u)return GOOL_ARG_RANGE;
    *value=(int32_t)(0x60000000u|(index<<2));return 0;
}
int gool_object_bind_stack(gool_object *object,gool_memory *memory) {
    if(!object||!object->vm||!memory||object->vm->memory!=memory)return GOOL_MEMORY_INVALID;
    if(object->stack_memory)return GOOL_MEMORY_INVALID;
    int32_t pointer;
    int result=gool_memory_borrow(memory,object->vm->stack.words,gool_stack_capacity(&object->vm->stack),1,&pointer);
    if(result)return result;
    object->stack_memory=memory;object->stack_pointer=pointer;return 0;
}
int gool_object_unbind_stack(gool_object *object) {
    if(!object||!object->stack_memory)return GOOL_MEMORY_UNBOUND;
    int result=gool_memory_release(object->stack_memory,object->stack_pointer);
    object->stack_memory=NULL;object->stack_pointer=0;return result;
}
static int stack_address(const gool_object *object,int32_t *value) {
    if(!object->vm||!object->stack_memory||object->vm->stack.top>gool_stack_capacity(&object->vm->stack))
        return GOOL_ARG_UNBOUND;
    /* Reject released/rebound spans, including recycled VM storage. */
    for(uint32_t n=0;n<GOOL_MEMORY_SPANS;n++) {
        const gool_memory_span *span=&object->stack_memory->spans[n];
        if(span->live&&span->base==(uint32_t)object->stack_pointer&&
           span->words==object->vm->stack.words&&span->count==gool_stack_capacity(&object->vm->stack)) {
            *value=(int32_t)(span->base+object->vm->stack.top*4);return 0;
        }
    }
    return GOOL_ARG_UNBOUND;
}
static int resolve(gool_object_context *ctx,uint32_t base,gool_object **out,uint32_t *index) {
    if(!ctx||!out||!index||!ctx->objects||ctx->self>=ctx->count||!ctx->objects[ctx->self])return GOOL_ARG_UNBOUND;
    uint32_t target=ctx->self;
    if(base==5) {
        if(!ctx->has_player)return GOOL_ARG_UNBOUND;
        target=ctx->player;
    } else if(base!=0) {
        if(base>=8)return GOOL_ARG_INVALID;
        int32_t value;int error=gool_object_get(ctx->objects[ctx->self],base,&value);
        if(error)return error;
        uint32_t pointer=(uint32_t)value;
        if((pointer>>28)!=6||(pointer&3))return GOOL_ARG_UNBOUND;
        target=(pointer&0x0fffffffu)>>2;
    }
    if(target>=ctx->count||!ctx->objects[target])return GOOL_ARG_UNBOUND;
    *out=ctx->objects[target];*index=target;return 0;
}
static int read_member(gool_object_context *ctx,gool_object *object,uint32_t index,
                       uint32_t member,int32_t *value) {
    if(member==0)return gool_object_pointer(index,value);
    if(member==5) {
        if(!ctx->has_player||ctx->player>=ctx->count||!ctx->objects[ctx->player])return GOOL_ARG_UNBOUND;
        return gool_object_pointer(ctx->player,value);
    }
    if(member==31) {
        /* INDIRECT loads raw object+0xbc; only direct STACK pops (VM layer).
         * SCUS-94244 fast operand read 0x80038d2c..0x80038d5c. */
        if(!ctx->objects[ctx->self]->vm||
           ctx->objects[ctx->self]->vm->memory!=object->stack_memory)return GOOL_ARG_UNBOUND;
        return stack_address(object,value);
    }
    /* Other entities' live SCRIPT_PTR and stackStart overlays need dispatcher
     * phase/registry semantics. Reject rather than read stale raw storage. */
    if(member==32)return GOOL_ARG_UNBOUND;
    if(object->vm&&object->vm->external.base&&member>=object->vm->external.base->stack_start)
        return GOOL_ARG_UNBOUND;
    return gool_object_get(object,member,value);
}
int gool_object_read(void *context,const gool_operand *arg,int32_t *value) {
    if(!arg||!value)return GOOL_ARG_INVALID;
    gool_object_context *ctx=context;
    gool_object *object;uint32_t index;
    if(arg->kind==GOOL_ARG_HALFWORD) {
        if(arg->value<0||arg->value>=64)return GOOL_ARG_RANGE;
        int error=resolve(ctx,arg->base,&object,&index);
        if(error)return error;
        if(arg->value<16)return gool_object_lighting_get(object,(uint32_t)arg->value,value);
        /* Link words use tagged port handles. Their raw PS1 address halves
         * cannot be reconstructed by splitting that different representation. */
        uint32_t member=((uint32_t)arg->value-16)/2;
        if(member<8)return GOOL_ARG_UNBOUND;
        int32_t word;
        error=read_member(ctx,object,index,member,&word);
        if(error)return error;
        uint32_t bits=((uint32_t)word>>((arg->value&1)*16))&0xffffu;
        *value=bits&0x8000u?(int32_t)bits-65536:(int32_t)bits;return 0;
    }
    uint32_t base=arg->kind==GOOL_ARG_INDIRECT?arg->base:0;
    if(arg->kind!=GOOL_ARG_VARIABLE&&arg->kind!=GOOL_ARG_INDIRECT)return GOOL_ARG_INVALID;
    if(arg->value<0||(uint32_t)arg->value>=GOOL_OBJECT_MEMBERS)return GOOL_ARG_RANGE;
    /* Native fast reader 0x80038d48 returns the null link itself before
     * dereferencing the requested member. Unknown/malformed links still fail. */
    if(arg->kind==GOOL_ARG_INDIRECT&&base!=0&&base!=5&&base<8) {
        if(!ctx||!ctx->objects||ctx->self>=ctx->count||!ctx->objects[ctx->self])
            return GOOL_ARG_UNBOUND;
        int32_t link;
        int result=gool_object_get(ctx->objects[ctx->self],base,&link);
        if(result)return result;
        if(!link){*value=0;return 0;}
    }
    int error=resolve(ctx,base,&object,&index);
    if(error)return error;
    return read_member(ctx,object,index,(uint32_t)arg->value,value);
}
int gool_object_write(void *context,const gool_operand *arg,int32_t value) {
    if(!arg)return GOOL_ARG_INVALID;
    gool_object_context *ctx=context;
    gool_object *object;uint32_t index;
    if(arg->kind==GOOL_ARG_HALFWORD) {
        if(arg->value<0||arg->value>=64)return GOOL_ARG_RANGE;
        int error=resolve(ctx,arg->base,&object,&index);
        if(error)return error;
        if(arg->value<16)return gool_object_lighting_set(object,(uint32_t)arg->value,value);
        uint32_t member=((uint32_t)arg->value-16)/2;
        if(member<8)return GOOL_ARG_UNBOUND;
        int32_t word;
        error=read_member(ctx,object,index,member,&word);
        if(error)return error;
        unsigned shift=(arg->value&1)*16;
        uint32_t bits=((uint32_t)word&~(0xffffu<<shift))|(((uint32_t)value&0xffffu)<<shift);
        int32_t result=bits<=INT32_MAX?(int32_t)bits:(int32_t)((int64_t)bits-INT64_C(4294967296));
        return gool_object_set(object,member,result);
    }
    uint32_t base=arg->kind==GOOL_ARG_INDIRECT?arg->base:0;
    if(arg->kind!=GOOL_ARG_VARIABLE&&arg->kind!=GOOL_ARG_INDIRECT)return GOOL_ARG_INVALID;
    if(arg->value<0||(uint32_t)arg->value>=GOOL_OBJECT_MEMBERS)return GOOL_ARG_RANGE;
    int error=resolve(ctx,base,&object,&index);
    if(error)return error;
    if(arg->value==32)return GOOL_ARG_INVALID;
    if(arg->value==31) {
        /* Raw foreign SP assignment is not a push. Until frame ownership and
         * dispatcher spill/reload are bound, fail without mutating the stack. */
        return GOOL_ARG_UNBOUND;
    }
    if(arg->value==0||arg->value==5)return GOOL_ARG_UNBOUND;
    if(object->vm&&object->vm->external.base&&(uint32_t)arg->value>=object->vm->external.base->stack_start)
        return GOOL_ARG_UNBOUND;
    return gool_object_set(object,(uint32_t)arg->value,value);
}
int gool_object_begin_update(gool_object *object,uint64_t tick) {
    if(!object||!object->vm)return GOOL_ARG_UNBOUND;
    int result=gool_vm_set_tick(object->vm,tick,1);
    if(result)return result;
    uint32_t bits=(uint32_t)tick;
    int32_t signed_tick=bits<=INT32_MAX?(int32_t)bits:(int32_t)((int64_t)bits-INT64_C(4294967296));
    return gool_object_set(object,0x27,signed_tick);
}
