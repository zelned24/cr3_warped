#include "gool_native_object.h"

static int fail(gool_native_object *o,int error){o->status=error;return error;}
static int read_word(gool_native_object *o,uint32_t address,uint32_t *value) {
    return gool_native_registry_read(o->factory->registry,address,value);
}
static int write_word(gool_native_object *o,uint32_t address,uint32_t value) {
    return gool_native_registry_write(o->factory->registry,address,value);
}
static int timeline(gool_native_object *o) {
    if(!o->factory->open||!o->factory->engine.tick||*o->factory->engine.tick!=o->tick)
        return GOOL_NATIVE_OBJECT_TIMELINE;
    uint32_t tick;int error=read_word(o,0x80068eecu,&tick);if(error)return error;
    return tick==(uint32_t)o->tick?0:GOOL_NATIVE_OBJECT_TIMELINE;
}
static int release(gool_native_object *o) {
    if(!o->actor_retained)return 0;
    if(!o->actor||!o->actor->attached||o->actor->enabled||o->actor->executing||
       !o->actor->call_references)return GOOL_NATIVE_ACTOR_BUSY;
    o->actor->call_references--;o->actor_retained=0;return 0;
}
static int finish(gool_native_object *o,int32_t result) {
    int error=timeline(o);if(error)return fail(o,error);
    error=release(o);if(error)return fail(o,error);
    o->native_result=result;o->active=0;o->status=0;return 0;
}
static int category(gool_native_object *o,uint32_t *value) {
    uint32_t behavior,header;int error=read_word(o,o->actor->node+0xc,&behavior);if(error)return error;
    if(behavior>UINT32_MAX-0x10u)return GOOL_NATIVE_OBJECT_INVALID;
    error=read_word(o,behavior+0x10,&header);if(error)return error;
    if(header>UINT32_MAX-4u)return GOOL_NATIVE_OBJECT_INVALID;
    return read_word(o,header+4,value);
}
static int admit(gool_native_object *o) {
    uint32_t flags,mask,status,motion,kind,node=o->actor->node;
    int error=write_word(o,0x80068ee8u,node);if(error)return error;
    error=read_word(o,0x80068f1cu,&flags);if(error)return error;
    mask=flags&8u;o->effective=o->requested;
    if(mask) {
        error=read_word(o,node+0x100,&status);if(error)return error;
        if(status&0x40000u)o->effective=1;
        error=read_word(o,node+0xac,&motion);if(error)return error;
        if(((motion&0x02000000u)||(status&0x8000u))&&(flags&0x8000u))mask=1;
        else {
            error=category(o,&kind);if(error)return error;
            switch(kind) {
                case 0x100:mask=flags&0x20u;break;
                case 0x200:mask=flags&0x100u;break;
                case 0x300:case 0x500:case 0x600:mask=flags&0x80u;break;
                case 0x400:case 0x900:mask=flags&0x400u;break;
                default:break;
            }
        }
    }
    o->admitted=o->effective&&mask;
    if(!o->admitted){o->phase=5;return 0;}
    error=write_word(o,node+0xdc,(uint32_t)o->tick);if(error)return error;
    error=read_word(o,node+0xac,&motion);if(error)return error;
    o->has_old_vectors=(motion&0x10u)!=0;
    if(o->has_old_vectors)for(uint32_t i=0;i<6;i++) {
        error=read_word(o,node+0x60+i*4,&o->old_vectors[i]);if(error)return error;
    }
    error=gool_native_update_take(&o->update,o->actor);
    if(error!=GOOL_NATIVE_UPDATE_BUDGET)return error<0?error:GOOL_NATIVE_OBJECT_INVALID;
    o->actor_retained=0;o->phase=2;return 0;
}
static int presentation(gool_native_object *o,int *allowed) {
    gool_world_presentation_input input={.requested_update=o->effective};
    uint32_t node=o->actor->node;int error;
    *allowed=0;
    error=read_word(o,node+0xe8,&input.model);if(error)return error;
    if(!input.model)return 0;
    uint8_t format;error=gool_domain_read8(o->factory->registry->domain,node+0x120,&format);if(error)return error;
    input.geometry_format=format;if(format==1)return 0;
    error=read_word(o,node+0xac,&input.flags_ac);if(error)return error;
    if(input.flags_ac&0x100u)return 0;
    error=read_word(o,0x80068f1cu,&input.world_flags);if(error)return error;
    if(!(input.world_flags&4u))return 0;
    error=read_word(o,node+0x100,&input.status_100);if(error)return error;
    if(!(((input.flags_ac&0x02000000u)||(input.status_100&0x8000u))&&
         (input.world_flags&0x4000u))) {
        error=category(o,&input.category);if(error)return error;
        if(input.category==0x100&&(input.world_flags&0x10u)) {
            error=read_word(o,0x80069034u,&input.engine_69034);if(error)return error;
            if(!(input.engine_69034&1u)) {
                error=read_word(o,0x8006087cu,&input.engine_6087c);if(error)return error;
                if(!input.engine_6087c){error=read_word(o,node+0x108,&input.mode);if(error)return error;}
            }
        } else if(input.category==0x200&&!input.requested_update&&!(input.status_100&0x40000u)) {
            error=read_word(o,0x80068f28u,&input.engine_68f28);if(error)return error;
        }
    }
    *allowed=gool_world_presentation_admit(input);return 0;
}
/* Original1E7C0..1E800. Other motion/model paths retain the real engine
 * provider; only the concrete no-motion SVTX/null-model path enters here. */
