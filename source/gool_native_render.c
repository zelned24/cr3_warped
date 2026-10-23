#include "gool_native_render.h"
#include "gool_trig.h"

static int32_t signed_word(uint32_t v){return v<=INT32_MAX?(int32_t)v:-1-(int32_t)(UINT32_MAX-v);}
static int32_t half(uint32_t v){v&=0xffffu;return v<=INT16_MAX?(int32_t)v:(int32_t)v-65536;}
static uint32_t sra(uint32_t v,uint32_t n){return (v>>n)|((v&0x80000000u)?UINT32_MAX<<(32u-n):0);}
static int32_t ir(int64_t value) {
    int64_t shifted=value<0?-1-((-1-value)/4096):value/4096;
    return shifted>32767?32767:shifted<-32768?-32768:(int32_t)shifted;
}
static void yaw(uint32_t angle,uint32_t matrix[5]) {
    uint32_t sine=(uint32_t)gool_trig_sin(signed_word(angle)),cosine=(uint32_t)gool_trig_cos(signed_word(angle));
    matrix[0]=cosine&0xffffu;matrix[1]=sine&0xffffu;matrix[2]=4096;
    matrix[3]=(0u-sine)&0xffffu;matrix[4]=cosine;
}
static void pitch(uint32_t angle,uint32_t matrix[5]) {
    uint32_t sine=(uint32_t)gool_trig_sin(signed_word(angle)),cosine=(uint32_t)gool_trig_cos(signed_word(angle));
    matrix[0]=4096;matrix[1]=0;matrix[2]=(cosine&0xffffu)|((0u-sine)<<16);
    matrix[3]=sine<<16;matrix[4]=cosine;
}
static int light(const gool_native_render_input *in,int yaw_only,gool_native_render_output *out) {
    uint32_t matrix[5],next[5],composed[5];
    yaw(0u-in->camera_angles[yaw_only?1u:2u],matrix);
    if(!yaw_only) {
        if(in->camera_angles[0]) {
            pitch(0u-in->camera_angles[0],next);
            int error=gool_native_matrix_41ef0(next,matrix,composed);if(error)return error;
            for(uint32_t i=0;i<5;i++)matrix[i]=composed[i];
        }
        uint32_t angle=in->camera_angles[2]-in->camera_angles[1];
        if(angle) {
            yaw(angle,next);
            int error=gool_native_matrix_41ef0(next,matrix,composed);if(error)return error;
            for(uint32_t i=0;i<5;i++)matrix[i]=composed[i];
        }
    }
    for(uint32_t vector=0;vector<2;vector++) {
        int32_t v[3]={half(in->light_vectors[vector][0]),half(in->light_vectors[vector][0]>>16),
            half(in->light_vectors[vector][1])},result[3];
        for(uint32_t row=0;row<3;row++) {
            int64_t sum=0;
            for(uint32_t col=0;col<3;col++) {
                uint32_t index=row*3u+col;
                sum+=(int64_t)half(matrix[index/2u]>>((index&1u)*16u))*v[col];
            }
            result[row]=ir(sum);
        }
        uint32_t index=vector?3u:0u;
        out->light_matrix[index]=((uint32_t)result[0]&0xffffu)|((uint32_t)result[1]<<16);
        /* Native CTC2 writes sign-extended IR3 as a whole control word,
         * including its upper half; preserve this rather than repacking zero. */
        out->light_matrix[index+1u]=(uint32_t)result[2];
    }
    out->light_written=1;return 0;
}
int gool_native_render_prepare(uint32_t function,const gool_native_render_input *in,
    gool_native_render_output *output) {
    if(!in||!output)return GOOL_NATIVE_MATRIX_INVALID;
    int axis=0,flat=0,lighting=0,yaw_only=0,fog=0,bias=0;
    switch(function) {
        case 0x8003d030u:case 0x8003d078u:case 0x8003d0c0u:axis=1;break;
        case 0x8003d054u:case 0x8003d09cu:break;
        case 0x8003cfa8u:fog=1;break;
        case 0x8003cffcu:bias=1;break;
        case 0x8003ce70u:lighting=1;break;
        case 0x8003d3acu:lighting=yaw_only=axis=flat=1;break;
        case 0x8003e67cu:axis=flat=1;break;
        case 0x8003ebe0u:axis=fog=1;break;
        case 0x8003ec34u:lighting=1;fog=2;break;
        default:return GOOL_NATIVE_MATRIX_INVALID;
    }
    gool_native_render_output out={.packet_flags=in->packet_flags,.packet_color=in->packet_color};
    for(uint32_t i=0;i<5;i++)out.light_matrix[i]=in->light_matrix[i];
    if(lighting){int error=light(in,yaw_only,&out);if(error)return error;}
    /* Original3F464 loads t0..t4 from the object's auxiliary matrix. The
     * scratch20 memory operand is the saved camera rotation:camera*object. */
    int error=axis?gool_native_matrix_41fd8(in->auxiliary_rotation,out.rotation):
        gool_native_matrix_41ef0(in->auxiliary_rotation,in->current_rotation,out.rotation);
    if(error)return error;
    if(flat) {
        uint32_t shift=(in->packet_flags&0x40000000u)?4u:8u;
        out.translation[0]=sra(in->translation.position[0],shift);
        out.translation[1]=0u-sra(in->translation.position[1],shift);
        out.translation[2]=(in->packet_flags&0x40000000u)?in->projection_distance<<4:in->projection_distance;
    } else {
        gool_native_translation_input translation=in->translation;
        translation.packet_flags=in->packet_flags;
        for(uint32_t i=0;i<5;i++)translation.camera_rotation[i]=in->current_rotation[i];
        if(bias)translation.position[1]+=in->packet_word50;
        gool_native_translation result;
        error=gool_native_matrix_translation_42058(&translation,&result);if(error)return error;
        for(uint32_t i=0;i<3;i++)out.translation[i]=result.translation[i];
        out.source_y=result.source_y;out.source_y_written=1;
    }
    if(fog) {
        uint32_t delta=out.translation[2]-in->fog_threshold;
        if(signed_word(delta)>=0) {
            delta<<=in->fog_shift&31u;
            delta+=(uint32_t)half(in->packet_flags);
            if(signed_word(delta-4096u)>0)delta=4096u;
            out.packet_flags=(in->packet_flags&0xffff0000u)|(delta&0xffffu);
            out.flags_written=1;
            if(fog==1){out.packet_color=in->fog_color;out.color_written=1;}
        }
    }
    *output=out;return 0;
}
static int words(gool_domain *d,uint32_t address,uint32_t count,uint32_t *output) {
    if(!address||(address&3u)||(uint64_t)address+(uint64_t)count*4u>UINT64_C(0x100000000))
        return GOOL_NATIVE_MATRIX_INVALID;
    for(uint32_t i=0;i<count;i++) {
        int error=gool_domain_read32(d,address+i*4u,&output[i]);if(error)return error;
    }
    return 0;
}
int gool_native_render_packet_type1(gool_domain *d,uint32_t node,uint32_t function,
    uint32_t frame,uint32_t *output) {
    static const uint32_t primitive[34]={
        0,0,0x8003e3acu,0x8003e3acu,0x8003d524u,0x8003d524u,0x8003fc50u,0x8003f2fcu,
        0x8003fc50u,0x8003f2fcu,0x8003e6d0u,0x8003d788u,0x8003e6d0u,0x8003d788u,
        0x8003d954u,0x8003d80cu,0x8003d954u,0x8003d80cu,0x8003f36cu,0x8003d748u,
        0x8003f36cu,0x8003d748u,0x80040bbcu,0x8003e39cu,0x80040bbcu,0x8003e39cu,
        0x8003da2cu,0x8003d8f8u,0x8003da2cu,0x8003d8f8u,0x8003d32cu,0x8003d36cu,
        0x8003d32cu,0x8003d36cu};
    static const uint32_t vertex[34]={
        0x8003f518u,0x8003f518u,0x8003f518u,0x8003e370u,0x8003f518u,0x8003e370u,
        0x8003f518u,0x8003f518u,0x8003e370u,0x8003e370u,0x8003fa80u,0x8003fa80u,
        0x8003e370u,0x8003e370u,0x8003fa80u,0x8003fa80u,0x8003e370u,0x8003e370u,
        0x8003fa80u,0x8003fa80u,0x8003e370u,0x8003e370u,0x8003fa80u,0x8003fa80u,
        0x8003e370u,0x8003e370u,0x8003fa80u,0x8003fa80u,0x8003e370u,0x8003e370u,
        0x8003fa80u,0x8003fa80u,0x8003e370u,0x8003e370u};
    static const uint32_t attribute[8]={0x8003e078u,0x8003d0e4u,0x8003e078u,0x8003e040u,
        0x8003e040u,0x8003fc40u,0x8003e078u,0x8003e078u};
    static const uint32_t shader[8]={0x80040ab8u,0x8003fa9cu,0x8003ea9cu,0x8003da9cu,
        0x8003e8c8u,0x8003fc18u,0x80040a9cu,0x8003cb24u};
    if(!d||!node||!frame||!output||((node|frame)&3u)||node>UINT32_MAX-0x124u||
       frame>UINT32_MAX-0x209cu)return GOOL_NATIVE_MATRIX_INVALID;
    uint32_t cursor,begin,player,list,model,aux,frames[2],geometry,motion,render,tick,mode;
    int e=gool_domain_read32(d,frame+0x2098u,&cursor);if(e)return e;
    e=gool_domain_read32(d,frame+0x2094u,&begin);if(e)return e;
    if(cursor-begin+0xe0u>=0x1a41u) {
        e=gool_domain_read32(d,0x80060a90u,&player);if(e)return e;
        if(node!=player){*output=0;return 0;}
    }
    if(!cursor||(cursor&3u)||cursor>UINT32_MAX-0x70u)return GOOL_NATIVE_MATRIX_INVALID;
    e=gool_domain_read32(d,node+0xe8u,&model);if(e)return e;
    uint8_t kind;e=gool_domain_read8(d,model,&kind);if(e)return e;
    if(kind!=1)return GOOL_NATIVE_MATRIX_INVALID;
    e=gool_domain_read32(d,node+8u,&aux);if(e)return e;
    if(!aux||(aux&3u)||aux>UINT32_MAX-0x2cu)return GOOL_NATIVE_MATRIX_INVALID;
    e=words(d,aux+0x20u,2,frames);if(e)return e;
    e=gool_domain_read32(d,aux+0x28u,&geometry);if(e)return e;
    e=gool_domain_read32(d,node+0xacu,&motion);if(e)return e;
    e=gool_domain_read32(d,node+0x120u,&render);if(e)return e;
    mode=(render>>8)&255u;
    if(!mode){e=gool_domain_read32(d,0x80067a1cu,&mode);if(e)return e;}
    uint32_t shading=(render>>16)&255u;
    if(shading>7u||shading==3u||shading==4u)return GOOL_NATIVE_MATRIX_INVALID;
    /* These six native dispatch entries publish shadow/light records beyond
     * the ordinary type1 packet, and must never silently select mode6. */
    if(mode==10u||mode==14u||mode==18u||mode==22u||mode==26u||mode==30u||mode>32u)
        return GOOL_NATIVE_MATRIX_INVALID;
    if(mode+(frames[0]!=frames[1])>=34u)return GOOL_NATIVE_MATRIX_INVALID;
    uint32_t values[0x70/4]={0},mask=0;
#define PACKET_WORD(off,value) do{values[(off)/4u]=(value);mask|=1u<<((off)/4u);}while(0)
    e=gool_domain_read32(d,frame+0x2090u,&list);if(e)return e;
    PACKET_WORD(0,list);PACKET_WORD(4,function);PACKET_WORD(0x10,node);PACKET_WORD(0x14,aux);
    e=gool_domain_read32(d,node+0xa0u,&values[0x50/4]);if(e)return e;mask|=1u<<(0x50/4);
    e=gool_domain_read32(d,(motion&0x100000u)?node+0xe4u:0x80069034u,&tick);if(e)return e;
    PACKET_WORD(0x18,tick);PACKET_WORD(0x24,(motion&0x40000u)?0x8003e034u:0x8003def4u);
    PACKET_WORD(0x28,attribute[shading]);PACKET_WORD(0x2c,shader[shading]);
    if(shading==6u){e=gool_domain_read32(d,node+0x13cu,&values[0x4c/4]);if(e)return e;mask|=1u<<(0x4c/4);}
    e=gool_domain_read32(d,node+0x118u,&values[0x48/4]);if(e)return e;mask|=1u<<(0x48/4);
    uint32_t selected=mode+(frames[0]!=frames[1]),flags=frames[0]==frames[1]?0x40000000u:0x60000000u;
    PACKET_WORD(8,vertex[selected]);PACKET_WORD(0xc,primitive[selected]);PACKET_WORD(0x30,frames[0]);
    if(frames[0]!=frames[1])PACKET_WORD(0x34,frames[1]);
    uint16_t code;e=gool_domain_read16(d,node+0xb6u,&code);if(e)return e;
    if(code==0x38u)flags|=0x10000000u;
    uint32_t special[2]={0};
    if(motion&0x10000000u)for(uint32_t i=0;i<2;i++) {
        uint32_t candidate;e=gool_domain_read32(d,i?0x800691e0u:0x800690c4u,&candidate);if(e)return e;
        if(candidate) {uint32_t linked;e=gool_domain_read32(d,candidate+0x44u,&linked);if(e)return e;
            special[i]=linked==node;}
    }
    if(!geometry||(geometry&3u)||geometry>UINT32_MAX-0x24u)return GOOL_NATIVE_MATRIX_INVALID;
    for(uint32_t i=0;i<4;i++) {
        e=gool_domain_read32(d,geometry+0x14u+i*4u,&values[(0x38u+i*4u)/4]);if(e)return e;
        mask|=1u<<((0x38u+i*4u)/4);
    }
    e=gool_domain_read32(d,geometry+0x10u,&values[0x6c/4]);if(e)return e;mask|=1u<<(0x6c/4);
    uint32_t count;e=gool_domain_read32(d,geometry+0xcu,&count);if(e)return e;
    if(signed_word(count)<6)PACKET_WORD(0x60,0);
    else {e=gool_domain_read32(d,geometry+0x24u,&values[0x60/4]);if(e)return e;mask|=1u<<(0x60/4);}
    uint16_t fog;e=gool_domain_read16(d,node+0x26u,&fog);if(e)return e;
    if(fog) {
        uint16_t color[3];for(uint32_t i=0;i<3;i++){e=gool_domain_read16(d,node+0x3au+i*2u,&color[i]);if(e)return e;}
        PACKET_WORD(0x20,((uint32_t)half(color[0])<<20)|((uint32_t)half(color[1])<<12)|((uint32_t)half(color[2])<<4));
        flags|=(uint32_t)half(fog);
    }
    uint32_t scale;e=gool_domain_read32(d,node+0x78u,&scale);if(e)return e;
    if(scale&0x80000000u)flags|=0x80000000u;
    PACKET_WORD(0x1c,flags);
    /* Original list/cursor publication precedes payload stores. */
    e=gool_domain_write32(d,frame+0x2098u,cursor+0x70u);if(e)return e;
    e=gool_domain_write32(d,cursor,list);if(e)return e;
    e=gool_domain_write32(d,frame+0x2090u,cursor);if(e)return e;
    for(uint32_t i=1;i<0x70u/4u;i++)if(mask&(1u<<i)) {
        e=gool_domain_write32(d,cursor+i*4u,values[i]);if(e)return e;
    }
    for(uint32_t i=0;i<2;i++)if(special[i]) {
        e=gool_domain_write32(d,0x80060a98u+i*4u,cursor);if(e)return e;
    }
    *output=cursor;return 0;
#undef PACKET_WORD
}
int gool_native_render_capture(gool_domain *d,uint32_t gp,uint32_t scratch,uint32_t packet,
    const uint32_t prior_light[5],uint32_t projection_distance,
    uint32_t *function,gool_native_render_input *output) {
    if(!d||!gp||!scratch||!packet||((gp|scratch|packet)&3u)||gp>UINT32_MAX-0x80u||
       scratch>UINT32_MAX-0xfcu||packet>UINT32_MAX-0x6cu||!prior_light||!function||!output)
        return GOOL_NATIVE_MATRIX_INVALID;
    gool_native_render_input next={.projection_distance=projection_distance};
    uint32_t handler,auxiliary,frame,second_frame,geometry;
    int error=gool_domain_read32(d,packet+4u,&handler);if(error)return error;
    error=gool_domain_read32(d,packet+0x1cu,&next.packet_flags);if(error)return error;
    if(!(next.packet_flags&0x40000000u))return GOOL_NATIVE_MATRIX_INVALID;
    error=gool_domain_read32(d,packet+0x20u,&next.packet_color);if(error)return error;
    error=gool_domain_read32(d,packet+0x50u,&next.packet_word50);if(error)return error;
    error=gool_domain_read32(d,packet+0x14u,&auxiliary);if(error)return error;
    error=words(d,auxiliary,5,next.auxiliary_rotation);if(error)return error;
    error=words(d,scratch+0x20u,5,next.current_rotation);if(error)return error;
    error=words(d,scratch+0x34u,3,next.translation.camera_position);if(error)return error;
    error=words(d,gp+0x60u,3,next.translation.position);if(error)return error;
    error=words(d,gp+0x6cu,3,next.camera_angles);if(error)return error;
    error=words(d,gp+0x78u,3,next.translation.object_scale);if(error)return error;
    error=words(d,packet+0x50u,2,next.light_vectors[0]);if(error)return error;
    error=words(d,packet+0x58u,2,next.light_vectors[1]);if(error)return error;
    error=gool_domain_read32(d,scratch+4u,&next.fog_color);if(error)return error;
    error=gool_domain_read32(d,scratch+8u,&next.fog_shift);if(error)return error;
    error=gool_domain_read32(d,scratch+0x10u,&next.fog_threshold);if(error)return error;
    error=gool_domain_read32(d,packet+0x30u,&frame);if(error)return error;
    if(!frame||(frame&3u)||frame>UINT32_MAX-0xcu)return GOOL_NATIVE_MATRIX_INVALID;
    error=words(d,frame+8u,2,next.translation.frame_offset);if(error)return error;
    if(next.packet_flags&0x20000000u) {
        error=gool_domain_read32(d,packet+0x34u,&second_frame);if(error)return error;
        if(!second_frame||(second_frame&3u)||second_frame>UINT32_MAX-0xcu)return GOOL_NATIVE_MATRIX_INVALID;
        error=words(d,second_frame+8u,2,next.translation.second_offset);if(error)return error;
    }
    error=gool_domain_read32(d,packet+0x6cu,&geometry);if(error)return error;
    error=words(d,geometry,3,next.translation.geometry_scale);if(error)return error;
    for(uint32_t i=0;i<5;i++) {
        next.light_matrix[i]=prior_light[i];next.translation.camera_rotation[i]=next.current_rotation[i];
    }
    next.translation.packet_flags=next.packet_flags;
    *output=next;*function=handler;return 0;
}
int gool_native_render_commit_packet(gool_domain *d,uint32_t scratch,uint32_t packet,
    const gool_native_render_input *captured,const gool_native_render_output *result) {
    if(!d||!scratch||!packet||((scratch|packet)&3u)||scratch>UINT32_MAX-0xfcu||
       packet>UINT32_MAX-0x20u||!captured||!result)return GOOL_NATIVE_MATRIX_INVALID;
    uint32_t value;int error=gool_domain_read32(d,packet+0x1cu,&value);if(error)return error;
    if(value!=captured->packet_flags)return GOOL_NATIVE_MATRIX_INVALID;
    error=gool_domain_read32(d,packet+0x20u,&value);if(error)return error;
    if(value!=captured->packet_color)return GOOL_NATIVE_MATRIX_INVALID;
    if(result->source_y_written) {
        error=gool_domain_write32(d,scratch+0xfcu,result->source_y);if(error)return error;
    }
    if(result->color_written) {
        error=gool_domain_write32(d,packet+0x20u,result->packet_color);if(error)return error;
    }
    if(result->flags_written) {
        error=gool_domain_write16(d,packet+0x1cu,(uint16_t)result->packet_flags);if(error)return error;
    }
    return 0;
}
int gool_native_render_attribute_command(gool_domain *d,uint32_t scratch,uint32_t attributes,
    uint32_t attribute_bytes,uint32_t command,uint32_t output[3]) {
    if(!d||!output||(command>>16))return GOOL_NATIVE_MATRIX_INVALID;
    uint32_t result[3];
    const uint32_t offsets[2]={(command>>7)&0x1fcu,command&0x1fcu};
    for(uint32_t i=0;i<2;i++) {
        uint32_t address;
        if(command&(1u<<i)) {
            if(!scratch||(scratch&3u)||(uint64_t)scratch+0x140u+offsets[i]+4u>UINT64_C(0x100000000))
                return GOOL_NATIVE_MATRIX_INVALID;
            address=scratch+0x140u+offsets[i];
        } else {
            if(!attributes||(attributes&3u)||offsets[i]>attribute_bytes||attribute_bytes-offsets[i]<4u||
               (uint64_t)attributes+offsets[i]+4u>UINT64_C(0x100000000))return GOOL_NATIVE_MATRIX_INVALID;
            address=attributes+offsets[i];
        }
        int error=gool_domain_read32(d,address,&result[i+1u]);if(error)return error;
    }
    result[0]=result[1];
    for(uint32_t i=0;i<3;i++)output[i]=result[i];
    return 0;
}
int gool_native_render_uv_command(gool_domain *d,uint32_t uv_base,uint32_t uv_bytes,
    uint32_t animation_base,uint32_t animation_bytes,uint32_t command,uint32_t clock,
    gool_native_render_uv *output) {
    if(!d||!output||!uv_base||(uv_base&3u)||
       (uint64_t)uv_base+uv_bytes>UINT64_C(0x100000000))return GOOL_NATIVE_MATRIX_INVALID;
    gool_native_render_uv next={.index=command&255u,.record=UINT32_MAX};
    if(command&0x100u) {
        uint32_t visited[64]={0}; /* low11 indices,256 bounded stack bytes */
        if(!animation_base||(animation_base&3u)||(animation_bytes&3u)||
           (uint64_t)animation_base+animation_bytes>UINT64_C(0x100000000))return GOOL_NATIVE_MATRIX_INVALID;
        for(;;) {
            uint32_t index=next.index,offset=index*4u;
            if(index>=2048u||offset>animation_bytes||animation_bytes-offset<4u)
                return GOOL_NATIVE_MATRIX_INVALID;
            uint32_t mask=1u<<(index&31u);
            if(visited[index>>5]&mask)return GOOL_NATIVE_MATRIX_INVALID;
            visited[index>>5]|=mask;
            uint32_t flags;int error=gool_domain_read32(d,animation_base+offset,&flags);if(error)return error;
            next.record=flags;next.lookups++;
            uint32_t phase=((clock>>((flags>>26)&31u))+(flags>>19))&((flags>>12)&127u);
            next.index=(flags+phase)&0x7ffu;
            if(!(flags&0x800u))break;
        }
    }
    uint32_t offset=next.index*12u;
    /* Native primitives read A2-12/-8/-4: A2 is the selected record END,
     * not its beginning. The last record can end exactly at the item end. */
    if(offset>uv_bytes||
       (uint64_t)uv_base+offset>UINT32_MAX)return GOOL_NATIVE_MATRIX_INVALID;
    next.address=uv_base+offset;next.textured=next.index!=0;
    *output=next;return 0;
}
