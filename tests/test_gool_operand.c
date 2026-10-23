/* Production C regression harness; run through tests/test_gool_c.py. */
#include <assert.h>
#include <stdio.h>
#include "gool_operand.h"
#ifdef NDEBUG
#error Assertions are required
#endif
static int reads,writes,fail;
static gool_operand last;
static int32_t stored;
static int read_value(void *context,const gool_operand *arg,int32_t *value) {
    assert(context==&stored);reads++;last=*arg;
    *value=123;return fail?-20:0;
}
static int write_value(void *context,const gool_operand *arg,int32_t value) {
    assert(context==&stored);writes++;last=*arg;if(fail)return -20;stored=value;return 0;
}
int main(void) {
    const uint8_t constants[]={255,255,255,255,0,0,0,128};
    const uint8_t alternate[]={120,86,52,18};
    gool_data base={.constants=constants,.constant_words=2};
    gool_data alt={.constants=alternate,.constant_words=1};
    gool_operand_context ctx={.base=&base,.alternate=&alt,.context=&stored,
        .read=read_value,.write=write_value};
    int32_t value=7;
    assert(gool_operand_read(&ctx,0,&value)==0&&value==-1);
    assert(gool_operand_read(&ctx,1,&value)==0&&value==INT32_MIN);
    assert(gool_operand_read(&ctx,0x400,&value)==0&&value==0x12345678);
    assert(gool_operand_read(&ctx,0x401,&value)==GOOL_ARG_RANGE&&value==0x12345678);
    assert(gool_operand_read(&ctx,0x900,&value)==0&&value==-65536);
    assert(gool_operand_read(&ctx,0xa80,&value)==0&&value==-2048);
    assert(gool_operand_read(&ctx,0xb90,&value)==0&&value==1);
    assert(gool_operand_read(&ctx,0xe1f,&value)==0&&reads==1&&last.kind==GOOL_ARG_VARIABLE&&last.value==31);
    assert(gool_operand_read(&ctx,0xc6a,&value)==0&&last.kind==GOOL_ARG_INDIRECT&&last.base==1&&last.value==42);
    assert(gool_operand_read(&ctx,0xb7f,&value)==0&&last.kind==GOOL_ARG_FRAME&&last.value==-1);
    assert(gool_operand_read(&ctx,0xb03,&value)==0&&last.kind==GOOL_ARG_FRAME&&last.value==3);
    for(unsigned raw=0xb00;raw<=0xb02;raw++) {
        assert(gool_operand_read(&ctx,raw,&value)==0&&last.kind==GOOL_ARG_FRAME&&last.value==(int32_t)(raw-0xb00));
    }
    value=99;fail=1;
    assert(gool_operand_read(&ctx,0xe25,&value)==-20&&value==99);fail=0;
    assert(gool_operand_write(&ctx,0xe1f,12)==0&&stored==12&&writes==1);
    assert(gool_operand_write(&ctx,0xb7f,15)==0&&stored==15&&writes==2);
    /* Native fast stores publish raw members, including object+C0. The
     * provider owns register/cache policy; operand decode must not suppress it. */
    assert(gool_operand_write(&ctx,0xe20,5)==0&&writes==3&&stored==5&&
           last.kind==GOOL_ARG_VARIABLE&&last.value==0x20);
    assert(gool_operand_write(&ctx,0xc20,6)==0&&writes==4&&stored==6&&
           last.kind==GOOL_ARG_INDIRECT&&last.base==0&&last.value==0x20);
    fail=1;
    assert(gool_operand_write(&ctx,0xe20,7)==-20&&writes==5&&stored==6);
    assert(gool_operand_write(&ctx,0xc20,8)==-20&&writes==6&&stored==6);fail=0;
    for(unsigned raw=0;raw<4096;raw++) {
        gool_operand arg;uint32_t simple;
        int result=gool_operand_decode(raw,&arg);
        {
            assert(result==0);
            if(arg.kind==GOOL_ARG_CONSTANT||arg.kind==GOOL_ARG_LITERAL||arg.kind==GOOL_ARG_BOOLEAN)
                assert(gool_operand_write(&ctx,raw,5)==GOOL_ARG_READ_ONLY);
        }
        assert(gool_operand_simple(raw,&simple)==0&&simple<4096);
    }
    ctx.read=NULL;ctx.write=NULL;
    assert(gool_operand_read(&ctx,0xe25,&value)==GOOL_ARG_UNBOUND);
    assert(gool_operand_write(&ctx,0xe25,1)==GOOL_ARG_UNBOUND);
    assert(gool_operand_decode(4096,&last)==GOOL_ARG_INVALID);
    assert(gool_operand_read(NULL,0,&value)==GOOL_ARG_INVALID);
    puts("PASS GOOL operands: constants, signs, callbacks, write protection and 12-bit decode");
    return 0;
}
