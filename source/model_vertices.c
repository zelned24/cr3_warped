#include "model_vertices.h"
#include "gool_native_actor.h"

static int fail(model_vertices *o,int error){o->status=error;return error;}
static int overlap(uintptr_t a,uintptr_t size,uintptr_t b,uintptr_t other) {
    return a<b+other&&b<a+size;
}
static int word(model_vertices *o,uint32_t address,uint32_t *value) {
    return gool_domain_read32(o->bank->domain,address,value);
}
static int frame_byte(model_vertices *o,uint32_t offset,uint8_t *value) {
    if(offset>=o->frame_bytes)return MODEL_VERTICES_FORMAT;
    return gool_domain_read8(o->bank->domain,o->frame+offset,value);
}
static int signed_bits(model_vertices *o,uint32_t count,int32_t *value) {
    /* Counts1..8 ensure refill never shifts by a negative count or32. The
     * format reads high then low halfwords of each LE32 via offset XOR2. */
    while(o->bits<count) {
        uint32_t offset=o->cursor^2u;uint8_t low,high;
        int error=frame_byte(o,offset,&low);if(error)return error;
        error=frame_byte(o,offset+1u,&high);if(error)return error;
        o->sink|=((uint32_t)low|((uint32_t)high<<8))<<(16u-o->bits);
        o->bits+=16u;o->cursor+=2u;
    }
    uint32_t raw=o->sink>>(32u-count);
    o->sink<<=count;o->bits-=count;
    *value=(int32_t)raw-((raw&(1u<<(count-1u)))?(int32_t)(1u<<count):0);
    return 0;
}
int model_vertices_begin(model_vertices *o,model_resources *b,uint32_t svtx,
    uint32_t frame,uint32_t tgeo,model_vertex_bytes *output,uint32_t capacity) {
    if(!o||o->opened||o->frame_retained||o->geometry_retained||!b||!b->open||b->closing||
       !output||!capacity||capacity>UINT32_MAX/sizeof(*output)||!svtx||!tgeo||
       (svtx&3u)||(tgeo&3u)||(frame&3u))return MODEL_VERTICES_INVALID;
    uintptr_t dst=(uintptr_t)output,bytes=(uintptr_t)capacity*sizeof(*output);
    uintptr_t owner=(uintptr_t)o,bank=(uintptr_t)b,storage=(uintptr_t)b->storage;
    uintptr_t domain=(uintptr_t)b->domain;
    uintptr_t storage_bytes=ZDAT_RESOURCES_SLOTS*ZDAT_RESOURCE_BYTES;
    if(dst>UINTPTR_MAX-bytes||owner>UINTPTR_MAX-sizeof(*o)||bank>UINTPTR_MAX-sizeof(*b)||
       domain>UINTPTR_MAX-sizeof(*b->domain)||storage>UINTPTR_MAX-storage_bytes||
       overlap(dst,bytes,owner,sizeof(*o))||overlap(dst,bytes,bank,sizeof(*b))||
       overlap(dst,bytes,domain,sizeof(*b->domain))||overlap(dst,bytes,storage,storage_bytes)||
       overlap(owner,sizeof(*o),bank,sizeof(*b))||overlap(owner,sizeof(*o),domain,sizeof(*b->domain))||
       overlap(owner,sizeof(*o),storage,storage_bytes))
        return MODEL_VERTICES_INVALID;
    *o=(model_vertices){.bank=b,.output=output,.capacity=capacity,.frame=frame,
        .geometry=tgeo,.opened=1,.active=1,.status=MODEL_VERTICES_PENDING};
    int error=zdat_resources_acquire_address(b,svtx,&o->frame_lease);if(error)return fail(o,error);
    o->frame_retained=1;
    error=zdat_resources_acquire_address(b,tgeo,&o->geometry_lease);if(error)return fail(o,error);
    o->geometry_retained=1;
    uint32_t type,items;
    error=word(o,svtx+8u,&type);if(error)return fail(o,error);
    if(type!=1)return fail(o,MODEL_VERTICES_FORMAT);
    error=word(o,tgeo+8u,&type);if(error)return fail(o,error);
    if(type!=2)return fail(o,MODEL_VERTICES_FORMAT);
    error=word(o,svtx+12u,&items);if(error)return fail(o,error);
    int found=0;
    for(uint32_t i=0;i<items;i++) {
        uint32_t address,size;
        error=model_resources_item(b,o->frame_lease,i,&address,&size);if(error)return fail(o,error);
        if(address==frame){o->frame_bytes=size;found=1;break;}
    }
    if(!found||o->frame_bytes<0x1cu)return fail(o,MODEL_VERTICES_FORMAT);
    uint32_t header,header_bytes;
    error=model_resources_item(b,o->geometry_lease,0,&header,&header_bytes);if(error)return fail(o,error);
    if(header_bytes<0x4cu)return fail(o,MODEL_VERTICES_FORMAT);
    for(uint32_t i=0;i<3;i++) {
        uint16_t raw;
        error=gool_domain_read16(b->domain,frame+i*2u,&raw);if(error)return fail(o,error);
        o->origin[i]=raw<=INT16_MAX?(int16_t)raw:(int16_t)(-1-(int32_t)(UINT16_MAX-raw));
        error=word(o,header+i*4u,&o->scale[i]);if(error)return fail(o,error);
    }
    /* Native3F70C averages the full word at+4, including the fourth
     * header halfword; preserving only signed origin.z loses native bits. */
    error=word(o,frame+4u,&o->origin_z_word);if(error)return fail(o,error);
    for(uint32_t i=0;i<2;i++) {
        error=word(o,frame+8u+i*4u,&o->view_offset_words[i]);if(error)return fail(o,error);
    }
    error=word(o,frame+0x10u,&o->count);if(error)return fail(o,error);
    error=word(o,frame+0x18u,&o->cursor);if(error)return fail(o,error);
    uint32_t hitboxes;
    error=word(o,frame+0x14u,&hitboxes);if(error)return fail(o,error);
    if((uint64_t)0x1cu+(uint64_t)hitboxes*0x28u>o->frame_bytes||
       o->cursor<0x1cu||o->cursor>o->frame_bytes)return fail(o,MODEL_VERTICES_FORMAT);
    error=word(o,tgeo+12u,&items);if(error)return fail(o,error);
    if(items>5u) {
        error=model_resources_item(b,o->geometry_lease,5,&o->compression,&o->compression_bytes);
        if(error)return fail(o,error);
        if(o->compression_bytes&3u)return fail(o,MODEL_VERTICES_FORMAT);
        /* Instruction count is authoritative; the nominal SVTX count can
         * differ. Do not substitute TGEO+38's known ambiguous count. */
        if(o->compression_bytes)o->count=o->compression_bytes/4u;
    }
    if(o->count>capacity)return fail(o,MODEL_VERTICES_INVALID);
    if(!o->compression_bytes&&(uint64_t)o->cursor+(uint64_t)o->count*3u>o->frame_bytes)
        return fail(o,MODEL_VERTICES_FORMAT);
    return o->status;
}
int model_vertices_begin_actor(model_vertices *o,gool_native_actor *a,uint32_t member,
    model_vertex_bytes *output,uint32_t capacity) {
    if(!a||!a->attached||a->enabled||a->executing||a->error||member>1u||
       !a->frame_retained||!a->geometry_retained||!a->registry->models)
        return MODEL_VERTICES_INVALID;
    model_resources *b=a->registry->models;
    if(a->frame_lease.slot>=ZDAT_RESOURCES_SLOTS||a->geometry_lease.slot>=ZDAT_RESOURCES_SLOTS)
        return MODEL_VERTICES_INVALID;
    zdat_resource_slot *frames=&b->slots[a->frame_lease.slot],*geometry=&b->slots[a->geometry_lease.slot];
    if(frames->generation!=a->frame_lease.generation||geometry->generation!=a->geometry_lease.generation||
       !frames->references||!geometry->references||frames->state!=ZDAT_RESOURCE_READY||
       geometry->state!=ZDAT_RESOURCE_READY||geometry->address!=a->geometry_address)
        return MODEL_VERTICES_INVALID;
    uint32_t auxiliary,frame,tgeo;int error=gool_domain_read32(b->domain,a->node+8u,&auxiliary);
    if(error)return error;
    if(!auxiliary||(auxiliary&3u)||auxiliary>UINT32_MAX-0x28u)return MODEL_VERTICES_INVALID;
    error=gool_domain_read32(b->domain,auxiliary+0x20u+member*4u,&frame);if(error)return error;
    error=gool_domain_read32(b->domain,auxiliary+0x28u,&tgeo);if(error)return error;
    if(frame!=(member?a->frame_next_address:a->frame_address)||tgeo!=a->geometry_address)
        return MODEL_VERTICES_INVALID;
    return model_vertices_begin(o,b,frames->address,frame,tgeo,output,capacity);
}
int model_vertices_step(model_vertices *o,uint32_t budget) {
    if(!o||!o->opened)return MODEL_VERTICES_INVALID;
    if(!o->active||o->status<0||!budget)return o->status;
    while(budget--&&o->decoded<o->count) {
        uint8_t xyz[3];int error;
        if(o->compression_bytes) {
            uint32_t instruction;
            error=word(o,o->compression+o->decoded*4u,&instruction);if(error)return fail(o,error);
            const uint32_t offsets[3]={(instruction>>24)&0xfeu,(instruction>>17)&0xffu,
                                        (instruction>>9)&0xffu};
            for(uint32_t axis=0;axis<3;axis++) {
                uint32_t count=1u+((instruction>>(6u-axis*3u))&7u);int32_t delta;
                error=signed_bits(o,count,&delta);if(error)return fail(o,error);
                xyz[axis]=(uint8_t)(count==8u?(uint32_t)delta:
                    (uint32_t)o->previous[axis]+(uint32_t)delta+offsets[axis]);
            }
        } else {
            error=frame_byte(o,o->cursor,&xyz[0]);if(error)return fail(o,error);
            error=frame_byte(o,o->cursor+2u,&xyz[1]);if(error)return fail(o,error);
            error=frame_byte(o,o->cursor+1u,&xyz[2]);if(error)return fail(o,error);
            o->cursor+=3u;
        }
        o->output[o->decoded]=(model_vertex_bytes){xyz[0],xyz[1],xyz[2],0};
        for(uint32_t axis=0;axis<3;axis++)o->previous[axis]=xyz[axis];
        o->decoded++;
    }
    if(o->decoded==o->count){o->active=0;o->status=MODEL_VERTICES_DONE;}
    return o->status;
}
int model_vertices_close(model_vertices *o) {
    if(!o)return MODEL_VERTICES_INVALID;
    if(o->geometry_retained) {
        int error=model_resources_release(o->bank,o->geometry_lease);if(error)return error;
        o->geometry_retained=0;
    }
    if(o->frame_retained) {
        int error=model_resources_release(o->bank,o->frame_lease);if(error)return error;
        o->frame_retained=0;
    }
    *o=(model_vertices){0};return 0;
}
