#include "gool_native_actor.h"
#include "gool_native_geometry.h"
#include "audio_native.h"
#include <limits.h>
#include <string.h>

static int32_t signed_word(uint32_t v){return v<=INT32_MAX?(int32_t)v:-1-(int32_t)(UINT32_MAX-v);}
static uint32_t arithmetic_shift(uint32_t value,unsigned count) {
    return (value>>count)|((value&0x80000000u)?UINT32_MAX<<(32u-count):0);
}
int gool_native_actor_frame_bounds(gool_native_actor *a,uint32_t scale) {
    if(!a||!a->attached||a->enabled||a->executing||a->error||!scale||(scale&3u)||
       scale>UINT32_MAX-8u)return GOOL_NATIVE_ACTOR_BUSY;
    gool_domain *domain=a->registry->domain;uint32_t player;
    int error=gool_domain_read32(domain,0x80060a90u,&player);if(error)return error;
    if(player!=a->node)return 0;
    if(!a->frame_retained||!a->frame_address)return GOOL_NATIVE_ACTOR_INVALID;
    uint32_t auxiliary,frame,original_x,box[6];
    error=gool_domain_read32(domain,a->node+8u,&auxiliary);if(error)return error;
    if(!auxiliary||(auxiliary&3u)||auxiliary>UINT32_MAX-0x44u)return GOOL_NATIVE_ACTOR_INVALID;
    error=gool_domain_read32(domain,auxiliary+0x20u,&frame);if(error)return error;
    if(frame!=a->frame_address||frame>UINT32_MAX-0x40u)return GOOL_NATIVE_ACTOR_INVALID;
    error=gool_domain_read32(domain,scale,&original_x);if(error)return error;
    if(original_x&0x80000000u) {
        error=gool_domain_write32(domain,scale,0u-original_x);if(error)return error;
    }
    for(uint32_t i=0;i<6;i++) {
        uint32_t center,extent;
        error=gool_domain_read32(domain,frame+0x20u+(i%3u)*4u,&center);if(error)return error;
        error=gool_domain_read32(domain,frame+(i<3u?0x2cu:0x38u)+(i%3u)*4u,&extent);if(error)return error;
        box[i]=center+extent;
    }
    for(uint32_t i=0;i<6;i++) {
        uint32_t factor,actual_aux;
        error=gool_domain_read32(domain,scale+(i%3u)*4u,&factor);if(error)return error;
        uint32_t value=arithmetic_shift(arithmetic_shift(box[i],8)*factor,4);
        /* Native rereads auxiliary before each individual result store. */
        error=gool_domain_read32(domain,a->node+8u,&actual_aux);if(error)return error;
        if(actual_aux!=auxiliary)return GOOL_NATIVE_ACTOR_INVALID;
        error=gool_domain_write32(domain,actual_aux+0x30u+i*4u,value);if(error)return error;
    }
    return gool_domain_write32(domain,scale,original_x);
}
int gool_native_actor_select_frames(gool_native_actor *a) {
    if(!a||!a->attached||a->enabled||a->executing||a->error||!a->registry->models)
        return GOOL_NATIVE_ACTOR_BUSY;
    gool_domain *domain=a->registry->domain;model_resources *bank=a->registry->models;
    uint32_t descriptor,word,auxiliary,previous[3];const uint32_t offsets[3]={0x20u,0x24u,0x2cu};
    int error=gool_domain_read32(domain,a->node+0xe8u,&descriptor);if(error)return error;
    error=gool_domain_read32(domain,a->node+0xecu,&word);if(error)return error;
    error=gool_domain_read32(domain,a->node+8u,&auxiliary);if(error)return error;
    if(!auxiliary||(auxiliary&3u)||auxiliary>UINT32_MAX-0x2cu)return GOOL_NATIVE_ACTOR_INVALID;
    for(unsigned i=0;i<3;i++) {
        error=gool_domain_read32(domain,auxiliary+offsets[i],&previous[i]);if(error)return error;
    }
    model_frame_selection selected={0};
    error=model_resources_select_frames(bank,descriptor,word,1,&selected);if(error)return error;
    const uint32_t values[3]={selected.first,selected.second,selected.index};
    unsigned published=0;
    for(;published<3;published++) {
        error=gool_domain_write32(domain,auxiliary+offsets[published],values[published]);if(error)break;
    }
    if(!error&&a->frame_retained)error=model_resources_release(bank,a->frame_lease);
    if(error) {
        int rollback=0;
        while(published) {
            published--;int status=gool_domain_write32(domain,auxiliary+offsets[published],previous[published]);
            if(status&&!rollback)rollback=status;
        }
        int cleanup=model_resources_release(bank,selected.lease);
        return rollback?rollback:cleanup?cleanup:error;
    }
    a->frame_lease=selected.lease;a->frame_address=selected.first;
    a->frame_next_address=selected.second;a->frame_retained=1;return 0;
}
static int bind_geometry(gool_native_actor *a,uint32_t reference,uint32_t expected_type) {
    if(!a||!a->attached||a->enabled||a->executing||a->error||!a->registry->models)
        return GOOL_NATIVE_ACTOR_BUSY;
    model_resources *bank=a->registry->models;uint32_t entry=0,type,auxiliary,previous;
    model_resource_lease lease={0};int error=0;
    if(reference) {
        if(reference&1u)error=model_resources_acquire(bank,reference,&entry,&lease);
        else {entry=reference;error=zdat_resources_acquire_address(bank,reference,&lease);}
        if(error)return error;
        error=gool_domain_read32(a->registry->domain,entry+8u,&type);
        if(!error&&type!=expected_type)error=GOOL_NATIVE_ACTOR_INVALID;
    }
    if(!error)error=gool_domain_read32(a->registry->domain,a->node+8u,&auxiliary);
    if(!error&&(!auxiliary||(auxiliary&3u)||auxiliary>UINT32_MAX-0x28u))error=GOOL_NATIVE_ACTOR_INVALID;
    if(!error)error=gool_domain_read32(a->registry->domain,auxiliary+0x28u,&previous);
    if(!error)error=gool_domain_write32(a->registry->domain,auxiliary+0x28u,entry);
    if(error) {
        int cleanup=reference?model_resources_release(bank,lease):0;return cleanup?cleanup:error;
    }
    if(a->geometry_retained) {
        error=model_resources_release(bank,a->geometry_lease);
        if(error) {
            int rollback=gool_domain_write32(a->registry->domain,auxiliary+0x28u,previous);
            int cleanup=reference?model_resources_release(bank,lease):0;
            return rollback?rollback:cleanup?cleanup:error;
        }
    }
    a->geometry_lease=lease;a->geometry_address=entry;a->geometry_retained=reference!=0;return 0;
}
int gool_native_actor_set_geometry(gool_native_actor *a,uint32_t reference) {
    return bind_geometry(a,reference,2);
}
int gool_native_actor_prepare_svtx(gool_native_actor *a,uint32_t scale) {
    if(!a||!a->attached||a->enabled||a->executing||a->error||!a->registry->models)
        return GOOL_NATIVE_ACTOR_BUSY;
    gool_domain *domain=a->registry->domain;uint32_t descriptor,reference;uint8_t type;
    int error=gool_domain_read32(domain,a->node+0xe8u,&descriptor);if(error)return error;
    if(!descriptor||(descriptor&3u)||descriptor>UINT32_MAX-0x10u)return GOOL_NATIVE_ACTOR_INVALID;
    error=gool_domain_read8(domain,descriptor,&type);if(error)return error;
    if(type!=1)return GOOL_NATIVE_ACTOR_INVALID;
    error=bind_geometry(a,0,2);if(error)return error;
    error=gool_domain_read32(domain,descriptor+0x10u,&reference);if(error)return error;
    error=bind_geometry(a,reference,1);if(error)return error;
    error=gool_native_actor_select_frames(a);if(error)return error;
    error=gool_domain_read32(domain,descriptor+0xcu,&reference);if(error)return error;
    error=bind_geometry(a,reference,2);if(error)return error;
    return gool_native_actor_frame_bounds(a,scale);
}
int gool_native_actor_set_frame(gool_native_actor *a,uint32_t eid,uint32_t index) {
    if(!a||!a->attached||a->enabled||a->executing||a->error||!a->registry->models)
        return GOOL_NATIVE_ACTOR_BUSY;
    model_resources *bank=a->registry->models;uint32_t entry,frame,bytes,type,auxiliary,previous,previous_next;
    model_resource_lease lease={0};
    int error=model_resources_acquire(bank,eid,&entry,&lease);if(error)return error;
    error=gool_domain_read32(a->registry->domain,entry+8u,&type);
    if(!error&&type!=1)error=GOOL_NATIVE_ACTOR_INVALID;
    if(!error)error=model_resources_item(bank,lease,index,&frame,&bytes);
    if(!error)error=gool_domain_read32(a->registry->domain,a->node+8u,&auxiliary);
    if(!error&&(!auxiliary||(auxiliary&3u)||auxiliary>UINT32_MAX-0x24u))error=GOOL_NATIVE_ACTOR_INVALID;
    if(!error)error=gool_domain_read32(a->registry->domain,auxiliary+0x20u,&previous);
    if(!error)error=gool_domain_read32(a->registry->domain,auxiliary+0x24u,&previous_next);
    if(!error)error=gool_domain_write32(a->registry->domain,auxiliary+0x20u,frame);
    if(error){int cleanup=model_resources_release(bank,lease);return cleanup?cleanup:error;}
    error=gool_domain_write32(a->registry->domain,auxiliary+0x24u,frame);
    if(error) {
        int rollback=gool_domain_write32(a->registry->domain,auxiliary+0x20u,previous);
        int cleanup=model_resources_release(bank,lease);return rollback?rollback:cleanup?cleanup:error;
    }
    if(a->frame_retained) {
        error=model_resources_release(bank,a->frame_lease);
        if(error) {
            int rollback=gool_domain_write32(a->registry->domain,auxiliary+0x20u,previous);
            int rollback_next=gool_domain_write32(a->registry->domain,auxiliary+0x24u,previous_next);
            int cleanup=model_resources_release(bank,lease);
            return rollback?rollback:rollback_next?rollback_next:cleanup?cleanup:error;
        }
    }
    a->frame_lease=lease;a->frame_address=frame;a->frame_next_address=frame;a->frame_retained=1;return 0;
}
int gool_native_actor_geometry(gool_native_actor *a,uint32_t instruction,
                               const gool_native_geometry_services *services) {
    if(!a||!a->attached||!a->executing||!a->enabled||a->error||
       !a->vm->stack.depth||a->vm->stack.depth>GOOL_STACK_FRAMES)return GOOL_NATIVE_ACTOR_INVALID;
    gool_stack *stack=&a->vm->stack;gool_domain *domain=a->registry->domain;
    uint32_t fp=stack->frames[stack->depth-1];
    if(stack->top>a->stack_words||fp>a->stack_words-GOOL_FRAME_WORDS)return GOOL_VM_STACK;
    int error=gool_domain_write32(domain,a->node+0xbc,a->stack_address+stack->top*4u);if(error)return error;
    error=gool_domain_write32(domain,a->node+0xc4,a->stack_address+fp*4u);if(error)return error;
    uint32_t operand;int service=gool_native_geometry_operand(a->registry,a->node,instruction,services,&operand);
    if(!service&&((instruction>>20)&15u)==13u) {
        uint32_t signal;service=gool_native_registry_read(a->registry,operand,&signal);
        if(!service&&signal==0xf00u) {
            a->vm->geometry_instruction=instruction;a->vm->geometry_operand=operand;
            service=GOOL_VM_GEOMETRY;
        }
    }
    if(!service)service=gool_native_geometry_resolved(a->registry,a->node,instruction,operand,services);
    /* Wrapper reloads S6, S3 and S4 even though S5/S7 remain cached. */
    uint32_t sp;error=gool_domain_read32(domain,a->node+0xbc,&sp);if(error)return error;
    if(sp<a->stack_address||((sp-a->stack_address)&3u)||
       (uint64_t)sp>(uint64_t)a->stack_address+a->stack_words*4u)return GOOL_VM_STACK;
    stack->top=(sp-a->stack_address)/4u;
    if(service)return service;
    if(stack->top<fp+GOOL_FRAME_WORDS)return GOOL_VM_STACK;
    uint32_t handle;gool_data alternate,constants;
    error=gool_domain_read32(domain,a->node+0x14,&handle);if(error)return error;
    error=gool_native_resources_behavior(a->registry->resources,handle,&alternate);if(error)return error;
    error=gool_domain_read32(domain,a->node+0x10,&handle);if(error)return error;
    error=gool_native_resources_behavior(a->registry->resources,handle,&constants);if(error)return error;
    a->alternate=alternate;a->constant_base=constants;
    return 0;
}
int gool_native_actor_sound(gool_native_actor *a,uint32_t instruction,
                           const struct audio_native_services *services) {
    if(!a||!a->attached||!a->executing||!a->enabled||a->error||
       !a->vm->stack.depth||a->vm->stack.depth>GOOL_STACK_FRAMES)return GOOL_NATIVE_ACTOR_INVALID;
    gool_vm *vm=a->vm;gool_stack *stack=&vm->stack;gool_domain *domain=a->registry->domain;
    uint32_t fp=stack->frames[stack->depth-1];
    if(stack->top>a->stack_words||fp>a->stack_words-GOOL_FRAME_WORDS)return GOOL_VM_STACK;
    int error=gool_domain_write32(domain,a->node+0xbcu,a->stack_address+stack->top*4u);if(error)return error;
    error=gool_domain_write32(domain,a->node+0xc4u,a->stack_address+fp*4u);if(error)return error;
    int service=audio_native_apply(a->registry,a->node,instruction,services);
    uint32_t sp,raw_fp,pc;
    error=gool_domain_read32(domain,a->node+0xbcu,&sp);if(error)return error;
    if(sp<a->stack_address||((sp-a->stack_address)&3u)||
       (uint64_t)sp>(uint64_t)a->stack_address+a->stack_words*4u)return GOOL_VM_STACK;
    stack->top=(sp-a->stack_address)/4u;
    if(service)return service;
    error=gool_domain_read32(domain,a->node+0xc4u,&raw_fp);if(error)return error;
    error=gool_domain_read32(domain,a->node+0xc0u,&pc);if(error)return error;
    if(raw_fp<a->node+0x40u)return GOOL_VM_STACK;
    error=gool_stack_import(stack,stack->origin,a->stack_words,sp-a->node-0x40u,raw_fp-a->node-0x40u);
    if(error)return GOOL_VM_STACK;
    gool_data owner;uint32_t offset;
    if(!gool_vm_resolve(vm,signed_word(pc),&owner,&offset)) {
        vm->code=owner;vm->pc=offset;vm->has_pending_pc=0;
    } else {vm->pending_pc=pc;vm->has_pending_pc=1;}
    return 0;
}
static gool_native_actor *actor_at(gool_native_registry *r,uint32_t address,uint32_t *offset) {
    if(!r||!r->actors)return NULL;
    for(uint32_t i=0;i<r->count;i++) {
        gool_native_actor *a=r->actors[i];
        if(a&&a->attached&&address>=a->node&&address-a->node<=0x138u) {
            *offset=address-a->node;return a;
        }
    }
    return NULL;
}
static int live_register(gool_native_actor *a,uint32_t offset,uint32_t *value) {
    gool_vm *vm=a->vm;
    if(a->flow_dirty)return gool_domain_read32(a->registry->domain,a->node+offset,value);
    if(offset==0xbc) {
        if(vm->stack.top>gool_stack_capacity(&vm->stack))return GOOL_NATIVE_ACTOR_INVALID;
        *value=a->stack_address+vm->stack.top*4;return 0;
    }
    if(offset==0xc4&&vm->stack.depth) {
        if(vm->stack.depth>GOOL_STACK_FRAMES||
           vm->stack.frames[vm->stack.depth-1]>gool_stack_capacity(&vm->stack)-GOOL_FRAME_WORDS||
           vm->stack.top<vm->stack.frames[vm->stack.depth-1]+GOOL_FRAME_WORDS)
            return GOOL_NATIVE_ACTOR_INVALID;
        *value=a->stack_address+vm->stack.frames[vm->stack.depth-1]*4;return 0;
    }
    if(offset==0xc0) {
        if(vm->has_pending_pc){*value=vm->pending_pc;return 0;}
        uint32_t pc=vm->pc+(a->executing?1u:0u);int32_t pointer;
        if(pc<vm->pc||gool_vm_address(&vm->code,pc,&pointer))return GOOL_NATIVE_ACTOR_INVALID;
        *value=(uint32_t)pointer;return 0;
    }
    return gool_domain_read32(a->registry->domain,a->node+offset,value);
}
int gool_native_registry_read(void *context,uint32_t address,uint32_t *value) {
    gool_native_registry *r=context;
    if(!r||!r->domain||!value)return GOOL_NATIVE_ACTOR_INVALID;
    uint32_t offset;gool_native_actor *a=actor_at(r,address,&offset);
    if(a&&(offset==0xbc||offset==0xc0||offset==0xc4))return live_register(a,offset,value);
    return gool_domain_read32(r->domain,address,value);
}
int gool_native_registry_write(void *context,uint32_t address,uint32_t value) {
    gool_native_registry *r=context;
    if(!r||!r->domain)return GOOL_NATIVE_ACTOR_INVALID;
    uint32_t offset;gool_native_actor *a=actor_at(r,address,&offset);
    if(a&&offset==0xf0&&r->zones) {
        uint32_t owner=0,previous;int error;zdat_resource_lease next={0};
        error=gool_domain_read32(r->domain,address,&previous);if(error)return error;
        if(value) {
            if((value&3u)||value>UINT32_MAX-4)return GOOL_ARG_RANGE;
            error=gool_domain_read32(r->domain,value+4,&owner);if(error)return error;
            error=zdat_resources_acquire_address(r->zones,owner,&next);if(error)return error;
        }
        error=gool_domain_write32(r->domain,address,value);
        if(error){if(value)zdat_resources_release(r->zones,next);return error;}
        if(a->database_retained) {
            error=zdat_resources_release(r->zones,a->database_lease);
            if(error) {
                gool_domain_write32(r->domain,address,previous);
                if(value)zdat_resources_release(r->zones,next);
                return error;
            }
        }
        a->database_lease=next;a->database_zone_address=owner;a->database_retained=value!=0;return 0;
    }
    if(a&&offset==0x18&&r->zones) {
        if(a->zone_address==value)return gool_domain_write32(r->domain,address,value);
        zdat_resource_lease next={0};int error;
        /* Native2157C can publish the all-zones FFFFFFFF sentinel. It owns
         * no resource page; preserve the raw word while retiring the old lease. */
        int has_zone=value&&value!=UINT32_MAX;
        if(has_zone){error=zdat_resources_acquire_address(r->zones,value,&next);if(error)return error;}
        error=gool_domain_write32(r->domain,address,value);
        if(error){if(has_zone)zdat_resources_release(r->zones,next);return error;}
        if(a->zone_retained) {
            error=zdat_resources_release(r->zones,a->zone_lease);
            if(error){
                gool_domain_write32(r->domain,address,a->zone_address);
                if(has_zone)zdat_resources_release(r->zones,next);
                return error;
            }
        }
        a->zone_lease=next;a->zone_address=value;a->zone_retained=has_zone;return 0;
    }
    if(a&&(offset==0xbc||offset==0xc0||offset==0xc4)) {
        if(offset==0xbc&&(value<a->stack_address||((value-a->stack_address)&3u)||
                         value-a->stack_address>a->stack_words*4u))return GOOL_ARG_RANGE;
        if(offset==0xc4&&value!=a->node+0x40&&
           (value<a->stack_address||((value-a->stack_address)&3u)||
            value-a->stack_address>(a->stack_words-GOOL_FRAME_WORDS)*4u))return GOOL_ARG_RANGE;
        int error=gool_domain_write32(r->domain,address,value);if(error)return error;
        /* Fast stores never assign S5/S6/S7. Inactive targets load these raw
         * registers at their next real dispatch boundary. */
        if(!a->executing)a->flow_dirty=1;
        return 0;
    }
    int error=gool_domain_write32(r->domain,address,value);
    if(!error&&a&&!a->executing&&(offset==0xc||offset==0x10||offset==0x14))a->flow_dirty=1;
    return error;
}
static int target(gool_native_actor *a,const gool_operand *arg,uint32_t *node) {
    if(arg->kind==GOOL_ARG_VARIABLE){*node=a->node;return 0;}
    if((arg->kind!=GOOL_ARG_INDIRECT&&arg->kind!=GOOL_ARG_HALFWORD)||arg->base>=8)
        return GOOL_ARG_INVALID;
    /* Original fast reader38D34..38D40 loads every link, including0 and5,
     * from self+40+base*4. No player/self substitution is made here. */
    return gool_native_registry_read(a->registry,a->node+0x40+arg->base*4,node);
}
static int member_address(uint32_t node,uint32_t member,uint32_t *address) {
    if(!node||(node&3u)||member>=512||(uint64_t)node+0x40+(uint64_t)member*4>UINT32_MAX)
        return GOOL_ARG_RANGE;
    *address=node+0x40+member*4;return 0;
}
static int operand_address(void *context,const gool_operand *arg,uint32_t scratch,uint32_t *address) {
    gool_native_actor *a=context;
    if(!a||!a->attached||!arg||!address||(scratch!=0x18&&scratch!=0x1c))return GOOL_ARG_INVALID;
    gool_stack *stack=&a->vm->stack;
    switch(arg->kind) {
    case GOOL_ARG_BOOLEAN:*address=(uint32_t)arg->value;return 0;
    case GOOL_ARG_LITERAL: {
        uint32_t slot=0x1f800000u+scratch;
        int error=gool_domain_write32(a->registry->domain,slot,(uint32_t)arg->value);if(error)return error;
        *address=slot;return 0;
    }
    case GOOL_ARG_CONSTANT: {
        const gool_data *owner=arg->base?&a->alternate:&a->constant_base;
        if(arg->value<0||(uint32_t)arg->value>=owner->constant_words||!owner->native_constant_address||
           (uint64_t)owner->native_constant_address+(uint32_t)arg->value*4>UINT32_MAX)return GOOL_ARG_RANGE;
        *address=owner->native_constant_address+(uint32_t)arg->value*4;return 0;
    }
    case GOOL_ARG_FRAME: {
        if(!stack->depth||stack->depth>GOOL_STACK_FRAMES)return GOOL_ARG_RANGE;
        int64_t slot=(int64_t)stack->frames[stack->depth-1]+arg->value;
        if(slot<0||slot>=a->stack_words)return GOOL_ARG_RANGE;
        *address=a->stack_address+(uint32_t)slot*4;return 0;
    }
    case GOOL_ARG_VARIABLE:
        if(arg->value==31) {
            int32_t value;int error=gool_stack_pop(stack,&value);if(error)return GOOL_ARG_RANGE;
            *address=a->stack_address+stack->top*4;return 0;
        }
        if(arg->value<0)return GOOL_ARG_RANGE;
        return member_address(a->node,(uint32_t)arg->value,address);
    case GOOL_ARG_INDIRECT: {
        uint32_t node;int error=target(a,arg,&node);if(error)return error;
        if(!node){*address=0;return 0;}
        if(arg->value<0)return GOOL_ARG_RANGE;
        return member_address(node,(uint32_t)arg->value,address);
    }
    default:return GOOL_ARG_INVALID;
    }
}
static int read_operand(void *context,const gool_operand *arg,int32_t *value) {
    gool_native_actor *a=context;
    if(!a||!a->attached||!arg||!value||arg->value<0)return GOOL_ARG_INVALID;
    uint32_t node;int error=target(a,arg,&node);if(error)return error;
    if(!node&&arg->kind==GOOL_ARG_INDIRECT){*value=0;return 0;}
    if(arg->kind==GOOL_ARG_HALFWORD) {
        if(!node||arg->value>=64||(uint64_t)node+0x20+(uint32_t)arg->value*2>UINT32_MAX)
            return GOOL_ARG_RANGE;
        uint16_t half;error=gool_domain_read16(a->registry->domain,node+0x20+(uint32_t)arg->value*2,&half);
        if(error)return error;
        *value=half<32768?(int32_t)half:(int32_t)half-65536;return 0;
    }
    uint32_t address,word;error=member_address(node,(uint32_t)arg->value,&address);if(error)return error;
    error=gool_native_registry_read(a->registry,address,&word);if(error)return error;
    *value=signed_word(word);return 0;
}
static int write_operand(void *context,const gool_operand *arg,int32_t value) {
    gool_native_actor *a=context;
    if(!a||!a->attached||!arg||arg->value<0)return GOOL_ARG_INVALID;
    uint32_t node;int error=target(a,arg,&node);if(error)return error;
    if(arg->kind==GOOL_ARG_HALFWORD) {
        if(!node||arg->value>=64||(uint64_t)node+0x20+(uint32_t)arg->value*2>UINT32_MAX)
            return GOOL_ARG_RANGE;
        return gool_domain_write16(a->registry->domain,node+0x20+(uint32_t)arg->value*2,(uint16_t)(uint32_t)value);
    }
    uint32_t address;error=member_address(node,(uint32_t)arg->value,&address);if(error)return error;
    uint32_t word=(uint32_t)value;
    if(arg->value==0x2a&&(word>>28)==4) {
        /* Original37CE0..37D08 always uses base behavior item5 via object+C. */
        if(word&3u)return GOOL_ARG_RANGE;
        uint32_t behavior;
        error=gool_domain_read32(a->registry->domain,node+0xc,&behavior);if(error)return error;
        error=gool_native_resources_model(a->registry->resources,behavior,(word&0x0fffffffu)>>2,&word);
        if(error)return error;
    }
    return gool_native_registry_write(a->registry,address,word);
}
static int read_address(void *context,uint32_t address,int32_t *value) {
    gool_native_actor *a=context;if(!a||!a->attached||!value)return GOOL_NATIVE_ACTOR_INVALID;
    uint32_t word;int error=gool_native_registry_read(a->registry,address,&word);if(error)return error;
    *value=signed_word(word);return 0;
}
static int write_address(void *context,uint32_t address,int32_t value) {
    gool_native_actor *a=context;if(!a||!a->attached)return GOOL_NATIVE_ACTOR_INVALID;
    return gool_native_registry_write(a->registry,address,(uint32_t)value);
}
int gool_native_registry_bind(gool_native_registry *r,gool_domain *domain,gool_native_resources *resources,
                                gool_native_actor **actors,uint32_t count) {
    if(!r||r->actors||!domain||!resources||!resources->open||resources->closing||!actors||!count||count>1024)
        return GOOL_NATIVE_ACTOR_INVALID;
    for(uint32_t i=0;i<count;i++)if(actors[i])return GOOL_NATIVE_ACTOR_BUSY;
    *r=(gool_native_registry){.domain=domain,.resources=resources,.actors=actors,.count=count};return 0;
}
int gool_native_registry_can_close(const gool_native_registry *r) {
    if(!r)return GOOL_NATIVE_ACTOR_INVALID;
    if(r->actors)for(uint32_t i=0;i<r->count;i++)if(r->actors[i])return GOOL_NATIVE_ACTOR_BUSY;
    return 0;
}
int gool_native_registry_close(gool_native_registry *r) {
    int error=gool_native_registry_can_close(r);if(error)return error;
    *r=(gool_native_registry){0};return 0;
}
int gool_native_actor_attach(gool_native_actor *a,gool_native_registry *r,uint32_t index,uint32_t node,
                               gool_domain_lease object_region,gool_vm *vm) {
    if(!a||a->attached||!r||!r->actors||!r->domain||!r->resources||!r->resources->open||
       r->resources->closing||index>=r->count||r->actors[index]||!vm||vm->status||
       !node||(node&3u)||!vm->external.base||!vm->external.alternate||
       vm->code.source!=&r->resources->source||vm->external.base->source!=&r->resources->source||
       vm->external.alternate->source!=&r->resources->source||vm->stack.origin<0x100u||vm->stack.depth>GOOL_STACK_FRAMES||
       (vm->stack.origin&3u)||vm->stack.top>gool_stack_capacity(&vm->stack))return GOOL_NATIVE_ACTOR_INVALID;
    uint32_t constant_handle;gool_data constant_base;
    int error=gool_domain_read32(r->domain,node+0x10,&constant_handle);if(error)return error;
    error=gool_native_resources_behavior(r->resources,constant_handle,&constant_base);if(error)return error;
    uint32_t references;error=gool_domain_references(r->domain,object_region,&references);if(error)return error;
    gool_domain_region *parent=&r->domain->regions[object_region.index];
    uint64_t stack_address=(uint64_t)node+0x40+vm->stack.origin;
    if(!parent->mutable_bytes||parent->overlay||node!=parent->address||
       stack_address>UINT32_MAX||
       stack_address<parent->address||stack_address-parent->address>parent->size||
       stack_address-parent->address==parent->size)return GOOL_NATIVE_ACTOR_INVALID;
    uint32_t stack_bytes=parent->size-(uint32_t)(stack_address-parent->address);
    if((stack_bytes&3u)||stack_bytes/4u<GOOL_FRAME_WORDS||stack_bytes/4u>GOOL_STACK_WORDS)
        return GOOL_NATIVE_ACTOR_INVALID;
    uint32_t stack_words=stack_bytes/4u;
    if(vm->stack.top>stack_words)return GOOL_NATIVE_ACTOR_INVALID;
    for(uint32_t i=0;i<vm->stack.depth;i++)
        if(i>=GOOL_STACK_FRAMES||vm->stack.frames[i]>stack_words-GOOL_FRAME_WORDS)
            return GOOL_NATIVE_ACTOR_INVALID;
    for(uint32_t i=0;i<r->count;i++)if(r->actors[i]) {
        const gool_native_actor *other=r->actors[i];
        if((uint64_t)node<(uint64_t)other->stack_address+other->stack_words*4u&&
           (uint64_t)other->node<stack_address+stack_bytes)return GOOL_NATIVE_ACTOR_BUSY;
    }
    uint32_t zone_address=0;zdat_resource_lease zone_lease={0};
    if(r->zones) {
        error=gool_domain_read32(r->domain,node+0x18,&zone_address);if(error)return error;
        if(zone_address&&zone_address!=UINT32_MAX){error=zdat_resources_acquire_address(r->zones,zone_address,&zone_lease);if(error)return error;}
    }
    uint32_t database_address=0,database_owner=0;zdat_resource_lease database_lease={0};
    if(r->zones) {
        error=gool_domain_read32(r->domain,node+0xf0,&database_address);
        if(!error&&database_address) {
            if((database_address&3u)||database_address>UINT32_MAX-4)error=GOOL_ARG_RANGE;
            else error=gool_domain_read32(r->domain,database_address+4,&database_owner);
            if(!error)error=zdat_resources_acquire_address(r->zones,database_owner,&database_lease);
        }
        if(error){if(zone_address&&zone_address!=UINT32_MAX)zdat_resources_release(r->zones,zone_lease);return error;}
    }
    uint64_t generation;error=gool_native_resources_retain(r->resources,&generation);
    if(error){
        if(database_address)zdat_resources_release(r->zones,database_lease);
        if(zone_address&&zone_address!=UINT32_MAX)zdat_resources_release(r->zones,zone_lease);
        return error;
    }
    gool_domain_lease overlay;
    error=gool_domain_map_overlay(r->domain,object_region,(uint32_t)stack_address,vm->stack.words,
                                  stack_bytes,&overlay);
    if(error){
        gool_native_resources_release(r->resources,generation);
        if(database_address)zdat_resources_release(r->zones,database_lease);
        if(zone_address&&zone_address!=UINT32_MAX)zdat_resources_release(r->zones,zone_lease);
        return error;
    }
    error=gool_stack_set_capacity(&vm->stack,stack_words);
    if(error) {
        gool_domain_release(r->domain,overlay);
        gool_native_resources_release(r->resources,generation);
        if(database_address)zdat_resources_release(r->zones,database_lease);
        if(zone_address&&zone_address!=UINT32_MAX)zdat_resources_release(r->zones,zone_lease);
        return error;
    }
    uint8_t *backing=parent->mutable_bytes+(size_t)(stack_address-parent->address);
    for(uint32_t i=vm->stack.top;i<stack_words;i++) {
        const uint8_t *p=backing+i*4;uint32_t word=(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
        vm->stack.words[i]=signed_word(word);
    }
    *a=(gool_native_actor){.registry=r,.vm=vm,.previous_context=vm->external,
        .base=*vm->external.base,.alternate=*vm->external.alternate,.constant_base=constant_base,
        .previous_address_context=vm->address_context,.previous_read=vm->read_address,.previous_write=vm->write_address,
        .stack_lease=overlay,.zone_lease=zone_lease,.zone_address=zone_address,
        .database_lease=database_lease,.database_zone_address=database_owner,.database_retained=database_address!=0,
        .zone_retained=zone_address!=0&&zone_address!=UINT32_MAX,.stack_backing=backing,.resource_generation=generation,.node=node,.index=index,
        .stack_address=(uint32_t)stack_address,.stack_words=stack_words,.attached=1,.enabled=1,.stack_mapped=1,.resource_retained=1};
    r->actors[index]=a;
    vm->external.base=&a->base;vm->external.alternate=&a->alternate;
    vm->external.constant_base=&a->constant_base;vm->external.constant_alternate=&a->alternate;
    vm->external.address=operand_address;
    vm->external.context=a;vm->external.read=read_operand;vm->external.write=write_operand;
    vm->address_context=a;vm->read_address=read_address;vm->write_address=write_address;
    error=gool_native_actor_publish(a);
    if(error){a->error=error;a->enabled=0;return error;}
    return 0;
}
int gool_native_actor_publish(gool_native_actor *a) {
    if(!a||!a->attached||a->executing||a->flow_dirty)return GOOL_NATIVE_ACTOR_INVALID;
    if((uint64_t)a->node+0x40+a->vm->stack.origin!=a->stack_address||
       gool_stack_capacity(&a->vm->stack)!=a->stack_words)return GOOL_NATIVE_ACTOR_INVALID;
    const uint32_t registers[]={0xbc,0xc4,0xc0};
    for(unsigned i=0;i<3;i++) {
        uint32_t value;int error=live_register(a,registers[i],&value);if(error)return error;
        error=gool_domain_write32(a->registry->domain,a->node+registers[i],value);if(error)return error;
    }
    return 0;
}
int gool_native_actor_step(gool_native_actor *a,uint32_t budget) {
    if(!a||!a->attached||!a->enabled||a->executing)return GOOL_NATIVE_ACTOR_INVALID;
    if(a->error)return a->error;
    if(a->flow_dirty) {
        a->enabled=0;int error=gool_native_actor_pull_flow(a);a->enabled=1;
        if(error){a->error=error;a->enabled=0;return error;}
    }
    while(budget--) {
        if(a->vm->has_pending_pc)return GOOL_NATIVE_ACTOR_INVALID;
        int32_t next_pointer;
        if(gool_vm_address(&a->vm->code,a->vm->pc+1,&next_pointer))return GOOL_NATIVE_ACTOR_INVALID;
        int write_error=gool_domain_write32(a->registry->domain,a->node+0xc0,(uint32_t)next_pointer);
        if(write_error)return write_error;
        a->executing=1;int result=gool_vm_step(a->vm);a->executing=0;
        int error=gool_native_actor_publish(a);
        if(error){a->error=error;a->enabled=0;return error;}
        if(result)return result;
    }
    return a->vm->status?a->vm->status:GOOL_VM_BUDGET;
}
int gool_native_actor_start(gool_native_actor *a) {
    if(!a||!a->attached||a->executing||a->enabled||a->error||a->vm->status||!a->stack_mapped)
        return GOOL_NATIVE_ACTOR_INVALID;
    int error=gool_native_actor_pull_flow(a);if(error)return error;
    a->enabled=1;return 0;
}
int gool_native_actor_stop(gool_native_actor *a) {
    if(!a||!a->attached)return GOOL_NATIVE_ACTOR_INVALID;
    if(a->executing)return GOOL_NATIVE_ACTOR_BUSY;
    a->enabled=0;return 0;
}
int gool_native_actor_detach(gool_native_actor *a) {
    if(!a||!a->attached)return GOOL_NATIVE_ACTOR_INVALID;
    if(a->executing||a->enabled||a->call_references)return GOOL_NATIVE_ACTOR_BUSY;
    if(a->index>=a->registry->count||a->registry->actors[a->index]!=a)return GOOL_NATIVE_ACTOR_INVALID;
    int error;
    if(a->stack_mapped) {
        uint32_t references;error=gool_domain_references(a->registry->domain,a->stack_lease,&references);
        if(error)return error;
        if(references!=1)return GOOL_NATIVE_ACTOR_BUSY;
        for(uint32_t i=0;i<a->stack_words;i++)for(unsigned byte=0;byte<4;byte++)
            a->stack_backing[i*4+byte]=(uint8_t)((uint32_t)a->vm->stack.words[i]>>(byte*8));
        error=gool_domain_release(a->registry->domain,a->stack_lease);if(error)return error;
        a->stack_mapped=0;
    }
    if(a->geometry_retained) {
        error=model_resources_release(a->registry->models,a->geometry_lease);if(error)return error;
        a->geometry_retained=0;
    }
    if(a->frame_retained) {
        error=model_resources_release(a->registry->models,a->frame_lease);if(error)return error;
        a->frame_retained=0;
    }
    if(a->database_retained) {
        error=zdat_resources_release(a->registry->zones,a->database_lease);if(error)return error;
        a->database_retained=0;
    }
    if(a->zone_retained) {
        error=zdat_resources_release(a->registry->zones,a->zone_lease);if(error)return error;
        a->zone_retained=0;
    }
    if(a->resource_retained) {
        error=gool_native_resources_release(a->registry->resources,a->resource_generation);
        if(error)return error;
        a->resource_retained=0;
    }
    a->vm->external=a->previous_context;a->vm->address_context=a->previous_address_context;
    a->vm->read_address=a->previous_read;a->vm->write_address=a->previous_write;
    a->registry->actors[a->index]=NULL;
    *a=(gool_native_actor){0};return 0;
}

/* Read raw registers rather than live-register adapters: these are explicit
 * quiescent import boundaries after native frame/state services have run. */
static int load_flow(gool_native_registry *r,uint32_t node,uint32_t bytes,
                     gool_data *base,gool_data *alternate,gool_data *code,
                     uint32_t *origin,uint32_t *capacity,uint32_t *sp,uint32_t *fp,uint32_t *pc) {
    uint32_t base_handle,alternate_handle,pointer;
    if(bytes<0x104u||(uint64_t)node+bytes>UINT32_MAX)return GOOL_NATIVE_ACTOR_INVALID;
    int error=gool_domain_read32(r->domain,node+0xc,&base_handle);if(error)return error;
    error=gool_domain_read32(r->domain,node+0x14,&alternate_handle);if(error)return error;
    error=gool_native_resources_behavior(r->resources,base_handle,base);if(error)return error;
    error=gool_native_resources_behavior(r->resources,alternate_handle,alternate);if(error)return error;
    if(base->stack_start<64||(uint64_t)base->stack_start*4+0x40>bytes)
        return GOOL_NATIVE_ACTOR_INVALID;
    *origin=base->stack_start*4;*capacity=(bytes-0x40-*origin)/4;
    if((bytes&3u)||*capacity<GOOL_FRAME_WORDS||*capacity>GOOL_STACK_WORDS)
        return GOOL_NATIVE_ACTOR_INVALID;
    error=gool_domain_read32(r->domain,node+0xbc,sp);if(error)return error;
    error=gool_domain_read32(r->domain,node+0xc4,fp);if(error)return error;
    error=gool_domain_read32(r->domain,node+0xc0,&pointer);if(error)return error;
    if(*sp<node+0x40||*fp<node+0x40)return GOOL_NATIVE_ACTOR_INVALID;
    *sp-=node+0x40;*fp-=node+0x40;
    if(!r->resources->source.resolve)return GOOL_NATIVE_ACTOR_INVALID;
    error=r->resources->source.resolve(r->resources,pointer,code,pc);if(error)return error;
    if(code->source!=&r->resources->source||*pc>=code->code_words||*pc>=0x4000u)
        return GOOL_NATIVE_ACTOR_INVALID;
    return 0;
}
int gool_native_actor_attach_raw(gool_native_actor *a,gool_native_registry *r,uint32_t index,
                               uint32_t node,gool_domain_lease object_region,gool_vm *vm) {
    if(!a||a->attached||!r||!r->domain||!r->resources||!r->actors||index>=r->count||r->actors[index]||
       !vm||vm->status||vm->stack.depth||vm->stack.top||vm->frame_yield||vm->send_pending||
       !node||(node&3u))return GOOL_NATIVE_ACTOR_INVALID;
    uint32_t references;int error=gool_domain_references(r->domain,object_region,&references);if(error)return error;
    const gool_domain_region *region=&r->domain->regions[object_region.index];
    if(region->address!=node||!region->mutable_bytes||region->overlay||references!=1)
        return GOOL_NATIVE_ACTOR_BUSY;
    gool_data base,alternate,code;uint32_t origin,capacity,sp,fp,pc;
    error=load_flow(r,node,region->size,&base,&alternate,&code,&origin,&capacity,&sp,&fp,&pc);
    if(error)return error;
    /* Preserve the configured VM on invalid serialized frame chains. */
    gool_stack stack={0};
    for(uint32_t i=0;i<capacity;i++) {
        uint32_t word;error=gool_domain_read32(r->domain,node+0x40+origin+i*4,&word);if(error)return error;
        stack.words[i]=signed_word(word);
    }
    error=gool_stack_import(&stack,origin,capacity,sp,fp);if(error)return GOOL_VM_STACK;
    gool_operand_context previous=vm->external;
    gool_data previous_code=vm->code;uint32_t previous_pc=vm->pc;int previous_pending=vm->has_pending_pc;
    gool_stack previous_stack=vm->stack;
    vm->stack=stack;vm->code=code;vm->pc=pc;vm->has_pending_pc=0;
    vm->external.base=&base;vm->external.alternate=&alternate;
    error=gool_native_actor_attach(a,r,index,node,object_region,vm);
    if(!a->attached) {
        vm->external=previous;vm->code=previous_code;vm->pc=previous_pc;vm->has_pending_pc=previous_pending;vm->stack=previous_stack;
    } else {
        /* attach copied the two staged views into the stable actor itself. */
        a->previous_context=previous;
    }
    return error;
}
int gool_native_actor_pull_flow(gool_native_actor *a) {
    if(!a||!a->attached||a->enabled||a->executing||!a->stack_mapped)
        return GOOL_NATIVE_ACTOR_BUSY;
    gool_data base,alternate,code;uint32_t origin,capacity,sp,fp,pc;
    uint32_t bytes=0x40+a->vm->stack.origin+a->stack_words*4;
    int error=load_flow(a->registry,a->node,bytes,&base,&alternate,&code,&origin,&capacity,&sp,&fp,&pc);
    if(error)return error;
    if(origin!=a->vm->stack.origin||capacity!=a->stack_words)return GOOL_NATIVE_ACTOR_INVALID;
    uint32_t constant_handle;gool_data constant_base;
    error=gool_domain_read32(a->registry->domain,a->node+0x10,&constant_handle);if(error)return error;
    error=gool_native_resources_behavior(a->registry->resources,constant_handle,&constant_base);if(error)return error;
    error=gool_stack_import(&a->vm->stack,origin,capacity,sp,fp);if(error)return GOOL_VM_STACK;
    a->base=base;a->alternate=alternate;a->constant_base=constant_base;a->vm->code=code;a->vm->pc=pc;a->vm->has_pending_pc=0;
    a->vm->external.base=&a->base;a->vm->external.alternate=&a->alternate;
    a->vm->external.constant_base=&a->constant_base;a->vm->external.constant_alternate=&a->alternate;
    a->flow_dirty=0;return 0;
}
int gool_native_actor_enter_frame(gool_native_actor *a,uint32_t argc,uint32_t flags) {
    if(!a||!a->attached||a->enabled||a->executing||a->error||!a->stack_mapped)
        return GOOL_NATIVE_ACTOR_BUSY;
    gool_domain *domain=a->registry->domain;
    uint32_t handle,sp,fp,pc;gool_data base;
    int error=gool_domain_read32(domain,a->node+0xc,&handle);if(error)return error;
    error=gool_native_resources_behavior(a->registry->resources,handle,&base);if(error)return error;
    if((uint64_t)base.stack_start*4!=a->vm->stack.origin)return GOOL_NATIVE_ACTOR_INVALID;
    error=gool_domain_read32(domain,a->node+0xbc,&sp);if(error)return error;
    error=gool_domain_read32(domain,a->node+0xc4,&fp);if(error)return error;
    error=gool_domain_read32(domain,a->node+0xc0,&pc);if(error)return error;
    if(sp<a->node+0x40||fp<a->node+0x40)return GOOL_NATIVE_ACTOR_INVALID;
    gool_stack *stack=&a->vm->stack;
    error=gool_stack_import(stack,stack->origin,a->stack_words,sp-a->node-0x40,fp-a->node-0x40);
    if(error)return GOOL_VM_STACK;
    /* State entry may save a null MAIN PC before installing RESUME/PRE_RUN.
     * Frame formation preserves that word without fetching or inventing code. */
    error=gool_stack_enter_flags(stack,argc,signed_word(pc),flags);if(error)return GOOL_VM_STACK;
    a->flow_dirty=1;
    error=gool_domain_write32(domain,a->node+0xc4,a->stack_address+stack->frames[stack->depth-1]*4);
    if(error)return error;
    return gool_domain_write32(domain,a->node+0xbc,a->stack_address+stack->top*4);
}
