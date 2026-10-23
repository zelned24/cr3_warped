#include "gool_native_router.h"
#include "gool_native_geometry.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static gool_domain domain;
static gool_native_resources resources;
static gool_native_registry registry;
static gool_native_factory factory;
static gool_native_factory_slot slots[3];
static gool_native_actor *actors[3];
static uint8_t objects[3][GOOL_NATIVE_POOL_STRIDE],behavior[128],code[40],globals[0x400];
static uint32_t roots[10],tick_word;
static uint64_t tick=17;
static gool_native_geometry_services services={.hierarchy_scan_limit=32};
static int calls;
static void wr(uint32_t a,uint32_t v){assert(!gool_domain_write32(&domain,a,v));}
static uint32_t rd(uint32_t a){uint32_t v;assert(!gool_domain_read32(&domain,a,&v));return v;}
static void put(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static int resolve(void *c,uint32_t pointer,gool_data *out,uint32_t *pc) {
    gool_native_resources *r=c;
    if(pointer<0x90010000u||pointer>=0x90010000u+sizeof(code)||(pointer&3u))return -1;
    *out=r->behaviors[0].data;*pc=(pointer-0x90010000u)/4u;return 0;
}
static int geometry(void *c,uint32_t instruction) {
    calls++;return gool_native_actor_geometry(c,instruction,&services);
}
static void map(uint32_t address,void *p,uint32_t size,gool_domain_lease *lease) {
    assert(!gool_domain_map_mutable(&domain,address,p,size,lease));
}
static void setup(void) {
    memset(&domain,0,sizeof(domain));memset(&resources,0,sizeof(resources));
    memset(slots,0,sizeof(slots));memset(objects,0,sizeof(objects));memset(roots,0,sizeof(roots));
    memset(behavior,0,sizeof(behavior));memset(code,0,sizeof(code));
    memset(&factory,0,sizeof(factory));calls=0;tick_word=17;
    gool_domain_lease lease;
    map(0x80068ea0u,roots,sizeof(roots),&lease);
    map(0x80068eecu,&tick_word,4,&lease);
    map(0x90000000u,behavior,sizeof(behavior),&lease);
    map(0x90010000u,code,sizeof(code),&lease);
    map(0x90020000u,globals,sizeof(globals),&lease);
    resources.open=1;resources.count=1;resources.domain=&domain;
    resources.source.context=&resources;resources.source.resolve=resolve;
    gool_data data={.code=code,.code_words=sizeof(code)/4,.stack_start=64,
        .native_code_address=0x90010000u,.source=&resources.source};
    resources.behaviors[0].handle=0x90000000u;resources.behaviors[0].data=data;
    resources.behaviors[0].code_address=0x90010000u;
    registry=(gool_native_registry){.domain=&domain,.resources=&resources,.actors=actors,.count=3};
    factory.open=1;factory.registry=&registry;factory.slots=slots;factory.count=3;
    factory.engine.tick=&tick;factory.engine.geometry=&services;
    wr(0x90000010,0x90000040);wr(0x90000014,0x90010000);wr(0x9000001c,0x90000060);
    wr(0x90000044,0); /* native category0 */
    assert(!gool_domain_write16(&domain,0x9000007e,0x8004));
    put(code,0x1cd08e1fu); /* selector13 mask1, pop original signal */
    put(code+4,0x31000000u);
    put(code+16,0x11a01e1fu); /* probe pushes16 */
    put(code+20,0x1c407e25u); /* link7 array-write caller+D8 */
    put(code+24,0x31000000u);
    for(unsigned i=0;i<3;i++) {
        uint32_t node=GOOL_NATIVE_POOL_BASE+i*GOOL_NATIVE_POOL_STRIDE;
        slots[i].node=node;slots[i].owner=&factory;
        gool_operand_context context={.base=&data,.alternate=&data};
        assert(!gool_vm_init(&slots[i].vm,&data,0,&context,NULL,0));
        assert(!gool_stack_set_capacity(&slots[i].vm.stack,63));
        gool_native_actor *a=&slots[i].actor;
        *a=(gool_native_actor){.registry=&registry,.vm=&slots[i].vm,.base=data,.alternate=data,
            .constant_base=data,.node=node,.index=i,.stack_address=node+0x140u,.stack_words=63,
            .attached=1,.stack_mapped=1,.tick_source=&tick};
        actors[i]=a;a->vm->geometry=geometry;a->vm->geometry_context=a;
        map(node,objects[i],sizeof(objects[i]),&lease);
        assert(!gool_domain_map_overlay(&domain,lease,a->stack_address,a->vm->stack.words,63*4,&a->stack_lease));
        wr(node,1);wr(node+0xc,0x90000000u);wr(node+0x10,0x90000000u);wr(node+0x14,0x90000000u);
        wr(node+0x40,node);wr(node+0xd4,38u<<8);wr(node+0x60,i*256u);
        assert(!gool_native_actor_publish(a));
    }
    wr(0x80068ec0u,2);wr(0x80068ec4u,slots[1].node);
    wr(slots[1].node+0x48,slots[2].node);
    wr(0x80068eb8u,2);wr(0x80068ebcu,0);
}
static unsigned resource_calls,resource_cancels;
static int resource_job(void *context,uint32_t node,uint32_t instruction,int cancel) {
    gool_native_actor *a=context;
    assert(node==a->node&&instruction==0x400a2025u&&!a->enabled);
    assert(a->vm->pc==1&&rd(node+0xc0)==0x90010004u);
    if(cancel){resource_cancels++;return 0;}
    resource_calls++;
    if(resource_calls<4)return 1;
    return gool_native_registry_write(&registry,node+0xd4u,0x83000000u);
}
static void resource_routes(void) {
    for(unsigned cancel=0;cancel<2;cancel++) {
        setup();gool_native_actor *a=&slots[0].actor;
        put(code,0x400a2025u);put(code+4,0x31000000u);
        a->vm->resource_enabled=1;
        factory.engine.resource=resource_job;factory.engine.resource_context=a;
        resource_calls=resource_cancels=0;
        gool_native_dispatch dispatch={0};gool_native_router router={0};
        gool_native_route_frame frames[2]={0};
        assert(gool_native_dispatch_begin(&dispatch,a,4)==1);
        assert(!gool_native_router_bind(&router,&factory,frames,2,0x92000000u));
        assert(gool_native_router_begin(&router,&dispatch)==1);
        unsigned steps=0;
        while(gool_native_router_step(&router,1)==1) {
            assert(++steps<20);
            if(cancel&&resource_calls==2)break;
        }
        if(cancel) {
            assert(dispatch.active&&router.depth==1&&resource_calls==2);
            assert(!gool_native_router_close(&router));
            assert(resource_cancels==1);
            assert(gool_native_dispatch_abort(&dispatch,-77)==-77);
        } else {
            assert(!router.status&&!dispatch.active&&resource_calls==4&&!resource_cancels);
            assert(rd(a->node+0xd4u)==0x83000000u);
            assert(!gool_native_router_close(&router));
        }
        assert(!a->call_references&&!a->enabled);
    }
}
int main(void) {
    resource_routes();
    setup();
    gool_native_actor *a=&slots[0].actor;
    assert(!gool_stack_push(&a->vm->stack,0xf00));assert(!gool_native_actor_publish(a));
    gool_native_dispatch dispatch={0};
    assert(gool_native_dispatch_begin(&dispatch,a,4)==1);
    assert(gool_native_dispatch_step(&dispatch,1)==GOOL_NATIVE_DISPATCH_GEOMETRY);
    assert(a->vm->geometry_pending&&a->vm->pc==1&&a->vm->stack.top==3&&calls==1&&!a->enabled);
    for(unsigned i=0;i<8;i++)assert(gool_vm_step(a->vm)==GOOL_VM_GEOMETRY&&calls==1);
    uint32_t saved_pc=rd(a->node+0xc0),saved_fp=rd(a->node+0xc4);
    gool_native_geometry_search owner={0};
    slots[1].vm.geometry_pending=1;slots[1].vm.geometry_instruction=0x12345678u;
    slots[1].vm.geometry_operand=0x87654321u;
    slots[1].vm.send_pending=1;slots[1].vm.send.raw_signal=0x2700;
    assert(gool_native_geometry_search_begin(&owner,&factory,&dispatch,0x92000000u)==1);
    unsigned steps=0;
    while(gool_native_geometry_search_step(&owner,1)==1)assert(++steps<100);
    assert(owner.status==0&&owner.best==slots[1].node&&!owner.candidate_retained);
    assert(!a->vm->geometry_pending&&a->vm->pc==1&&a->vm->stack.top==4&&a->enabled);
    assert((uint32_t)a->vm->stack.words[3]==slots[1].node&&calls==2);
    assert(rd(a->node+0xc0)==saved_pc&&rd(a->node+0xc4)==saved_fp);
    assert(rd(slots[1].node+0x5c)==a->node&&rd(a->node+0xd8)==16);
    assert(!slots[1].actor.call_references&&!slots[1].actor.enabled);
    assert(slots[1].vm.geometry_pending&&slots[1].vm.geometry_instruction==0x12345678u&&
           slots[1].vm.geometry_operand==0x87654321u);
    assert(slots[1].vm.send_pending&&slots[1].vm.send.raw_signal==0x2700);
    assert(!gool_native_geometry_search_close(&owner));
    uint32_t unmapped;assert(gool_domain_read32(&domain,0x92000000u,&unmapped)==GOOL_DOMAIN_UNBOUND);
    assert(gool_native_dispatch_step(&dispatch,1)==0&&!a->call_references);
    /* Native routing and one-unit traversal execute through the real router. */
    setup();a=&slots[0].actor;
    assert(!gool_stack_push(&a->vm->stack,0xf00));assert(!gool_native_actor_publish(a));
    dispatch=(gool_native_dispatch){0};assert(gool_native_dispatch_begin(&dispatch,a,4)==1);
    gool_native_route_frame frames[4]={0};gool_native_router router={0};
    assert(!gool_native_router_bind(&router,&factory,frames,4,0x92000000u));
    assert(gool_native_router_begin(&router,&dispatch)==1);steps=0;
    while(gool_native_router_step(&router,1)==1)assert(++steps<150);
    assert(!router.status&&!router.depth&&!a->call_references&&calls==2);
    assert((uint32_t)a->vm->stack.words[3]==slots[1].node);
    assert(!gool_native_router_close(&router));
    assert(gool_domain_read32(&domain,0x92000000u,&unmapped)==GOOL_DOMAIN_UNBOUND);
    /* Stop an unfinished child deepest-first, then abort its outer dispatch.
     * A changed timeline fails stickily; cleanup still releases every lease. */
    for(unsigned changed_tick=0;changed_tick<2;changed_tick++) {
        setup();a=&slots[0].actor;
        assert(!gool_stack_push(&a->vm->stack,0xf00));assert(!gool_native_actor_publish(a));
        dispatch=(gool_native_dispatch){0};assert(gool_native_dispatch_begin(&dispatch,a,4)==1);
        memset(frames,0,sizeof(frames));router=(gool_native_router){0};
        assert(!gool_native_router_bind(&router,&factory,frames,4,0x92000000u));
        assert(gool_native_router_begin(&router,&dispatch)==1);steps=0;
        while(!router.depth||!frames[0].geometry.dispatch.active) {
            assert(gool_native_router_step(&router,1)==1);assert(++steps<40);
        }
        assert(slots[1].actor.call_references==2&&frames[0].geometry.argument_mapped);
        if(changed_tick) {
            tick_word++;
            int failed=gool_native_router_step(&router,1);assert(failed<0);
            assert(gool_native_router_step(&router,20)==failed);
        }
        assert(!gool_native_router_close(&router));
        assert(!slots[1].actor.call_references&&!slots[1].actor.enabled);
        assert(gool_domain_read32(&domain,0x92000000u,&unmapped)==GOOL_DOMAIN_UNBOUND);
        assert(gool_native_dispatch_abort(&dispatch,-77)==-77&&!a->call_references);
        assert(!a->vm->geometry_pending&&calls==1&&a->vm->stack.top==3);
    }
    /* Real recursive F00 probes suspend each other. Explicit route capacity
     * stops recursion, and cancellation unwinds their actual shared actors. */
    setup();a=&slots[0].actor;
    put(code+16,0x1cd08e24u);
    wr(slots[1].node+0xd0,0xf00);wr(slots[2].node+0xd0,0xf00);
    assert(!gool_stack_push(&a->vm->stack,0xf00));assert(!gool_native_actor_publish(a));
    dispatch=(gool_native_dispatch){0};assert(gool_native_dispatch_begin(&dispatch,a,4)==1);
    memset(frames,0,sizeof(frames));router=(gool_native_router){0};
    assert(!gool_native_router_bind(&router,&factory,frames,4,0x92000000u));
    assert(gool_native_router_begin(&router,&dispatch)==1);steps=0;
    int recursive_status;
    while((recursive_status=gool_native_router_step(&router,1))==1)assert(++steps<200);
    assert(recursive_status==GOOL_NATIVE_ROUTER_LIMIT&&router.depth==4&&router.overflow);
    assert(!gool_native_router_close(&router));
    assert(!slots[1].actor.call_references&&!slots[2].actor.call_references);
    assert(gool_native_dispatch_abort(&dispatch,-77)==-77&&!a->call_references);
    for(unsigned i=0;i<4;i++)
        assert(gool_domain_read32(&domain,0x92000000u+i*256u,&unmapped)==GOOL_DOMAIN_UNBOUND);
    puts("PASS native F00: actual mode3 probe, bounded tree/router, pending non-replay and cached PC/FP/SP");
    return 0;
}
