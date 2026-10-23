#include <stdio.h>
#include <stdint.h>
#include "gool_operand.h"
#include "gool_vm.h"
static int32_t members[2];
static int32_t code_pointer;
static int read_member(void *ctx,const gool_operand *arg,int32_t *value) {
    (void)ctx;
    if(arg->kind!=GOOL_ARG_VARIABLE||arg->value<0x25||arg->value>0x26)return GOOL_ARG_UNBOUND;
    *value=members[arg->value-0x25];return 0;
}
static int write_member(void *ctx,const gool_operand *arg,int32_t value) {
    (void)ctx;
    if(arg->kind!=GOOL_ARG_VARIABLE)return GOOL_ARG_UNBOUND;
    if(arg->value==0x22){code_pointer=value;return 0;}
    if(arg->value<0x25||arg->value>0x26)return GOOL_ARG_UNBOUND;
    members[arg->value-0x25]=value;return 0;
}
static void pack(uint8_t *p,uint32_t n){for(unsigned j=0;j<4;j++)p[j]=(uint8_t)(n>>(j*8));}
int main(void){
    const uint8_t constants[]={255,255,255,255,0,0,0,128};
    gool_data base0={.constants=constants,.constant_words=2};
    gool_operand_context ctx={.base=&base0,.context=&members,.read=read_member,.write=write_member};
    printf("write e20=%d writes?\n",gool_operand_write(&ctx,0xe20,5));
    printf("write c20=%d\n",gool_operand_write(&ctx,0xc20,5));
    gool_operand a; gool_operand_decode(0xe20,&a); printf("decode e20 kind=%d value=%d\n",a.kind,a.value);
    gool_operand_decode(0xc20,&a); printf("decode c20 kind=%d value=%d base=%u\n",a.kind,a.value,a.base);

    uint8_t code[20];
    uint32_t program[]={0x187c0004,0x11e1fe25,0x47800e25,0x2f000000,0x31000000};
    for(unsigned j=0;j<5;j++)pack(code+j*4,program[j]);
    gool_data base={.code=code,.code_words=5,.stack_start=64,.lookup_index=0};
    gool_vm vm;
    printf("init=%d\n",gool_vm_init(&vm,&base,0,&ctx,NULL,0));vm.mode=3;
    for(unsigned j=0;j<4;j++)printf("step=%d\n",gool_vm_step(&vm));
    pack(code,0x18800000);
    printf("init2=%d\n",gool_vm_init(&vm,&base,0,&ctx,NULL,0));
    int st=gool_vm_step(&vm);
    printf("script_ptr step=%d pc=%u code_pointer=%08x\n",st,(unsigned)vm.pc,(unsigned)code_pointer);
    return 0;
}
