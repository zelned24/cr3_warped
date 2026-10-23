/* Production C regression harness; run through tests/test_gool_c.py. */
#include <assert.h>
#include <stdio.h>
#include "gool_object.h"
#ifdef NDEBUG
#error Assertions are required
#endif
int main(void) {
    gool_object a,b;gool_vm avm,bvm;
    gool_object_reset(&a,&avm);gool_object_reset(&b,&bvm);
    gool_object *objects[]={&a,&b,NULL};
    gool_object_context actx={.objects=objects,.count=3,.self=0,.player=1,.has_player=1};
    gool_object_context bctx={.objects=objects,.count=3,.self=1};
    const uint8_t code[]={0,0,0,0x2f,0,0,0,0x31};
    gool_data base={.code=code,.code_words=2,.stack_start=512};
    gool_operand_context args={.base=&base,.context=&actx,.read=gool_object_read,.write=gool_object_write};
    assert(gool_vm_init(&avm,&base,0,&args,NULL,0)==0);
    args.context=&bctx;assert(gool_vm_init(&bvm,&base,0,&args,NULL,0)==0);
    args.context=&actx;
    int32_t value=987,pointer;
    assert(gool_object_pointer(1,&pointer)==0&&pointer==0x60000004);
    assert(gool_object_set(&a,1,pointer)==0);
    assert(gool_object_get(&a,0x27,&value)==GOOL_ARG_UNBOUND&&value==987);
    assert(gool_object_set(&a,0x27,0)==0&&gool_object_get(&a,0x27,&value)==0&&value==0);
    assert(gool_object_set(&b,0x25,75)==0);
    assert(gool_operand_read(&args,0xc65,&value)==0&&value==75);
    assert(gool_operand_write(&args,0xc65,99)==0);
    assert(gool_object_get(&b,0x25,&value)==0&&value==99);
    assert(gool_operand_read(&args,0xc00,&value)==0&&value==0x60000000);
    assert(gool_operand_read(&args,0xc40,&value)==0&&value==0x60000004);
    assert(gool_operand_read(&args,0xd65,&value)==0&&value==99); /* PLAYER.TEMP_A */
    assert(gool_stack_push(&bvm.stack,55)==0);
    uint32_t old_top=bvm.stack.top;
    assert(gool_operand_read(&args,0xc5f,&value)==GOOL_ARG_UNBOUND);
    gool_memory memory;gool_memory_init(&memory);
    assert(gool_vm_bind_memory(&avm,&memory)==0);
    assert(gool_vm_bind_memory(&bvm,&memory)==0);
    assert(gool_object_bind_stack(&b,&memory)==0);
    assert(gool_operand_read(&args,0xc5f,&value)==0);
    assert((uint32_t)value==(uint32_t)b.stack_pointer+old_top*4&&bvm.stack.top==old_top);
    assert(gool_memory_read(&memory,(uint32_t)value-4,&value)==0&&value==55);
    assert(gool_operand_write(&args,0xc5f,66)==GOOL_ARG_UNBOUND);
    assert(gool_stack_pop(&bvm.stack,&value)==0&&value==55);
    assert(gool_object_unbind_stack(&b)==0);
    assert(gool_operand_read(&args,0xc5f,&value)==GOOL_ARG_UNBOUND);
    assert(gool_object_begin_update(&a,17)==0&&gool_object_get(&a,0x27,&value)==0&&value==17);
    assert(gool_object_begin_update(&a,16)==GOOL_VM_INVALID);
    assert(gool_object_get(&a,0x27,&value)==0&&value==17);
    assert(gool_object_set(&a,1,0)==0);
    for(unsigned member=0;member<64;member++) {
        assert(gool_operand_read(&args,0xc40u|member,&value)==0&&value==0);
        assert(gool_operand_write(&args,0xc40u|member,99)!=0);
    }
    const int32_t invalid[]={0x50000004,0x60000005,0x60000008,0x600000fc};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        assert(gool_object_set(&a,1,invalid[i])==0);
        assert(gool_operand_read(&args,0xc65,&value)==GOOL_ARG_UNBOUND);
    }
    assert(gool_object_set(&a,1,pointer)==0);objects[1]=NULL;
    assert(gool_operand_read(&args,0xc65,&value)==GOOL_ARG_UNBOUND);
    assert(gool_object_pointer(0x4000000,&pointer)==GOOL_ARG_RANGE);
    puts("PASS object context: known members, indirect links, remote stack and explicit update timestamp");
    return 0;
}
