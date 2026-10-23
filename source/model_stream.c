#include "model_stream.h"

int model_stream_begin(model_stream *o,model_actor *actor,const model_stream_controls *controls) {
    if(!o||o->opened||o->commands.opened||o->emission.opened||!controls||!actor||
       !actor->opened||((actor->render_word>>16)&255u)>7u||
       (controls->binding_capacity&&!controls->bindings)||
       controls->binding_capacity>UINT32_MAX/sizeof(model_packet_binding)||
       !controls->primitive||!controls->table_begin||
       ((controls->primitive|controls->arena_end|controls->table_begin|controls->table_end)&3u)||
       controls->primitive>controls->arena_end||controls->table_begin>controls->table_end||
       (uint64_t)controls->table_begin+0x24u>controls->table_end||
       ((uint64_t)controls->primitive<controls->table_end&&
        (uint64_t)controls->table_begin<controls->arena_end))return MODEL_STREAM_INVALID;
    model_stream_controls captured=*controls;
    int error=model_commands_begin(&o->commands,actor,captured.scratch);
    if(error!=MODEL_COMMANDS_PENDING)return error;
    o->controls=captured;o->projection=(model_command_projection){.fifo=captured.prior_fifo};
    o->primitive=captured.primitive;o->emitted=o->rejected=o->skipped=0;
    o->binding_count=0;o->binding=(model_packet_binding){0};
    o->color_flags=o->normal_flags=0;o->phase=0;o->opened=1;o->status=MODEL_STREAM_PENDING;
    return o->status;
}
static int current(const model_stream *o) {
    const model_actor *a=o->commands.actor;
    if(!a||!a->opened||!a->retained||a->active||a->status!=MODEL_ACTOR_DONE||
       !a->actor||!a->actor->attached||a->actor->enabled||a->actor->executing||
       !a->actor->tick_source||*a->actor->tick_source!=o->commands.tick)return MODEL_STREAM_INVALID;
    uint32_t tick;int error=gool_domain_read32(a->bank->domain,0x80068eecu,&tick);
    if(error)return error;
    return tick==(uint32_t)o->commands.tick?0:MODEL_STREAM_INVALID;
}
static int emit_begin(model_stream *o) {
    model_stream_controls *c=&o->controls;
    uint32_t mode=(o->commands.actor->render_word>>16)&255u;
    if(mode==3u||mode==4u)return model_emit_begin_normal(&o->emission,&o->commands,&o->projection,
        o->primitive,c->arena_end,c->table_begin,c->table_end,c->depth_base,&c->shading);
    if(mode==6u)return model_emit_begin_depth_pair(&o->emission,&o->commands,&o->projection,
        o->primitive,c->arena_end,c->table_begin,c->table_end,c->depth_f4,c->depth_f8,
        c->shading.packet_flags,c->shading.rgba,c->shading.far_color);
    return model_emit_begin(&o->emission,&o->commands,&o->projection,o->primitive,
        c->arena_end,c->table_begin,c->table_end,c->depth_base,c->shading.packet_flags,
        c->shading.rgba,c->shading.far_color);
}
static int capture_binding(model_stream *o) {
    model_emit *e=&o->emission;o->binding=(model_packet_binding){0};
    if(!e->uv_end||(e->mode==1&&!(e->colors[0]|e->colors[1]|e->colors[2])))return 0;
    model_triangles *t=&o->commands.actor->topology;uint32_t uv;
    if(e->uv_end<8u)return MODEL_STREAM_INVALID;
    int error=gool_domain_read32(t->bank->domain,e->uv_end-8u,&uv);if(error)return error;
    if(e->mode==4&&(uv&0x00600000u)&&(o->commands.triangle.flags&0x10000000u)&&e->backface)return 0;
    if(o->binding_count>=o->controls.binding_capacity)return MODEL_STREAM_BINDINGS;
    uint32_t index=(uv>>26)&7u,eid;
    if(index>=t->texture_count)return MODEL_STREAM_INVALID;
    error=gool_domain_read32(t->bank->domain,t->texture_names+index*4u,&eid);if(error)return error;
    if(!(eid&1u))return MODEL_STREAM_INVALID;
    model_packet_binding binding={.packet=e->primitive,.word=e->mode==7&&e->coefficient?6u:0u,
        .texture_eid=eid};
    if(o->binding_count) {
        const model_packet_binding *last=&o->controls.bindings[o->binding_count-1u];
        if(last->packet>binding.packet||(last->packet==binding.packet&&last->word>=binding.word))
            return MODEL_STREAM_INVALID;
    }
    o->binding=binding;return 0;
}
int model_stream_step(model_stream *o,uint32_t budget) {
    if(!o||!o->opened)return MODEL_STREAM_INVALID;
    if(o->status!=MODEL_STREAM_PENDING)return o->status;
    while(budget--) {
        int error=current(o);if(error)return o->status=error;
        if(o->phase==0) {
            error=model_commands_step(&o->commands,1);
            if(error==MODEL_COMMANDS_DONE)return o->status=MODEL_STREAM_DONE;
            if(error==MODEL_COMMANDS_TRIANGLE)o->phase=1;
            else if(error!=MODEL_COMMANDS_PENDING)return o->status=error;
        } else if(o->phase==1) {
            error=model_commands_project_stream(&o->commands,&o->controls.projection,
                o->controls.shading.packet_flags,o->controls.zsf3,&o->projection);
            if(error!=MODEL_COMMANDS_TRIANGLE)return o->status=error;
            if(o->projection.visibility.admitted)o->phase=2;
            else {o->rejected++;o->phase=4;}
        } else if(o->phase==2) {
            error=emit_begin(o);if(error!=MODEL_EMIT_PENDING)return o->status=error;
            error=capture_binding(o);if(error)return o->status=error;
            o->phase=3;
        } else if(o->phase==3) {
            error=model_emit_step(&o->emission,1);
            if(error==MODEL_EMIT_PENDING)continue;
            if(error!=MODEL_EMIT_DONE)return o->status=error;
            o->primitive=o->emission.next;
            if(o->emission.skipped)o->skipped++;else o->emitted++;
            if(!o->emission.skipped&&o->binding.texture_eid)
                o->controls.bindings[o->binding_count++]=o->binding;
            o->color_flags|=o->emission.color_flags;o->normal_flags|=o->emission.normal_flags;
            error=model_emit_close(&o->emission);if(error)return o->status=error;
            o->phase=4;
        } else if(o->phase==4) {
            error=model_commands_project_accept(&o->commands,&o->projection);
            if(error!=MODEL_COMMANDS_PENDING)return o->status=error;
            o->phase=0;
        } else return o->status=MODEL_STREAM_INVALID;
    }
    return MODEL_STREAM_PENDING;
}
int model_stream_close(model_stream *o) {
    if(!o)return MODEL_STREAM_INVALID;
    int error=model_emit_close(&o->emission);if(error)return error;
    error=model_commands_close(&o->commands);if(error)return error;
    uint64_t generation=o->commands.generation;
    *o=(model_stream){0};o->commands.generation=generation;return 0;
}
