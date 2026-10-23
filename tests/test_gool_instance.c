/* Production instance lifecycle regressions.
 * Optional argv[1]: game_data/runtime/lv_0B.cga for original initialization. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "gool_instance.h"
#ifdef NDEBUG
#error Assertions are required
#endif
static void pack(uint8_t *p,uint32_t n) {
    for(unsigned j=0;j<4;j++)p[j]=(uint8_t)(n>>(j*8));
}
static int32_t member(gool_instance *i,uint32_t n) {
    int32_t value;assert(gool_object_get(&i->object,n,&value)==0);return value;
}
static void synthetic(gool_instance *i,gool_object **objects,const uint8_t *code) {
    memset(i,0,sizeof(*i));objects[0]=&i->object;
    i->context=(gool_object_context){.objects=objects,.count=1,.self=0};
    i->base=(gool_data){.code=code,.code_words=7,.lookup_index=0,.stack_start=512};
    i->alternate=i->base;i->alternate.lookup_index=1;
    gool_operand_context ctx={.base=&i->base,.alternate=&i->alternate,
        .context=&i->context,.read=gool_object_read,.write=gool_object_write};
    assert(gool_vm_init(&i->vm,&i->base,0,&ctx,NULL,0)==0);
    gool_object_reset(&i->object,&i->vm);
    assert(gool_object_set(&i->object,0x26,0)==0);
    assert(gool_object_set(&i->object,0xe,4096)==0);
    assert(gool_object_set(&i->object,0x22,0x50000010)==0);
    i->ready=i->active=1;
}
int main(int argc,char **argv) {
    const uint32_t words[]={0x11e26e25,0x39c30a00,0x320003fd,0x2f000000,
                            0x00a01e26,0x11e1fe26,0x31000000};
    uint8_t code[sizeof(words)];
    for(unsigned j=0;j<7;j++)pack(code+j*4,words[j]);
    gool_instance i;gool_object *objects[1];synthetic(&i,objects,code);
    assert(gool_instance_update(&i,0,100)==GOOL_VM_WAIT);
    assert(member(&i,0x25)==16&&member(&i,0x26)==16);
    assert(gool_instance_update(&i,1,100)==GOOL_VM_WAIT);
    assert(member(&i,0x25)==16&&member(&i,0x26)==32);
    assert(gool_instance_update(&i,1,100)==GOOL_INSTANCE_SKIPPED);
    assert(gool_instance_update(&i,0,100)==GOOL_VM_INVALID);
    /* Live replacement selects alternate owner, not a cached initial handler. */
    assert(gool_object_set(&i.object,0x22,0x50040010)==0);
    assert(gool_instance_update(&i,3,100)==GOOL_VM_WAIT);
    assert(member(&i,0x25)==48&&i.vm.code.lookup_index==0&&i.vm.stack.depth==1);
    assert(gool_object_set(&i.object,0x22,0)==0);
    assert(gool_instance_update(&i,4,100)==GOOL_VM_WAIT&&member(&i,0x26)==48);
    assert(gool_object_set(&i.object,0x22,0x60000000)==0);
    assert(gool_instance_update(&i,5,100)==GOOL_VM_INVALID&&i.vm.tick==4);
    synthetic(&i,objects,code);
    assert(gool_instance_update(&i,5,0)==GOOL_VM_BUDGET&&!i.busy);
    assert(gool_instance_update(&i,5,1)==GOOL_VM_BUDGET);
    assert(gool_instance_update(&i,6,1)==GOOL_VM_INVALID);
    int result=GOOL_VM_BUDGET;
    for(unsigned j=0;j<20&&result==GOOL_VM_BUDGET;j++)result=gool_instance_update(&i,5,1);
    assert(result==GOOL_VM_WAIT&&member(&i,0x26)==16&&i.vm.stack.depth==1);
    if(argc==2) {
        cga_archive archive={0};assert(cga_open(&archive,argv[1],0x0B)==0);
        gool_instance original={0};gool_object *table[13]={0};table[12]=&original.object;
        gool_object_context context={.objects=table,.count=13,.self=12};
        const int32_t params[]={0,0,0};
        gool_placement p={.behavior_eid=0x4ac42e4d,.class_index=34,.object_id=28,
            .path_points=1,.position={12594944,818944,103679744},
            .parameters=params,.parameter_count=3,.tick=17};
        assert(gool_instance_init(&original,&archive,&context,&p)==0);
        assert(original.state==8&&original.object.vm==&original.vm);
        assert(original.vm.external.base==&original.base);
        assert(original.vm.external.alternate==&original.alternate);
        assert(original.vm.external.context==&original.context);
        assert(original.vm.stack.origin==original.base.stack_start*4);
        assert(member(&original,0x1e)==12*256&&member(&original,8)==12594944);
        assert(member(&original,0x27)==-1&&member(&original,0x28)==17);
        assert(gool_instance_init(&original,&archive,&context,&p)==GOOL_VM_INVALID);
        /* An actual original SET_STATE 4 instruction, not substituted code. */
        assert(gool_vm_resume_frame(&original.vm,17)==0);
        original.vm.pc=389;
        assert(gool_object_set(&original.object,0x32,0)==0); /* explicit directed-test provider */
        assert(gool_vm_step(&original.vm)==GOOL_VM_NEW_STATE);
        assert(original.state==4&&original.vm.pc==1762&&original.vm.stack.depth==1);
        assert(member(&original,0x30)==641&&member(&original,0x1c)==0);
        int32_t expected;
        assert(gool_vm_address(&original.base,268,&expected)==0);
        assert(member(&original,0x22)==expected);
        assert(gool_vm_address(&original.base,1762,&expected)==0);
        assert(original.vm.stack.words[1]==expected);
        memset(&original,0,sizeof(original));
        assert(gool_instance_init(&original,&archive,&context,&p)==0);
        uint8_t transition[4];pack(transition,0x35000804); /* state 4, argc 2 */
        assert(gool_vm_resume_frame(&original.vm,17)==0);
        original.vm.code.code=transition;original.vm.code.code_words=1;original.vm.pc=0;
        assert(gool_object_set(&original.object,0x32,0)==0); /* not an initialization default */
        assert(gool_stack_push(&original.vm.stack,77)==0);
        assert(gool_stack_push(&original.vm.stack,-4)==0);
        assert(gool_vm_step(&original.vm)==GOOL_VM_NEW_STATE);
        assert(original.vm.stack.words[0]==77&&original.vm.stack.words[1]==-4);
        assert(original.vm.stack.depth==1&&original.vm.stack.frames[0]==2);
        assert(original.vm.stack.words[3]==expected);
        /* Engine entry accepts external arguments, including a same-ID reset.
         * Caller performs signal compatibility; pre_run is a separate stage. */
        assert(original.state_generation==2);
        const int32_t incoming[]={-123,456};
        assert(gool_instance_enter_state(&original,4,incoming,2)==GOOL_VM_NEW_STATE);
        assert(original.state_generation==3&&original.state==4);
        assert(original.vm.stack.words[0]==-123&&original.vm.stack.words[1]==456);
        assert(original.vm.stack.depth==1&&original.vm.stack.frames[0]==2&&original.vm.frame_yield);
        assert(original.vm.pc==1762&&original.vm.stack.words[3]==expected);
        assert(gool_instance_enter_state(&original,255,NULL,0)==GOOL_VM_INVALID);
        assert(original.state_generation==3);
        memset(&original,0,sizeof(original));
        assert(gool_instance_init(&original,&archive,&context,&p)==0);
        pack(transition,0x36000bff|(0x25u<<14)); /* false: discard 2; invalid target never read */
        assert(gool_vm_resume_frame(&original.vm,17)==0);
        original.vm.code.code=transition;original.vm.code.code_words=1;original.vm.pc=0;
        assert(gool_object_set(&original.object,0x25,0)==0);
        assert(gool_stack_push(&original.vm.stack,77)==0);
        assert(gool_stack_push(&original.vm.stack,-4)==0);
        assert(gool_vm_step(&original.vm)==GOOL_VM_RUNNING);
        assert(original.state==8&&original.vm.pc==1&&gool_stack_height(&original.vm.stack)==0);
        memset(&original,0,sizeof(original));
        assert(gool_instance_init(&original,&archive,&context,&p)==0);
        pack(transition,0x35000414); /* state 20, one argument; original motion has bit 2 */
        assert(gool_vm_resume_frame(&original.vm,17)==0);
        original.vm.code.code=transition;original.vm.code.code_words=1;original.vm.pc=0;
        assert(gool_object_set(&original.object,0x32,3)==0);
        assert(gool_object_set(&original.object,0x1c,0)==0);
        assert(gool_stack_push(&original.vm.stack,77)==0);
        assert(gool_vm_step(&original.vm)==GOOL_VM_RUNNING);
        assert(original.state==8&&gool_stack_height(&original.vm.stack)==0);
        memset(&original,0,sizeof(original));
        assert(gool_instance_init(&original,&archive,&context,&p)==0);
        pack(transition,0x35000404); /* missing mode provider: args consumed, no new root */
        assert(gool_vm_resume_frame(&original.vm,17)==0);
        original.vm.code.code=transition;original.vm.code.code_words=1;original.vm.pc=0;
        assert(gool_stack_push(&original.vm.stack,77)==0);
        assert(gool_vm_step(&original.vm)==GOOL_VM_OPERAND);
        assert(original.state==8&&original.state_generation==1&&gool_stack_height(&original.vm.stack)==0);
        /* Directed original RETURN at BoxsC PC 68 as old RESUME. The one
         * instruction budget queues new pre_run without executing its body. */
        memset(&original,0,sizeof(original));
        assert(gool_instance_init(&original,&archive,&context,&p)==0);
        int32_t resume_pointer;
        assert(gool_vm_address(&original.base,68,&resume_pointer)==0);
        assert(gool_object_set(&original.object,0x24,resume_pointer)==0);
        assert(gool_instance_enter_state(&original,4,incoming,2)==GOOL_VM_NEW_STATE);
        assert(original.has_resume&&!original.vm.frame_yield&&member(&original,0x24)==0);
        assert(gool_instance_update(&original,17,1)==GOOL_VM_BUDGET);
        assert(!original.has_resume&&!original.resume_active&&original.phase==1&&original.vm.pc==268);
        assert(original.vm.frame_yield&&original.vm.stack.words[original.vm.frame_token_index]==0);
        assert(original.vm.stack.words[0]==-123&&original.vm.stack.words[1]==456);
        /* Reinitialize separately for the original initial-state prefix. */
        memset(&original,0,sizeof(original));
        assert(gool_instance_init(&original,&archive,&context,&p)==0);
        int32_t globals[512]={0};globals[0]=11<<8;
        assert(gool_vm_bind_globals(&original.vm,globals,512)==0);
        assert(gool_instance_update(&original,17,100)==GOOL_VM_OPERAND);
        assert(original.vm.pc==1997&&member(&original,0x27)==17);
        cga_close(&archive);
    }
    puts("PASS instance update traces (not world scheduling or physics)");return 0;
}
