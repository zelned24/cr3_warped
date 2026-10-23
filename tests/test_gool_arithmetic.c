/* Production C regression harness; run through tests/test_gool_c.py. */
#include <assert.h>
#include <stdint.h>
#include "gool_vm.h"
#ifdef NDEBUG
#error Assertions required
#endif
static void pack(uint8_t *p,uint32_t n){for(unsigned j=0;j<4;j++)p[j]=(uint8_t)(n>>(8*j));}
static int32_t execute(uint32_t op,int32_t left,int32_t right,uint32_t *state){
    uint8_t code[4],constants[8];pack(code,op<<24|1u<<12);
    pack(constants,(uint32_t)left);pack(constants+4,(uint32_t)right);
    gool_data data={.code=code,.code_words=1,.constants=constants,.constant_words=2,.stack_start=64};
    gool_operand_context args={.base=&data};gool_vm vm;int32_t result;
    assert(gool_vm_init(&vm,&data,0,&args,NULL,0)==0);
    assert(gool_vm_bind_random(&vm,state)==0);
    assert(gool_vm_step(&vm)==0&&gool_stack_pop(&vm.stack,&result)==0);
    return result;
}
int main(void){
    assert(execute(2,INT32_MIN,-1,NULL)==INT32_MIN);
    assert(execute(2,65536,65536,NULL)==0);
    assert(execute(5,7,9,NULL)==7);
    assert(execute(5,7,0,NULL)==0);
    assert(execute(3,7,0,NULL)==-1&&execute(3,-7,0,NULL)==1);
    assert(execute(13,-7,0,NULL)==-7);
    uint32_t state[]={0x12345678,0xabcdef01};
    uint32_t a=(state[0]>>8)|(state[1]<<24);
    uint32_t b=(state[1]>>8)|((state[0]+(state[1]>>8)+(a>>8))<<24);
    int32_t result=execute(0x10,0,256,state);
    assert(result==(int32_t)(b%256)&&state[0]==a&&state[1]==b);
    return 0;
}