static int native_transform(gool_native_object *o,int *selected) {
    uint32_t motion,model;*selected=0;
    int error=read_word(o,o->actor->node+0xac,&motion);if(error)return error;
    if(motion||!o->actor->registry->models)return 0;
    error=read_word(o,o->actor->node+0xe8,&model);if(error)return error;
    if(model) {
        uint8_t kind;
        error=gool_domain_read8(o->factory->registry->domain,model,&kind);if(error)return error;
        if(kind!=1)return 0;
    }
    *selected=1;o->phase=11;return 0;
}
int gool_native_object_bind(gool_native_object *o,gool_native_factory *factory,
    gool_native_route_frame *frames,uint32_t capacity,uint32_t base,
    const gool_native_object_services *services) {
    if(!o||o->bound||!services||!services->protected_update||!services->transform||
       !services->present||!services->cancel)return GOOL_NATIVE_OBJECT_INVALID;
    gool_native_object_services copy=*services;
    *o=(gool_native_object){.factory=factory,.services=copy};
    int error=gool_native_update_bind(&o->update,factory,frames,capacity,base);
    if(error){*o=(gool_native_object){0};return error;}
    error=gool_native_represent_bind(&o->represent,factory->registry);
    if(error){gool_native_update_close(&o->update);*o=(gool_native_object){0};return error;}
    o->bound=1;return 0;
}
int gool_native_object_begin(gool_native_object *o,gool_native_actor *a,uint32_t requested) {
    if(!o||!o->bound||o->active||o->actor_retained||o->external_stage||o->update.active||
       o->model.active||o->model.actor_retained||o->model.retained[0]||o->model.retained[1]||
       !o->factory->open||!o->factory->engine.tick||!a||!a->attached||a->enabled||a->executing||
       a->error||!a->vm||a->vm->status||a->vm->send_pending||a->call_references||
       a->registry!=o->factory->registry||a->index>=o->factory->count||
       a->tick_source!=o->factory->engine.tick)return GOOL_NATIVE_OBJECT_INVALID;
    gool_native_factory_slot *slot=&o->factory->slots[a->index];
    if(&slot->actor!=a||slot->owner!=o->factory||slot->node!=a->node||slot->request||
       a->registry->actors[a->index]!=a)return GOOL_NATIVE_OBJECT_INVALID;
    uint32_t tick;int error=read_word(o,0x80068eecu,&tick);if(error)return error;
    if(tick!=(uint32_t)*o->factory->engine.tick)return GOOL_NATIVE_OBJECT_TIMELINE;
    o->tick=*o->factory->engine.tick;o->actor=a;o->requested=requested;o->effective=requested;
    o->phase=0;o->written=0;o->has_old_vectors=o->admitted=o->presented=0;
    o->status=GOOL_NATIVE_OBJECT_PENDING;o->active=1;o->actor_retained=1;a->call_references++;
    return o->status;
}
int gool_native_object_step(gool_native_object *o,uint32_t budget) {
    if(!o||!o->bound)return GOOL_NATIVE_OBJECT_INVALID;
    if(!o->active||o->status<0||!budget)return o->status;
    while(budget--) {
        int error=timeline(o);if(error)return fail(o,error);
        uint32_t node=o->actor->node;
        switch(o->phase) {
            case 0: {
                uint32_t protected;
                error=read_word(o,0x80060a90u,&protected);if(error)return fail(o,error);
                o->phase=node==protected?10:1;break;
            }
            case 10:
                o->external_stage=GOOL_NATIVE_OBJECT_PROTECTED;
                error=o->services.protected_update(o->services.context,node);
                if(error<0)return fail(o,error);
                if(error==GOOL_NATIVE_OBJECT_PENDING)break;
                if(error)return fail(o,GOOL_NATIVE_OBJECT_INVALID);
                o->external_stage=0;o->phase=1;break;
            case 1:
                error=admit(o);if(error)return fail(o,error);
                break;
            case 2:
                error=gool_native_update_step(&o->update,1);
                if(error<0)return fail(o,error);
                if(error)break;
                /* Native early returns skip the common current-object clear. */
                if(o->update.early_return)return finish(o,o->update.native_result);
                if(o->actor->call_references||!o->actor->attached||o->actor->enabled||o->actor->executing)
                    return fail(o,GOOL_NATIVE_ACTOR_BUSY);
                o->actor->call_references++;o->actor_retained=1;o->phase=3;break;
            case 3:
                error=gool_native_mode_update(o->factory->registry,node);if(error)return fail(o,error);
                o->phase=4;break;
            case 4: {
                int selected;error=native_transform(o,&selected);if(error)return fail(o,error);
                if(selected)break;
                o->phase=15;break;
            }
            case 15:
                o->external_stage=GOOL_NATIVE_OBJECT_TRANSFORM;
                error=o->services.transform(o->services.context,node,o->old_vectors,
                    o->has_old_vectors,o->representation,&o->written);
                if(error<0)return fail(o,error);
                if(error==GOOL_NATIVE_OBJECT_PENDING)break;
                if(error)return fail(o,GOOL_NATIVE_OBJECT_INVALID);
                o->external_stage=0;
                if(o->written&~3u)return fail(o,GOOL_NATIVE_OBJECT_INVALID);
                o->phase=9;break;
            case 11: {
                uint32_t flags;error=read_word(o,node+0xa8,&flags);if(error)return fail(o,error);
                error=write_word(o,node+0xa8,flags&0xf8ca207eu);if(error)return fail(o,error);
                o->phase=12;break;
            }
            case 12: {
                uint32_t model;error=read_word(o,node+0xe8,&model);if(error)return fail(o,error);
                if(!model) {
                    error=gool_native_actor_set_geometry(o->actor,0);if(error)return fail(o,error);
                    o->phase=14;break;
                }
                error=gool_native_model_prepare_begin(&o->model,o->actor,node+0x78);
                if(error!=GOOL_NATIVE_MODEL_PENDING)return fail(o,error<0?error:GOOL_NATIVE_OBJECT_INVALID);
                o->phase=13;break;
            }
            case 13:
                error=gool_native_model_prepare_step(&o->model,1);
                if(error<0)return fail(o,error);
                if(error)return GOOL_NATIVE_OBJECT_PENDING;
                error=gool_native_model_prepare_close(&o->model);if(error)return fail(o,error);
                o->phase=14;break;
            case 14:
                error=gool_native_represent_run(&o->represent,node,o->representation,&o->written);
                if(error)return fail(o,error);
                error=write_word(o,node+0x58,0);if(error)return fail(o,error);
                o->phase=9;break;
            case 9: {
                uint32_t flags;error=read_word(o,node+0xa8,&flags);if(error)return fail(o,error);
                error=write_word(o,node+0xa8,flags&~0x20u);if(error)return fail(o,error);
                o->phase=6;break;
            }
            case 5:
                error=gool_native_represent_run(&o->represent,node,o->representation,&o->written);
                if(error)return fail(o,error);
                if(o->written&~3u)return fail(o,GOOL_NATIVE_OBJECT_INVALID);
                o->phase=6;break;
            case 6: {
                int allowed;error=presentation(o,&allowed);if(error)return fail(o,error);
                o->phase=allowed?7:8;break;
            }
            case 7:
                o->external_stage=GOOL_NATIVE_OBJECT_PRESENT;
                error=o->services.present(o->services.context,node,o->representation,o->written);
                if(error<0)return fail(o,error);
                if(error==GOOL_NATIVE_OBJECT_PENDING)break;
                if(error)return fail(o,GOOL_NATIVE_OBJECT_INVALID);
                o->external_stage=0;o->presented=1;o->phase=8;break;
            case 8:
                error=write_word(o,0x80068ee8u,0);if(error)return fail(o,error);
                return finish(o,-255);
            default:return fail(o,GOOL_NATIVE_OBJECT_INVALID);
        }
    }
    return GOOL_NATIVE_OBJECT_PENDING;
}
int gool_native_object_close(gool_native_object *o) {
    if(!o)return GOOL_NATIVE_OBJECT_INVALID;
    if(!o->bound)return 0;
    if(o->external_stage) {
        if(!o->actor||!o->actor->attached)return GOOL_NATIVE_ACTOR_BUSY;
        int status=o->services.cancel(o->services.context,o->actor->node,o->external_stage);
        if(status<0)return status;
        if(status==GOOL_NATIVE_OBJECT_PENDING)return GOOL_NATIVE_ACTOR_BUSY;
        if(status)return GOOL_NATIVE_OBJECT_INVALID;
        o->external_stage=0;
    }
    int error=gool_native_model_prepare_close(&o->model);if(error)return error;
    error=gool_native_represent_close(&o->represent);if(error)return error;
    error=gool_native_update_close(&o->update);if(error)return error;
    error=release(o);if(error)return error;
    *o=(gool_native_object){0};return 0;
}
