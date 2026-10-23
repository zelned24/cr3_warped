#include "model_commands.h"

static int fail(model_commands *o,int error){o->status=error;return error;}
static int command(model_commands *o,uint32_t offset,uint32_t *value) {
    const model_triangles *t=&o->actor->topology;
    if(offset>t->command_bytes||t->command_bytes-offset<4u)return MODEL_COMMANDS_INVALID;
    return gool_domain_read32(t->bank->domain,t->commands+offset,value);
}
int model_commands_begin(model_commands *o,model_actor *a,uint32_t scratch) {
    if(!o||o->opened||o->retained||o->generation==UINT64_MAX||!a||!a->opened||a->active||a->status!=MODEL_ACTOR_DONE||
       !a->retained||a->consumers==UINT32_MAX||!scratch||(scratch&3u)||
       scratch>UINT32_MAX-0x93fu||a->topology.copy_count>a->topology.attribute_bytes/4u||
       a->topology.copy_count>512u)return MODEL_COMMANDS_INVALID;
    uint64_t generation=o->generation+1u;
    *o=(model_commands){.actor=a,.scratch=scratch,.cursor=4u,.tick=a->tick,.generation=generation,
        .opened=1,.retained=1,.status=MODEL_COMMANDS_PENDING};
    a->consumers++;return o->status;
}
static int stable(model_commands *o) {
    model_actor *a=o->actor;
    if(!a||!a->opened||a->active||a->status||!a->consumers||!a->retained||
       !a->actor->attached||a->actor->enabled||a->actor->executing||!a->actor->tick_source||
       *a->actor->tick_source!=o->tick)return MODEL_COMMANDS_INVALID;
    uint32_t tick;int error=gool_domain_read32(a->bank->domain,0x80068eecu,&tick);if(error)return error;
    return tick==(uint32_t)o->tick?0:MODEL_COMMANDS_INVALID;
}
static int vertex(model_commands *o,uint32_t corner,uint32_t offset) {
    const model_triangles *t=&o->actor->topology;
    int error=model_material_vertex_cache_store(t,&o->actor->pose,o->index,corner,offset,o->scratch);
    if(error)return error;
    const model_triangle_corner *source=&t->output[o->index].corners[corner];
    model_pose_vertex value;
    if(source->vertex_flags&0x04000000u) {
        uint32_t address=o->scratch+0x140u+((source->vertex_flags>>16)&255u)*8u;
        error=gool_domain_read32(t->bank->domain,address,&value.xy);if(error)return error;
        error=gool_domain_read32(t->bank->domain,address+4u,&value.z);if(error)return error;
    } else value=o->actor->pose.output[source->vertex];
    o->vertices[corner]=value;
    error=model_material_attribute_read(t,o->index,corner,o->scratch,&o->attributes[corner]);
    if(error)return error;
    return 0;
}
int model_commands_step(model_commands *o,uint32_t budget) {
    if(!o||!o->opened)return MODEL_COMMANDS_INVALID;
    if(o->status!=MODEL_COMMANDS_PENDING||!budget)return o->status;
    while(budget--) {
        int error=stable(o);if(error)return fail(o,error);
        model_triangles *t=&o->actor->topology;gool_domain *d=t->bank->domain;
        if(o->copied<t->copy_count) {
            uint32_t value;error=gool_domain_read32(d,t->attributes+o->copied*4u,&value);if(error)return fail(o,error);
            error=gool_domain_write32(d,o->scratch+0x140u+o->copied*4u,value);if(error)return fail(o,error);
            o->copied++;continue;
        }
        uint32_t flags;error=command(o,o->cursor,&flags);if(error)return fail(o,error);
        if(flags==UINT32_MAX) {
            if(o->index!=t->count)return fail(o,MODEL_COMMANDS_INVALID);
            o->status=MODEL_COMMANDS_DONE;return o->status;
        }
        if(o->cursor!=4u&&!(flags&0x80000000u)&&!(flags>>16)) {
            error=gool_native_render_attribute_command(d,o->scratch,t->attributes,t->attribute_bytes,flags,o->attributes);
            if(error)return fail(o,error);
            o->cursor+=4u;continue;
        }
        if(o->index>=t->count||t->output[o->index].flags!=flags)return fail(o,MODEL_COMMANDS_INVALID);
        model_triangle triangle=t->output[o->index];
        o->starts_triangle=o->cursor==4u||(flags&0x80000000u);
        if(o->starts_triangle) {
            for(uint32_t corner=0;corner<3;corner++) {
                error=vertex(o,corner,o->cursor+corner*4u);if(error)return fail(o,error);
            }
            o->cursor+=12u;
        } else {
            if(!(flags&0x40000000u)) {
                o->attributes[0]=o->attributes[1];o->vertices[0]=o->vertices[1];
            }
            o->attributes[1]=o->attributes[2];
            o->vertices[1]=o->vertices[2];
            error=vertex(o,2,o->cursor);if(error)return fail(o,error);
            o->cursor+=4u;
        }
        for(uint32_t i=0;i<3;i++) {
            triangle.corners[i].attribute=o->attributes[i];triangle.corners[i].attribute_stored=0;
        }
        o->triangle=triangle;o->status=MODEL_COMMANDS_TRIANGLE;return o->status;
    }
    return MODEL_COMMANDS_PENDING;
}
int model_commands_consume(model_commands *o) {
    if(!o||!o->opened||o->handoffs||o->status!=MODEL_COMMANDS_TRIANGLE)return MODEL_COMMANDS_INVALID;
    o->index++;o->status=MODEL_COMMANDS_PENDING;return o->status;
}
int model_commands_close(model_commands *o) {
    if(!o)return MODEL_COMMANDS_INVALID;
    if(o->handoffs)return MODEL_COMMANDS_INVALID;
    if(o->retained) {
        if(!o->actor||!o->actor->opened||!o->actor->consumers)return MODEL_COMMANDS_INVALID;
        o->actor->consumers--;o->retained=0;
    }
    uint64_t generation=o->generation;*o=(model_commands){.generation=generation};return 0;
}
int model_commands_project(const model_commands *o,const model_project_controls *controls,
    uint32_t packet_flags,int16_t zsf3,model_project_triangle *projected,model_visibility *visibility) {
    if(!o||!o->opened||!o->retained||o->status!=MODEL_COMMANDS_TRIANGLE||!o->actor||
       !o->actor->opened||!o->actor->consumers||!controls||!projected||!visibility)
        return MODEL_COMMANDS_INVALID;
    model_project_fifo local={0};model_project_triangle result;
    int error=model_project_rtpt(controls,o->vertices,&local,&result);if(error)return error;
    int16_t x[3],y[3];uint16_t depth[3];
    for(uint32_t i=0;i<3;i++){x[i]=result.vertices[i].x;y[i]=result.vertices[i].y;depth[i]=result.vertices[i].depth;}
    model_visibility admission;
    error=model_visibility_native(o->triangle.flags,packet_flags,x,y,depth,zsf3,&admission);if(error)return error;
    *projected=result;*visibility=admission;return 0;
}
int model_commands_project_stream(const model_commands *o,const model_project_controls *controls,
    uint32_t packet_flags,int16_t zsf3,model_command_projection *p) {
    if(!o||!p||!controls||!o->opened||!o->retained||o->status!=MODEL_COMMANDS_TRIANGLE||
       !o->actor||!o->actor->opened||!o->actor->consumers||p->next_index!=o->index||
       (p->owner&&(p->owner!=o||p->generation!=o->generation)))return MODEL_COMMANDS_INVALID;
    if(p->pending)return MODEL_COMMANDS_TRIANGLE;
    if(!p->owner&&o->index)return MODEL_COMMANDS_INVALID;
    model_command_projection next=*p;next.owner=o;next.generation=o->generation;
    int error;
    if(o->starts_triangle) {
        error=model_project_rtpt(controls,o->vertices,&next.fifo,&next.projected);if(error)return error;
    } else {
        if(!p->owner)return MODEL_COMMANDS_INVALID;
        if(o->triangle.flags&0x40000000u) {
            next.fifo.x[1]=next.fifo.x[0];next.fifo.y[1]=next.fifo.y[0];
            next.fifo.depth[2]=next.fifo.depth[1];
        } else next.projected.vertices[0]=p->projected.vertices[1];
        next.projected.vertices[1]=p->projected.vertices[2];
        error=model_project_rtps_fifo(controls,&o->vertices[2],&next.fifo,&next.projected.vertices[2]);
        if(error)return error;
        next.projected.flags=next.projected.vertices[2].flags;
    }
    error=model_visibility_native(o->triangle.flags,packet_flags,next.fifo.x,next.fifo.y,
        &next.fifo.depth[1],zsf3,&next.visibility);if(error)return error;
    if(next.visibility.area_written)next.fifo.mac0=next.visibility.area;
    if(next.visibility.depth_written)next.fifo.mac0=next.visibility.depth_mac;
    next.pending=1;*p=next;return MODEL_COMMANDS_TRIANGLE;
}
int model_commands_project_accept(model_commands *o,model_command_projection *p) {
    if(!o||!p||p->owner!=o||p->generation!=o->generation||!p->pending||p->next_index!=o->index)
        return MODEL_COMMANDS_INVALID;
    int error=model_commands_consume(o);if(error!=MODEL_COMMANDS_PENDING)return error;
    p->next_index++;p->pending=0;return MODEL_COMMANDS_PENDING;
}
