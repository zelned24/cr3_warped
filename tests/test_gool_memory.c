/* Production C regression harness; run through tests/test_gool_c.py. */
#include <assert.h>
#include <stdio.h>
#include "gool_vm.h"
#ifdef NDEBUG
#error Assertions are required
#endif
static void pack(uint8_t *p,uint32_t n) {for(unsigned j=0;j<4;j++)p[j]=(uint8_t)(n>>(j*8));}
int main(void) {
    gool_memory memory;gool_memory_init(&memory);
    int32_t values[]={101,-77,303},pointer,value=123;
    assert(gool_memory_borrow(&memory,values,3,1,&pointer)==0&&pointer!=0);
    assert(gool_memory_read(&memory,(uint32_t)pointer+4,&value)==0&&value==-77);
    assert(gool_memory_read(&memory,(uint32_t)pointer+12,&value)==GOOL_MEMORY_UNBOUND);
    assert(gool_memory_write(&memory,(uint32_t)pointer+1,44)==GOOL_MEMORY_UNBOUND);
    uint8_t code[8];pack(code,0x2a801b7f);pack(code+4,0x4e802b7f);
    gool_data data={.code=code,.code_words=2,.stack_start=64};
    gool_operand_context context={.base=&data};gool_vm vm;
    assert(gool_vm_init(&vm,&data,0,&context,&pointer,1)==0);
    assert(gool_vm_bind_memory(&vm,&memory)==0);
    assert(gool_vm_step(&vm)==0&&gool_vm_step(&vm)==0);
    assert(values[2]==-77&&gool_stack_height(&vm.stack)==0);
    assert(gool_memory_release(&memory,pointer)==0);
    assert(gool_memory_read(&memory,(uint32_t)pointer,&value)==GOOL_MEMORY_UNBOUND);
    int32_t next;
    assert(gool_memory_borrow(&memory,values,3,0,&next)==0&&next!=pointer);
    assert(gool_memory_write(&memory,(uint32_t)next,11)==GOOL_MEMORY_READ_ONLY);
    assert(gool_memory_release(&memory,next+4)==GOOL_MEMORY_UNBOUND);
    assert(gool_memory_release(&memory,next)==0);
    assert(gool_memory_borrow(&memory,NULL,0,1,&next)==0);
    assert(gool_memory_read(&memory,(uint32_t)next,&value)==GOOL_MEMORY_UNBOUND);
    assert(gool_memory_release(&memory,next)==0);
    for(unsigned j=0;j<GOOL_MEMORY_SPANS;j++)assert(gool_memory_borrow(&memory,values,3,1,&next)==0);
    assert(gool_memory_borrow(&memory,values,3,1,&pointer)==GOOL_MEMORY_CAPACITY);
    gool_memory_init(&memory);memory.next_address=0x7ffffffc;
    assert(gool_memory_borrow(&memory,values,3,1,&pointer)==GOOL_MEMORY_CAPACITY);
    assert(gool_memory_borrow(&memory,NULL,0,1,&pointer)==0);
    assert(gool_memory_borrow(&memory,NULL,0,1,&pointer)==GOOL_MEMORY_CAPACITY);
    puts("PASS borrowed spans/array harness; production C execution");return 0;
}
