#include "model_emit.h"

static int begin(model_emit *o,model_commands *c,const model_command_projection *p,
    uint32_t primitive,uint32_t arena_end,uint32_t table_begin,uint32_t table_end,
    uint32_t depth_base,uint32_t packet_flags,uint32_t rgba,const uint32_t far_color[3],
    const gool_native_shading_controls *normal_controls) {
    if(!o||o->opened||o->retained||!c||!c->opened||c->status!=MODEL_COMMANDS_TRIANGLE||c->handoffs||
       !p||!p->pending||p->owner!=c||p->generation!=c->generation||p->next_index!=c->index||
       !p->visibility.admitted||!p->visibility.depth_written||!c->actor||!c->actor->opened)
        return MODEL_EMIT_INVALID;
    uint32_t mode=(c->actor->render_word>>16)&255u;
    if(mode>7)return MODEL_EMIT_INVALID;
    if((mode==3||mode==4)!=!!normal_controls)return MODEL_EMIT_INVALID;
    model_emit next={.command=c,.generation=c->generation,.index=c->index,.primitive=primitive,
        .arena_end=arena_end,.table_begin=table_begin,.table_end=table_end,.mode=(int)mode,
        .depth_mac=p->visibility.depth_mac,.coefficient=packet_flags&0xffffu,.selected_depth=depth_base};
    int error;
    if(mode==5)next.bucket=table_begin;
    else {
        error=gool_native_primitive_bucket(table_begin,depth_base,p->visibility.depth_mac,&next.bucket);
        if(error)return error;
        model_material_selection selected;
        error=model_material_resolve(&c->actor->topology,c->index,c->scratch,&selected);if(error)return error;
        next.uv_end=(mode==1||mode==3||mode==4||selected.textured)?selected.native_address:0;
    }
    if(normal_controls) {
        uint32_t area,flags;
        error=model_project_nclip(p->fifo.x,p->fifo.y,&area,&flags);if(error)return error;
        next.backface=((area^packet_flags^(c->triangle.flags<<2))&0x80000000u)!=0;
        if(next.backface)next.bucket-=mode==3?8u:40u;
        gool_native_shading_controls lighting=*normal_controls;lighting.backface=next.backface;
        gool_native_shading_result shaded;
        error=gool_native_shading_normal(c->actor->bank->domain,&lighting,c->triangle.corners[0].attribute,&shaded);
        if(error)return error;
        next.colors[0]=shaded.rgb;next.color_flags=shaded.depth_cued.flags;next.normal_flags=shaded.normal_flags;
    }
    uint32_t bytes=mode==1?40u:mode==5?20u:(mode==3||mode==4)?32u:
        mode==7&&next.coefficient?(next.uv_end?64u:60u):(mode==2||next.uv_end)?40u:28u;
    if(!primitive||((primitive|arena_end|table_begin|table_end|next.bucket)&3u)||primitive>arena_end||
       bytes>arena_end-primitive||!table_begin||table_begin>table_end||next.bucket<table_begin||
       (uint64_t)next.bucket+0x24u>table_end||
       ((uint64_t)primitive<table_end&&(uint64_t)table_begin<(uint64_t)primitive+bytes))return MODEL_EMIT_INVALID;
    for(uint32_t i=0;i<3;i++) {
        next.sxy[i]=(uint16_t)p->fifo.x[i]|((uint32_t)(uint16_t)p->fifo.y[i]<<16);
        if(!normal_controls)next.colors[i]=c->triangle.corners[i].attribute;
    }
    if(mode==1)for(uint32_t i=0;i<3;i++) {
        uint32_t depth=p->fifo.depth[i+1u];
        if(depth<=0x1400u)next.fade_stores|=1u<<i;
        if(depth<=0xc00u)next.colors[i]=0;
        else if(depth<=0x1400u) {
            if(!far_color)return MODEL_EMIT_INVALID;
            gool_native_shading_color color;
            error=gool_native_shading_depthcue(next.colors[i],far_color,
                (uint16_t)((0x1400u-depth)<<1),&color);if(error)return error;
            next.colors[i]=color.rgba;next.color_flags|=color.flags;
        }
    }
    if((mode==0||mode==2||mode==6||mode==7)&&(packet_flags&0xffffu)&&
       (mode==7||!(c->triangle.flags&0x01000000u))) {
        if(!far_color)return MODEL_EMIT_INVALID;
        for(uint32_t i=0;i<3;i++) {
            gool_native_shading_color color;
            error=gool_native_shading_depthcue((next.colors[i]&0xffffffu)|(rgba&0xff000000u),far_color,
                (uint16_t)packet_flags,&color);if(error)return error;
            next.colors[i]=color.rgba;next.color_flags|=color.flags;
        }
    }
    next.opened=next.retained=1;next.status=MODEL_EMIT_PENDING;
    *o=next;c->handoffs++;return o->status;
}
int model_emit_begin(model_emit *o,model_commands *c,const model_command_projection *p,
    uint32_t primitive,uint32_t arena_end,uint32_t table_begin,uint32_t table_end,
    uint32_t depth_base,uint32_t packet_flags,uint32_t rgba,const uint32_t far_color[3]) {
    if(c&&c->actor&&((c->actor->render_word>>16)&255u)==6u)return MODEL_EMIT_INVALID;
    return begin(o,c,p,primitive,arena_end,table_begin,table_end,depth_base,packet_flags,rgba,far_color,NULL);
}
int model_emit_begin_depth_pair(model_emit *o,model_commands *c,const model_command_projection *p,
    uint32_t primitive,uint32_t arena_end,uint32_t table_begin,uint32_t table_end,
    uint32_t f4,uint32_t f8,uint32_t packet_flags,uint32_t rgba,const uint32_t far_color[3]) {
    if(!c||!c->actor||((c->actor->render_word>>16)&255u)!=6u)return MODEL_EMIT_INVALID;
    uint32_t shifted=c->triangle.flags<<6;
    uint32_t selected=shifted&&!(shifted&0x80000000u)?f4:f8;
    return begin(o,c,p,primitive,arena_end,table_begin,table_end,selected,packet_flags,rgba,far_color,NULL);
}
int model_emit_begin_normal(model_emit *o,model_commands *c,const model_command_projection *p,
    uint32_t primitive,uint32_t arena_end,uint32_t table_begin,uint32_t table_end,
    uint32_t depth_base,const gool_native_shading_controls *controls) {
    if(!controls)return MODEL_EMIT_INVALID;
    return begin(o,c,p,primitive,arena_end,table_begin,table_end,depth_base,controls->packet_flags,
        controls->rgba,controls->far_color,controls);
}
int model_emit_step(model_emit *o,uint32_t budget) {
    if(!o||!o->opened)return MODEL_EMIT_INVALID;
    if(o->status!=MODEL_EMIT_PENDING||!budget)return o->status;
    while(budget--) {
        model_commands *c=o->command;
        if(!c||!c->opened||c->generation!=o->generation||c->index!=o->index||
           c->status!=MODEL_COMMANDS_TRIANGLE||!c->handoffs)return o->status=MODEL_EMIT_INVALID;
        gool_domain *d=c->actor->bank->domain;int error;
        if(o->phase==0) {
            /* Keep projection MAC0 in its original scratch output slot. */
            error=gool_domain_write32(d,c->scratch+0x120u,o->depth_mac);
            if(error)return o->status=error;
            o->phase=1;
        } else if(o->phase==1) {
            if(o->mode==1) {
                for(uint32_t i=0;i<3;i++) {
                    error=gool_domain_write32(d,c->scratch+0x124u+i*4u,c->triangle.corners[i].attribute);
                    if(error)return o->status=error;
                }
                for(uint32_t i=0;i<3;i++) {
                    if(!(o->fade_stores&(1u<<i)))continue;
                    error=gool_domain_write32(d,c->scratch+0x124u+i*4u,o->colors[i]);
                    if(error)return o->status=error;
                }
                if(!(o->colors[0]|o->colors[1]|o->colors[2])) {
                    o->skipped=1;o->next=o->primitive;o->status=0;return 0;
                }
                error=gool_native_primitive_rgb1(d,o->primitive,o->arena_end,o->sxy,o->colors,o->uv_end,&o->size);
                if(error)return o->status=error;
                o->phase=2;continue;
            }
            if(o->mode==6) {
                error=gool_domain_write32(d,c->scratch+0xf0u,o->selected_depth);if(error)return o->status=error;
            }
            if(o->mode==7&&o->coefficient) {
                error=gool_native_primitive_rgb7(d,o->primitive,o->arena_end,o->sxy,o->colors,o->uv_end,
                    (uint16_t)o->coefficient,&o->size);if(error)return o->status=error;
                o->phase=2;continue;
            }
            if(o->mode==3||o->mode==4) {
                gool_native_normal_primitive result;
                error=gool_native_primitive_normal(d,o->primitive,o->arena_end,(uint32_t)o->mode,
                    c->triangle.flags,o->backface,o->uv_end,o->sxy,o->colors[0],&result);
                if(error)return o->status=error;
                if(result.skipped){o->skipped=1;o->next=o->primitive;o->status=0;return 0;}
                o->size=result.size;o->phase=2;continue;
            }
            if(o->mode==2) {
                error=gool_native_primitive_rgb2(d,o->primitive,o->arena_end,o->bucket,o->table_begin,
                    o->table_end,o->sxy,o->colors,o->uv_end,&o->next);
                if(error)return o->status=error;
                o->status=0;return 0;
            }
            if(o->mode==5)error=gool_native_primitive_constant(d,o->primitive,o->arena_end,o->sxy,&o->size);
            else error=gool_native_primitive_rgb(d,o->primitive,o->arena_end,o->sxy,o->colors,o->uv_end,&o->size);
            if(error)return o->status=error;
            o->phase=2;
        } else if(o->phase==2) {
            error=gool_native_primitive_link(d,o->primitive,o->arena_end,o->bucket,o->table_begin,
                o->table_end,o->size,&o->next);if(error)return o->status=error;
            o->status=0;return 0;
        } else return o->status=MODEL_EMIT_INVALID;
    }
    return MODEL_EMIT_PENDING;
}
int model_emit_close(model_emit *o) {
    if(!o)return MODEL_EMIT_INVALID;
    if(o->retained) {
        if(!o->command||!o->command->opened||o->command->generation!=o->generation||!o->command->handoffs)
            return MODEL_EMIT_INVALID;
        o->command->handoffs--;o->retained=0;
    }
    *o=(model_emit){0};return 0;
}
