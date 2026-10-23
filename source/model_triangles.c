#include "model_triangles.h"
#include "gool_native_render.h"

#define LOAD_VERTEX 0x04000000u
#define USE_OLDEST 0x40000000u
#define NEW_TRIANGLE 0x80000000u

static int fail(model_triangles *o,int error){o->status=error;return error;}
static int overlaps(uintptr_t a,uintptr_t n,uintptr_t b,uintptr_t m) {
    return a<b+m&&b<a+n;
}
static int word(model_triangles *o,uint32_t address,uint32_t *value) {
    return gool_domain_read32(o->bank->domain,address,value);
}
static int command(model_triangles *o,uint32_t offset,uint32_t *value) {
    if(offset>o->command_bytes||o->command_bytes-offset<4u)return MODEL_TRIANGLES_FORMAT;
    return word(o,o->commands+offset,value);
}
static int advance(model_triangles *o,uint32_t flags,uint32_t corner,uint32_t command_offset) {
    uint32_t index=(flags>>16)&255u,vertex;
    if(flags&LOAD_VERTEX) {
        if(!(o->valid[index>>5]&(1u<<(index&31u))))return MODEL_TRIANGLES_FORMAT;
        vertex=o->stored[index];
    } else {
        if(o->next_vertex>=o->vertex_count)return MODEL_TRIANGLES_FORMAT;
        vertex=o->next_vertex++;o->stored[index]=vertex;
        o->valid[index>>5]|=1u<<(index&31u);
    }
    uint32_t attribute=(flags>>9)&127u;
    o->last[corner]=(model_triangle_corner){.vertex=vertex,.attribute=attribute,
        .attribute_flags=flags,.attribute_index=attribute,.vertex_flags=flags,.vertex_command=command_offset,
        .attribute_stored=(flags>>27)&1u};
    return 0;
}
static int emit(model_triangles *o,uint32_t flags) {
    if(o->count>=o->capacity)return MODEL_TRIANGLES_CAPACITY;
    uint32_t uv=flags&255u;
    if(flags&0x100u) {
        if(uv>=o->animation_count)return MODEL_TRIANGLES_FORMAT;
    } else if(uv>o->uv_count)return MODEL_TRIANGLES_FORMAT;
    model_triangle triangle={.flags=flags};
    for(uint32_t i=0;i<3;i++) {
        triangle.corners[i]=o->last[i];
        uint32_t index=o->last[i].attribute;
        triangle.corners[i].attribute_index=index;
        /* A native scratch index is not bounded by TGEO item2 count and is
         * not a raw table color. Preserve the token until the live lookup. */
        if(o->last[i].attribute_stored)continue;
        if(index>=o->attribute_count)return MODEL_TRIANGLES_FORMAT;
        int error=word(o,o->attributes+index*4u,&triangle.corners[i].attribute);if(error)return error;
    }
    o->output[o->count++]=triangle;return 0;
}
int model_triangles_begin(model_triangles *o,const model_vertices *v,
    model_triangle *output,uint32_t capacity) {
    if(!o||o->opened||o->retained||!v||!v->opened||v->active||v->status!=MODEL_VERTICES_DONE||
       !v->geometry_retained||!v->bank||!v->bank->open||v->bank->closing||!output||
       !capacity||capacity>UINT32_MAX/sizeof(*output))return MODEL_TRIANGLES_INVALID;
    model_resources *b=v->bank;
    uintptr_t dst=(uintptr_t)output,size=(uintptr_t)capacity*sizeof(*output),owner=(uintptr_t)o;
    uintptr_t source=(uintptr_t)v,points=(uintptr_t)v->output,point_bytes=(uintptr_t)v->capacity*sizeof(*v->output);
    uintptr_t bank=(uintptr_t)b,domain=(uintptr_t)b->domain,storage=(uintptr_t)b->storage;
    uintptr_t storage_bytes=ZDAT_RESOURCES_SLOTS*ZDAT_RESOURCE_BYTES;
    if(dst>UINTPTR_MAX-size||owner>UINTPTR_MAX-sizeof(*o)||source>UINTPTR_MAX-sizeof(*v)||
       points>UINTPTR_MAX-point_bytes||bank>UINTPTR_MAX-sizeof(*b)||
       domain>UINTPTR_MAX-sizeof(*b->domain)||storage>UINTPTR_MAX-storage_bytes||
       overlaps(dst,size,owner,sizeof(*o))||overlaps(dst,size,source,sizeof(*v))||
       overlaps(dst,size,points,point_bytes)||overlaps(dst,size,bank,sizeof(*b))||
       overlaps(dst,size,domain,sizeof(*b->domain))||overlaps(dst,size,storage,storage_bytes)||
       overlaps(owner,sizeof(*o),source,sizeof(*v))||overlaps(owner,sizeof(*o),points,point_bytes)||
       overlaps(owner,sizeof(*o),bank,sizeof(*b))||overlaps(owner,sizeof(*o),domain,sizeof(*b->domain))||
       overlaps(owner,sizeof(*o),storage,storage_bytes))return MODEL_TRIANGLES_INVALID;
    if(v->geometry_lease.slot>=ZDAT_RESOURCES_SLOTS)return MODEL_TRIANGLES_INVALID;
    zdat_resource_slot *slot=&b->slots[v->geometry_lease.slot];
    if(slot->generation!=v->geometry_lease.generation||!slot->references||
       slot->state!=ZDAT_RESOURCE_READY||slot->address!=v->geometry)return MODEL_TRIANGLES_INVALID;
    *o=(model_triangles){.bank=b,.entry=v->geometry,.output=output,.capacity=capacity,
        .vertex_count=v->count,.opened=1,.active=1,.status=MODEL_TRIANGLES_PENDING,.cursor=4};
    int error=zdat_resources_acquire_address(b,o->entry,&o->lease);if(error)return fail(o,error);
    o->retained=1;
    uint32_t header,bytes,type;
    error=word(o,o->entry+8u,&type);if(error)return fail(o,error);
    if(type!=2)return fail(o,MODEL_TRIANGLES_FORMAT);
    error=model_resources_item(b,o->lease,0,&header,&bytes);if(error)return fail(o,error);
    if(bytes<0x4cu)return fail(o,MODEL_TRIANGLES_FORMAT);
    o->texture_names=header+0xcu;
    error=word(o,header+0x40u,&o->texture_count);if(error)return fail(o,error);
    if(o->texture_count>8u)return fail(o,MODEL_TRIANGLES_FORMAT);
    error=word(o,header+0x34u,&o->uv_count);if(error)return fail(o,error);
    error=word(o,header+0x3cu,&o->attribute_count);if(error)return fail(o,error);
    error=word(o,header+0x44u,&o->declared_triangles);if(error)return fail(o,error);
    error=word(o,header+0x48u,&o->animation_count);if(error)return fail(o,error);
    error=model_resources_item(b,o->lease,1,&o->commands,&o->command_bytes);if(error)return fail(o,error);
    if(o->command_bytes<8u||(o->command_bytes&3u))return fail(o,MODEL_TRIANGLES_FORMAT);
    error=word(o,o->commands,&o->copy_count);if(error)return fail(o,error);
    error=model_resources_item(b,o->lease,2,&o->attributes,&o->attribute_bytes);if(error)return fail(o,error);
    if((uint64_t)o->attribute_count*4u>o->attribute_bytes)return fail(o,MODEL_TRIANGLES_FORMAT);
    error=model_resources_item(b,o->lease,3,&o->uvs,&bytes);if(error)return fail(o,error);
    if((uint64_t)o->uv_count*12u>bytes)return fail(o,MODEL_TRIANGLES_FORMAT);
    if(o->animation_count) {
        error=model_resources_item(b,o->lease,4,&o->animations,&bytes);if(error)return fail(o,error);
        if((uint64_t)o->animation_count*4u>bytes)return fail(o,MODEL_TRIANGLES_FORMAT);
    }
    return o->status;
}
int model_triangles_step(model_triangles *o,uint32_t budget) {
    if(!o||!o->opened)return MODEL_TRIANGLES_INVALID;
    if(!o->active||o->status<0||!budget)return o->status;
    while(budget--) {
        uint32_t flags;int error=command(o,o->cursor,&flags);if(error)return fail(o,error);
        if(flags==UINT32_MAX){o->active=0;o->status=MODEL_TRIANGLES_DONE;return o->status;}
        if(o->cursor==4u||(flags&NEW_TRIANGLE)) {
            uint32_t group[3]={flags,0,0};
            for(uint32_t i=1;i<3;i++) {
                error=command(o,o->cursor+i*4u,&group[i]);if(error)return fail(o,error);
                if(group[i]==UINT32_MAX)return fail(o,MODEL_TRIANGLES_FORMAT);
            }
            for(uint32_t i=0;i<3;i++){error=advance(o,group[i],i,o->cursor+i*4u);if(error)return fail(o,error);}
            error=emit(o,flags);if(error)return fail(o,error);
            o->cursor+=12u;
        } else {
            if(!(flags>>16)) {
                o->last[0].attribute=o->last[1].attribute=(flags>>9)&127u;
                o->last[2].attribute=(flags>>2)&127u;
                for(uint32_t i=0;i<3;i++) {
                    o->last[i].attribute_flags=flags;o->last[i].attribute_index=o->last[i].attribute;
                    o->last[i].attribute_stored=(flags>>(i==2u?1u:0u))&1u;
                }
            } else {
                if(!(flags&USE_OLDEST))o->last[0]=o->last[1];
                o->last[1]=o->last[2];
                error=advance(o,flags,2,o->cursor);if(error)return fail(o,error);
                error=emit(o,flags);if(error)return fail(o,error);
            }
            o->cursor+=4u;
        }
    }
    return MODEL_TRIANGLES_PENDING;
}
int model_triangles_close(model_triangles *o) {
    if(!o)return MODEL_TRIANGLES_INVALID;
    if(o->retained) {
        int error=model_resources_release(o->bank,o->lease);if(error)return error;
        o->retained=0;
    }
    *o=(model_triangles){0};return 0;
}
static int material_ready(const model_triangles *o) {
    if(!o||!o->opened||o->active||o->status!=MODEL_TRIANGLES_DONE||!o->retained||
       !o->bank||!o->bank->open||o->lease.slot>=ZDAT_RESOURCES_SLOTS)
        return MODEL_TRIANGLES_INVALID;
    const zdat_resource_slot *slot=&o->bank->slots[o->lease.slot];
    if(slot->generation!=o->lease.generation||slot->address!=o->entry||!slot->references||
       !slot->mapped||slot->state!=ZDAT_RESOURCE_READY)return MODEL_TRIANGLES_INVALID;
    return 0;
}
int model_material_read(const model_triangles *o,uint32_t index,model_material_uv *output) {
    int error=material_ready(o);if(error)return error;
    if(!output||index>=o->uv_count)return MODEL_TRIANGLES_INVALID;
    model_material_uv next={0};uint32_t address=o->uvs+index*12u;
    gool_domain *d=o->bank->domain;
    error=gool_domain_read16(d,address+2u,&next.clut);if(error)return error;
    error=gool_domain_read16(d,address+6u,&next.texpage);if(error)return error;
    next.texture_index=(next.texpage>>10)&7u;
    uint32_t depth=(next.texpage>>7)&3u;
    if(next.texture_index>=o->texture_count||depth==3u)return MODEL_TRIANGLES_FORMAT;
    next.depth=(uint8_t)(4u<<depth);next.blend=(next.texpage>>5)&3u;
    error=gool_domain_read32(d,o->texture_names+next.texture_index*4u,&next.texture_eid);if(error)return error;
    if(!(next.texture_eid&1u))return MODEL_TRIANGLES_FORMAT;
    next.min_u=next.min_v=255u;
    const uint32_t offsets[4]={0,4,8,10};
    for(uint32_t i=0;i<4;i++) {
        error=gool_domain_read8(d,address+offsets[i],&next.u[i]);if(error)return error;
        error=gool_domain_read8(d,address+offsets[i]+1u,&next.v[i]);if(error)return error;
        if(next.u[i]<next.min_u)next.min_u=next.u[i];
        if(next.u[i]>next.max_u)next.max_u=next.u[i];
        if(next.v[i]<next.min_v)next.min_v=next.v[i];
        if(next.v[i]>next.max_v)next.max_v=next.v[i];
    }
    *output=next;return 0;
}
int model_material_animation_read(const model_triangles *o,uint32_t index,
    model_material_animation *output) {
    int error=material_ready(o);if(error)return error;
    if(!output||index>=o->animation_count)return MODEL_TRIANGLES_INVALID;
    uint32_t flags;
    error=gool_domain_read32(o->bank->domain,o->animations+index*4u,&flags);if(error)return error;
    *output=(model_material_animation){.flags=flags,.start=flags&0x7ffu,
        .offset=(flags>>19)&0x3fu,.frame_shift=(flags>>26)&31u,.count_mask=(flags>>12)&127u,
        .mipmapped=(flags>>31)&1u,.indirect=(flags>>11)&1u};
    return 0;
}
int model_material_resolve(const model_triangles *o,uint32_t triangle,uint32_t scratch,
    model_material_selection *output) {
    int error=material_ready(o);if(error)return error;
    if(!output||triangle>=o->count)return MODEL_TRIANGLES_INVALID;
    uint32_t uv_address,uv_bytes,animation_address=0,animation_bytes=0,clock=0;
    error=model_resources_item(o->bank,o->lease,3,&uv_address,&uv_bytes);if(error)return error;
    uint32_t command=o->output[triangle].flags;
    if(command&0x100u) {
        if(!scratch||(scratch&3u)||scratch>UINT32_MAX-0x1cu)return MODEL_TRIANGLES_INVALID;
        error=gool_domain_read32(o->bank->domain,scratch+0x1cu,&clock);if(error)return error;
        error=model_resources_item(o->bank,o->lease,4,&animation_address,&animation_bytes);if(error)return error;
    }
    gool_native_render_uv resolved;
    error=gool_native_render_uv_command(o->bank->domain,uv_address,uv_bytes,animation_address,
        animation_bytes,command,clock,&resolved);if(error)return error;
    model_material_selection next={.index=UINT32_MAX,.native_address=resolved.address,
        .animation_lookups=resolved.lookups,.textured=resolved.textured};
    if(next.textured) {
        next.index=resolved.index-1u;
        error=model_material_read(o,next.index,&next.material);if(error)return error;
    }
    *output=next;return 0;
}
int model_material_attribute_read(const model_triangles *o,uint32_t triangle,uint32_t corner,
    uint32_t scratch,uint32_t *output) {
    int error=material_ready(o);if(error)return error;
    if(!output||triangle>=o->count||corner>=3u)return MODEL_TRIANGLES_INVALID;
    const model_triangle_corner *source=&o->output[triangle].corners[corner];
    if(source->attribute_stored) {
        if(!scratch||(scratch&3u)||source->attribute_index>=128u||
           (uint64_t)scratch+0x140u+(uint64_t)source->attribute_index*4u+4u>UINT64_C(0x100000000))
            return MODEL_TRIANGLES_INVALID;
        return gool_domain_read32(o->bank->domain,scratch+0x140u+source->attribute_index*4u,output);
    }
    *output=source->attribute;return 0;
}
int model_material_vertex_cache_store(const model_triangles *o,const model_pose *pose,
    uint32_t triangle,uint32_t corner,uint32_t command_offset,uint32_t scratch) {
    int error=material_ready(o);if(error)return error;
    if(!pose||!pose->opened||pose->active||pose->status!=MODEL_POSE_DONE||pose->decoded!=pose->count||
       pose->bank!=o->bank||pose->geometry!=o->entry||pose->geometry_identity.slot!=o->lease.slot||
       pose->geometry_identity.generation!=o->lease.generation||pose->count!=o->vertex_count||
       triangle>=o->count||corner>=3u||!scratch||(scratch&3u))return MODEL_TRIANGLES_INVALID;
    const model_triangle_corner *source=&o->output[triangle].corners[corner];
    if(source->vertex>=pose->count||source->vertex_command!=command_offset)return MODEL_TRIANGLES_INVALID;
    if(source->vertex_flags&LOAD_VERTEX)return 0;
    uint32_t slot=(source->vertex_flags>>16)&255u;
    uint64_t address=(uint64_t)scratch+0x140u+slot*8u;
    if(address+8u>UINT64_C(0x100000000))return MODEL_TRIANGLES_INVALID;
    model_pose_vertex value=pose->output[source->vertex];
    error=gool_domain_write32(o->bank->domain,(uint32_t)address,value.xy);if(error)return error;
    return gool_domain_write32(o->bank->domain,(uint32_t)address+4u,value.z);
}
