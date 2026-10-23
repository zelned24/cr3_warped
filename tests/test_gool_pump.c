/* Production C regression harness; run through tests/test_gool_c.py. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "gool_pump.h"
#ifdef NDEBUG
#error Assertions are required
#endif
static void pack(uint8_t *p,uint32_t value) {for(unsigned j=0;j<4;j++)p[j]=(uint8_t)(value>>(j*8));}
static unsigned spans(const gool_memory *m) {
    unsigned n=0;for(unsigned j=0;j<GOOL_MEMORY_SPANS;j++)n+=m->spans[j].live!=0;return n;
}
static void setup(gool_instance *s,gool_instance *t,gool_instance **registry,
                  gool_object **objects,gool_memory *memory,uint8_t *scode,uint8_t *tcode,uint8_t *indices) {
    memset(s,0,sizeof(*s));memset(t,0,sizeof(*t));gool_memory_init(memory);
    registry[0]=s;registry[1]=t;objects[0]=&s->object;objects[1]=&t->object;
    for(unsigned n=0;n<16;n++){pack(scode+4*n,0x2f000000);pack(tcode+4*n,0x2f000000);}
    for(unsigned n=0;n<64;n++){indices[n*2]=255;indices[n*2+1]=0;}
    pack(scode,0x3c000000|(1u<<21)|(1u<<18)|(14u<<12)|0xe25);
    pack(tcode+8,0x3c000000|(7u<<21)|(14u<<12)|0xa10);pack(tcode+12,0x3e800000);
    for(unsigned n=0;n<2;n++) {
        gool_instance *i=registry[n];
        i->base=(gool_data){.code=n?tcode:scode,.code_words=16,.stack_start=64,
            .lookup_index=n,.signal_count=64,.indices=indices};
        i->alternate=i->base;i->context=(gool_object_context){.objects=objects,.count=2,.self=n};
        gool_operand_context ctx={.base=&i->base,.alternate=&i->alternate,
            .context=&i->context,.read=gool_object_read,.write=gool_object_write};
        assert(gool_vm_init(&i->vm,&i->base,0,&ctx,NULL,0)==0);
        gool_object_reset(&i->object,&i->vm);
        assert(gool_object_set(&i->object,0x1a,0x20020)==0);
        assert(gool_object_set(&i->object,0x1c,0)==0);
        assert(gool_object_set(&i->object,14,4096)==0);
        assert(gool_object_set(&i->object,0x25,0)==0);
        int32_t pointer=0;
        if(n)assert(gool_vm_address(&i->base,2,&pointer)==0);
        assert(gool_object_set(&i->object,0x23,pointer)==0);
        assert(gool_vm_bind_memory(&i->vm,memory)==0);
        assert(gool_vm_seed_frame(&i->vm)==0);
        i->ready=i->active=1;i->state_generation=1;i->vm.send_enabled=1;
    }
    assert(gool_object_set(&s->object,1,0x60000004)==0);
    assert(gool_vm_resume_frame(&s->vm,7)==0);s->vm.mode=4;
    assert(gool_stack_push(&s->vm.stack,70)==0&&gool_vm_step(&s->vm)==GOOL_VM_SEND);
}
int main(void) {
    gool_instance s,t,*registry[2];gool_object *objects[2];gool_memory memory;
    uint8_t scode[64],tcode[64],indices[128];gool_pump pump={0};
    setup(&s,&t,registry,objects,&memory,scode,tcode,indices);
    assert(gool_pump_begin(&pump,registry,2,&s,&memory,7,0)==GOOL_VM_BUDGET);
    assert(gool_pump_step(&pump,0)==GOOL_VM_BUDGET&&spans(&memory)==0);
    assert(gool_pump_step(&pump,1)==GOOL_VM_BUDGET&&pump.depth==1);
    int32_t parent=pump.jobs[0].span_pointer,value;
    assert(gool_pump_step(&pump,1)==GOOL_VM_BUDGET&&pump.pending==&t);
    assert(gool_pump_step(&pump,1)==GOOL_VM_BUDGET&&pump.depth==2&&spans(&memory)==2);
    assert(gool_pump_step(&pump,1)==GOOL_VM_BUDGET&&pump.depth==1&&spans(&memory)==1);
    assert(gool_memory_read(&memory,(uint32_t)parent,&value)==0&&value==70);
    assert(gool_pump_step(&pump,1)==GOOL_VM_RUNNING&&!pump.active&&pump.depth==0);
    assert(!s.vm.send_pending&&s.vm.pc==1&&gool_stack_height(&s.vm.stack)==0);
    assert(spans(&memory)==0&&t.vm.receiver==NULL&&t.vm.frame_yield&&t.vm.mode==0);
    assert(gool_pump_step(&pump,100)==GOOL_VM_RUNNING);
    /* An infinite script is budgeted and abort restores borrowed context. */
    setup(&s,&t,registry,objects,&memory,scode,tcode,indices);pack(tcode+8,0x320003ff);
    assert(gool_pump_begin(&pump,registry,2,&s,&memory,7,0)==GOOL_VM_BUDGET);
    assert(gool_pump_step(&pump,20)==GOOL_VM_BUDGET&&pump.active);
    assert(gool_pump_abort(&pump,GOOL_VM_UNSUPPORTED)==GOOL_VM_UNSUPPORTED);
    assert(spans(&memory)==0&&t.vm.receiver==NULL&&s.vm.send_pending);
    assert(gool_pump_step(&pump,1)==GOOL_VM_UNSUPPORTED);
    /* Malformed destination is not reported as a successful absent object. */
    setup(&s,&t,registry,objects,&memory,scode,tcode,indices);s.vm.send.target=0x60000005;
    assert(gool_pump_begin(&pump,registry,2,&s,&memory,7,0)==GOOL_VM_BUDGET);
    assert(gool_pump_step(&pump,1)==GOOL_VM_OPERAND&&s.vm.send_pending&&spans(&memory)==0);
    /* Recursive callbacks hit the explicit resource budget then unwind LIFO. */
    setup(&s,&t,registry,objects,&memory,scode,tcode,indices);
    pack(scode+8,0x3c000000|(7u<<21)|(14u<<12)|0xa10);
    int32_t on_signal;assert(gool_vm_address(&s.base,2,&on_signal)==0);
    assert(gool_object_set(&s.object,0x23,on_signal)==0);
    assert(gool_pump_begin(&pump,registry,2,&s,&memory,7,0)==GOOL_VM_BUDGET);
    assert(gool_pump_step(&pump,1000)==GOOL_VM_STACK&&!pump.active&&pump.depth==0);
    assert(spans(&memory)==0&&s.vm.receiver==NULL&&t.vm.receiver==NULL);
    assert(s.vm.send_pending&&!t.vm.send_pending);
    puts("PASS signal-pump composition harness; not a world scheduler test");return 0;
}
