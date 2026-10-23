/* Production C regression harness; run through tests/test_gool_c.py. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "gool_delivery.h"
#ifdef NDEBUG
#error Assertions are required
#endif
static void pack(uint8_t *p,uint32_t n) {for(unsigned j=0;j<4;j++)p[j]=(uint8_t)(n>>(j*8));}
static void setup(gool_instance *s,gool_instance *t,gool_object **objects,
                  uint8_t *scode,uint8_t *tcode,uint8_t *indices,uint8_t *states,uint8_t *constants) {
    memset(s,0,sizeof(*s));memset(t,0,sizeof(*t));
    objects[0]=&s->object;objects[1]=&t->object;
    for(unsigned j=0;j<8;j++){pack(scode+4*j,0x2f000000);pack(tcode+4*j,0x2f000000);}
    pack(scode,0x3c000000|(1u<<21)|(1u<<18)|(14u<<12)|0xe25);
    pack(tcode+8,0x3e400001);
    memset(indices,0,2);indices[0]=255;memset(states,0,32);pack(constants,123);
    gool_instance *pair[]={s,t};
    for(unsigned j=0;j<2;j++) {
        gool_instance *i=pair[j];
        i->base=(gool_data){.code=j?tcode:scode,.code_words=8,.stack_start=64,.lookup_index=j,
            .indices=indices,.signal_count=1,.states=states,.state_count=2,.constants=constants,.constant_words=1};
        i->alternate=i->base;
        i->context=(gool_object_context){.objects=objects,.count=2,.self=j};
        gool_operand_context ctx={.base=&i->base,.alternate=&i->alternate,.context=&i->context,
            .read=gool_object_read,.write=gool_object_write};
        assert(gool_vm_init(&i->vm,&i->base,0,&ctx,NULL,0)==0);
        gool_object_reset(&i->object,&i->vm);
        assert(gool_object_set(&i->object,0x1a,0x20020)==0);
        assert(gool_object_set(&i->object,0x1c,0)==0);
        assert(gool_object_set(&i->object,14,4096)==0);
        assert(gool_object_set(&i->object,0x25,0)==0);
        int32_t pointer=0;
        if(j)assert(gool_vm_address(&i->base,2,&pointer)==0);
        assert(gool_object_set(&i->object,0x23,pointer)==0);
        i->ready=i->active=1;i->vm.send_enabled=1;
    }
    assert(gool_object_set(&s->object,1,0x60000004)==0);
    assert(gool_vm_set_tick(&s->vm,7,1)==0);s->vm.mode=4;
    assert(gool_vm_seed_frame(&t->vm)==0);
    assert(gool_stack_push(&s->vm.stack,77)==0);
    assert(gool_vm_step(&s->vm)==GOOL_VM_SEND);
}
int main(void) {
    gool_instance s,t;gool_object *objects[2];
    uint8_t scode[32],tcode[32],indices[2],states[32],constants[4];
    setup(&s,&t,objects,scode,tcode,indices,states,constants);
    gool_delivery d={0};uint32_t top=t.vm.stack.top;
    assert(gool_delivery_begin(&d,&s,&t,-1342177280,7)==0); /* host span handle 0xb0000000 */
    assert(gool_delivery_step(&d,0)==GOOL_VM_BUDGET);
    assert(gool_delivery_step(&d,1)==GOOL_DELIVERY_ROUTE);
    assert(d.route.kind==GOOL_SIGNAL_STATE&&d.route.value==1&&t.state==0);
    assert(!d.active&&t.vm.receiver==NULL&&t.vm.stack.top==top&&s.vm.send_pending);
    int32_t value;
    assert(gool_object_get(&s.object,0x25,&value)==0&&value==1);
    assert(gool_object_get(&t.object,7,&value)==0&&value==0x60000000);
    assert(gool_object_get(&t.object,0x37,&value)==0&&value==0);
    /* Separate fixture: unhandled/default NONE actually finishes the wrapper. */
    setup(&s,&t,objects,scode,tcode,indices,states,constants);pack(tcode+8,0x31000000);
    memset(&d,0,sizeof(d));
    assert(gool_delivery_begin(&d,&s,&t,-1342177280,7)==0);
    assert(gool_delivery_step(&d,1)==0&&!d.active&&!s.vm.send_pending);
    assert(gool_object_get(&s.object,0x25,&value)==0&&value==0);
    assert(gool_stack_height(&s.vm.stack)==0);
    /* Budget pause borrows context until completion or explicit abort. */
    setup(&s,&t,objects,scode,tcode,indices,states,constants);pack(tcode+8,0x320003ff);
    memset(&d,0,sizeof(d));
    assert(gool_delivery_begin(&d,&s,&t,-1342177280,7)==0);
    assert(gool_delivery_step(&d,10)==GOOL_VM_BUDGET&&t.vm.receiver==&d.receiver);
    assert(gool_delivery_abort(&d,GOOL_VM_UNSUPPORTED)==GOOL_VM_UNSUPPORTED);
    assert(t.vm.receiver==NULL&&s.vm.send_pending);
    /* Explicit script application preserves paused main/token; no archive
     * needed for this synthetic base-code route. */
    setup(&s,&t,objects,scode,tcode,indices,states,constants);
    assert(gool_object_set(&t.object,0x23,0)==0);
    indices[0]=4;indices[1]=0x80; /* explicit base PC 4 */
    pack(tcode+16,0x11b7fe26);pack(tcode+20,0x31000000);
    memset(&d,0,sizeof(d));top=t.vm.stack.top;
    uint32_t token=t.vm.frame_token_index;
    assert(gool_delivery_begin(&d,&s,&t,0,7)==0);
    assert(gool_delivery_step(&d,1)==GOOL_DELIVERY_ROUTE);
    assert(gool_delivery_apply(&d,0,0)==GOOL_VM_BUDGET&&t.vm.pc==0);
    assert(gool_delivery_apply(&d,0,1)==GOOL_VM_BUDGET&&s.vm.send_pending);
    assert(gool_object_get(&t.object,0x26,&value)==0&&value==77);
    assert(gool_delivery_step(&d,1)==GOOL_VM_RUNNING&&!s.vm.send_pending);
    assert(t.vm.pc==0&&t.vm.stack.top==top&&t.vm.frame_token_index==token&&t.vm.frame_yield);
    /* Mapped incoming arguments are borrowed from this stable job. Actual
     * handler writes through ARRAY_STORE; completion releases its handle. */
    setup(&s,&t,objects,scode,tcode,indices,states,constants);
    gool_memory memory;gool_memory_init(&memory);
    assert(gool_vm_bind_memory(&s.vm,&memory)==0&&gool_vm_bind_memory(&t.vm,&memory)==0);
    pack(tcode+8,0x11a09e1f);pack(tcode+12,0x4e800b7f);pack(tcode+16,0x3e800000);
    memset(&d,0,sizeof(d));
    assert(gool_delivery_begin_mapped(&d,&s,&t,&memory,7)==0);
    int32_t span=d.span_pointer;
    assert(gool_memory_read(&memory,(uint32_t)span,&value)==0&&value==77);
    assert(gool_delivery_step(&d,3)==0&&d.span_memory==NULL&&!s.vm.send_pending);
    assert(d.request.arguments[0]==144);
    assert(gool_memory_read(&memory,(uint32_t)span,&value)==GOOL_MEMORY_UNBOUND);
    puts("PASS handler/route/script/span harness; state application also needs archive validation");return 0;
}
