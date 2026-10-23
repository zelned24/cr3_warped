#include <assert.h>
#include <stdio.h>
#include "gool_object.h"
#ifdef NDEBUG
#error Assertions are required
#endif
static void pack(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
int main(void) {
    gool_object a,b;gool_vm vm;gool_object_reset(&a,&vm);gool_object_reset(&b,NULL);
    gool_object *objects[]={&a,&b};
    gool_object_context context={.objects=objects,.count=2,.self=0,.player=1,.has_player=1};
    uint8_t code[4];pack(code,0x2f000000);
    gool_data data={.code=code,.code_words=1,.stack_start=512};
    gool_operand_context args={.base=&data,.context=&context,.read=gool_object_read,.write=gool_object_write};
    assert(gool_vm_init(&vm,&data,0,&args,NULL,0)==0);
    int32_t pointer,value=123;
    assert(gool_object_pointer(1,&pointer)==0&&gool_object_set(&a,1,pointer)==0);
    for(unsigned selector=0;selector<16;selector++) {
        gool_operand half={.kind=GOOL_ARG_HALFWORD,.base=1,.value=(int32_t)selector};
        assert(gool_object_read(&context,&half,&value)==GOOL_ARG_UNBOUND&&value==123);
        assert(gool_object_write(&context,&half,0x12348000+(int32_t)selector)==0);
        assert(gool_object_read(&context,&half,&value)==0&&value==-32768+(int32_t)selector);
        value=123;
    }
    assert(gool_object_set(&b,8,0x12345678)==0);
    gool_operand low={.kind=GOOL_ARG_HALFWORD,.base=1,.value=32},high=low;high.value=33;
    assert(gool_object_write(&context,&low,0xabcd)==0);
    assert(gool_object_get(&b,8,&value)==0&&(uint32_t)value==0x1234abcdu);
    assert(gool_object_write(&context,&high,0xfedc)==0);
    assert(gool_object_get(&b,8,&value)==0&&(uint32_t)value==0xfedcabcdu);
    assert(gool_object_read(&context,&low,&value)==0&&value==-21555);
    /* Actual VM GET resolves the target context and sign-extends its field. */
    pack(code,0x23001000|(32u<<15));
    assert(gool_vm_step(&vm)==0&&gool_stack_pop(&vm.stack,&value)==0&&value==-21555);
    /* Null-target SH still consumes source STACK before rejecting the address. */
    pack(code,0x24001e1f);assert(gool_object_set(&a,1,0)==0);
    assert(gool_vm_init(&vm,&data,0,&args,NULL,0)==0&&gool_stack_push(&vm.stack,42)==0);
    assert(gool_vm_step(&vm)==GOOL_VM_OPERAND&&gool_stack_height(&vm.stack)==0&&vm.pc==0);
    /* No partial word is synthesized from an unknown previous value. */
    assert(gool_object_set(&a,1,pointer)==0);low.value=34;
    assert(gool_object_write(&context,&low,99)==GOOL_ARG_UNBOUND);
    assert(gool_object_get(&b,9,&value)==GOOL_ARG_UNBOUND);
    for(unsigned selector=16;selector<32;selector++) {
        low.value=(int32_t)selector;
        assert(gool_object_write(&context,&low,99)==GOOL_ARG_UNBOUND);
        assert(gool_object_read(&context,&low,&value)==GOOL_ARG_UNBOUND);
    }
    puts("PASS native halfword object storage, word aliases, signed GET, source-first SH and unknown providers");
    return 0;
}
