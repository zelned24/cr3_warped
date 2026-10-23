#include "model_draw.h"
#include "model_actor.h"
#include "model_commands.h"
#include "world_shader_shbin.h"
#include "render_frustum.h"
#include <string.h>
#include <float.h>

/* Bitwise finite check remains valid under the target's -ffast-math. */
static int finite_word(float value) {
    uint32_t raw;memcpy(&raw,&value,sizeof(raw));return (raw&0x7fffffffu)<0x7f800000u;
}
static int matrix_ok(const C3D_Mtx *m) {
    for(uint32_t r=0;r<4;r++) {
        const float values[4]={m->r[r].x,m->r[r].y,m->r[r].z,m->r[r].w};
        for(uint32_t c=0;c<4;c++)if(!finite_word(values[c])||values[c]<-1e8f||values[c]>1e8f)return 0;
    }
    return 1;
}
static int state_ok(const model_draw_state *s) {
    return (unsigned)s->cull<=GPU_CULL_BACK_CCW&&(unsigned)s->alpha_test<=GPU_GEQUAL&&
        (unsigned)s->depth_test<=GPU_GEQUAL&&((unsigned)s->write_mask&~GPU_WRITE_ALL)==0&&
        (unsigned)s->color_equation<=GPU_BLEND_MAX&&(unsigned)s->alpha_equation<=GPU_BLEND_MAX&&
        (unsigned)s->source_color<=GPU_SRC_ALPHA_SATURATE&&(unsigned)s->destination_color<=GPU_SRC_ALPHA_SATURATE&&
        (unsigned)s->source_alpha<=GPU_SRC_ALPHA_SATURATE&&(unsigned)s->destination_alpha<=GPU_SRC_ALPHA_SATURATE&&
        (s->rgb_scale==1||s->rgb_scale==2||s->rgb_scale==4)&&
        (s->alpha_enabled==0||s->alpha_enabled==1)&&(s->depth_enabled==0||s->depth_enabled==1);
}
int model_draw_init(model_draw *o) {
    if(!o||o->initialized||o->vertices||o->shader)return MODEL_DRAW_INVALID;
    *o=(model_draw){.initialized=1};
    o->vertices=linearAlloc(MODEL_DRAW_VERTICES*sizeof(*o->vertices));
    if(!o->vertices)return o->status=MODEL_DRAW_MEMORY;
    o->shader=DVLB_ParseFile((u32*)world_shader_shbin,world_shader_shbin_size);
    if(!o->shader)return o->status=MODEL_DRAW_MEMORY;
    if(R_FAILED(shaderProgramInit(&o->program)))return o->status=MODEL_DRAW_MEMORY;
    o->program_initialized=1;
    if(R_FAILED(shaderProgramSetVsh(&o->program,&o->shader->DVLE[0])))return o->status=MODEL_DRAW_MEMORY;
    o->projection_location=shaderInstanceGetUniformLocation(o->program.vertexShader,"projection");
    o->view_location=shaderInstanceGetUniformLocation(o->program.vertexShader,"modelView");
    if(o->projection_location<0||o->view_location<0)return o->status=MODEL_DRAW_INVALID;
    return 0;
}
int model_draw_begin(model_draw *o) {
    if(!o||!o->initialized||!o->vertices||!o->shader||o->status<0)return MODEL_DRAW_INVALID;
    if(o->recording||o->sealed||o->batch_count||o->submitted)return MODEL_DRAW_BUSY;
    o->recording=1;o->vertex_count=o->cursor=o->submitted_vertices=0;return 0;
}
int model_draw_record(model_draw *o,const model_draw_vertex *vertices,uint32_t count,
    const C3D_Mtx *projection,const C3D_Mtx *view,const model_draw_state *state,
    model_texture_cache *cache,model_texture_handle texture) {
    if(!o||!o->initialized||!o->recording||o->submitted||o->status<0||!vertices||!count||count%3u||
       !projection||!view||!state||!state_ok(state)||!matrix_ok(projection)||!matrix_ok(view))
        return MODEL_DRAW_INVALID;
    if(o->batch_count>MODEL_DRAW_BATCHES||o->vertex_count>MODEL_DRAW_VERTICES||
       count>MODEL_DRAW_VERTICES-o->vertex_count)return MODEL_DRAW_CAPACITY;
    model_draw_batch batch={.projection=*projection,.view=*view,.state=*state,.cache=cache,
        .first=o->vertex_count,.count=count,.low={FLT_MAX,FLT_MAX,FLT_MAX},.high={-FLT_MAX,-FLT_MAX,-FLT_MAX}};
    model_draw_batch *previous=o->batch_count?&o->batches[o->batch_count-1u]:NULL;
    int merge=0;
    if(previous&&previous->first<=o->vertex_count&&previous->count==o->vertex_count-previous->first&&
       previous->cache==cache&&previous->lease.handle.generation==texture.generation&&
       previous->lease.handle.slot==texture.slot&&
       !memcmp(&previous->projection,projection,sizeof(*projection))&&
       !memcmp(&previous->view,view,sizeof(*view))) {
        const model_draw_state *p=&previous->state;
        merge=p->cull==state->cull&&p->alpha_test==state->alpha_test&&p->depth_test==state->depth_test&&
            p->write_mask==state->write_mask&&p->color_equation==state->color_equation&&
            p->alpha_equation==state->alpha_equation&&p->source_color==state->source_color&&
            p->destination_color==state->destination_color&&p->source_alpha==state->source_alpha&&
            p->destination_alpha==state->destination_alpha&&p->blend_constant==state->blend_constant&&
            p->alpha_reference==state->alpha_reference&&p->rgb_scale==state->rgb_scale&&
            p->alpha_enabled==state->alpha_enabled&&p->depth_enabled==state->depth_enabled;
    }
    if(!merge&&o->batch_count==MODEL_DRAW_BATCHES)return MODEL_DRAW_CAPACITY;
    for(uint32_t i=0;i<count;i++) {
        const float values[8]={vertices[i].x,vertices[i].y,vertices[i].z,vertices[i].r,
            vertices[i].g,vertices[i].b,vertices[i].u,vertices[i].v};
        for(uint32_t j=0;j<8;j++)if(!finite_word(values[j])||values[j]<-1e8f||values[j]>1e8f)return MODEL_DRAW_INVALID;
        for(uint32_t j=0;j<3;j++) {
            if(values[j]<batch.low[j])batch.low[j]=values[j];
            if(values[j]>batch.high[j])batch.high[j]=values[j];
        }
    }
    if(cache) {
        int error=model_texture_cache_acquire(cache,texture,&batch.texture,&batch.lease);if(error)return error;
        if(merge) {
            error=model_texture_cache_release(cache,batch.lease);if(error)return error;
        }
    } else {
        if(texture.generation)return MODEL_DRAW_INVALID;
        batch.lease.handle=texture;
    }
    memmove(o->vertices+o->vertex_count,vertices,count*sizeof(*vertices));
    if(merge) {
        previous->count+=count;
        for(uint32_t j=0;j<3;j++) {
            if(batch.low[j]<previous->low[j])previous->low[j]=batch.low[j];
            if(batch.high[j]>previous->high[j])previous->high[j]=batch.high[j];
        }
    } else o->batches[o->batch_count++]=batch;
    o->vertex_count+=count;return 0;
}
static int visible(const model_draw_batch *b) {
    float projection[4][4],view[4][4];double clip[4][4];
    for(uint32_t r=0;r<4;r++) {
        projection[r][0]=b->projection.r[r].x;projection[r][1]=b->projection.r[r].y;
        projection[r][2]=b->projection.r[r].z;projection[r][3]=b->projection.r[r].w;
        view[r][0]=b->view.r[r].x;view[r][1]=b->view.r[r].y;
        view[r][2]=b->view.r[r].z;view[r][3]=b->view.r[r].w;
    }
    render_frustum_matrix(projection,view,clip);return render_frustum_visible(clip,b->low,b->high);
}
int model_draw_submit(model_draw *o,uint32_t budget) {
    if(!o||!o->initialized||(!o->recording&&!o->sealed))return MODEL_DRAW_INVALID;
    if(o->status<0)return o->status;
    if(!budget)return o->cursor<o->batch_count?1:0;
    if(o->recording) {
        GSPGPU_FlushDataCache(o->vertices,o->vertex_count*sizeof(*o->vertices));
        o->recording=0;o->sealed=1;
    }
    C3D_BindProgram(&o->program);
    C3D_AttrInfo *attrs=C3D_GetAttrInfo();AttrInfo_Init(attrs);
    AttrInfo_AddLoader(attrs,0,GPU_FLOAT,3);AttrInfo_AddLoader(attrs,1,GPU_FLOAT,3);AttrInfo_AddLoader(attrs,2,GPU_FLOAT,2);
    for(uint32_t stage=1;stage<6;stage++)C3D_TexEnvInit(C3D_GetTexEnv(stage));
    while(budget--&&o->cursor<o->batch_count) {
        model_draw_batch *b=&o->batches[o->cursor];
        if(visible(b)) {
            C3D_BufInfo *buf=C3D_GetBufInfo();BufInfo_Init(buf);
            if(BufInfo_Add(buf,o->vertices+b->first,sizeof(*o->vertices),3,0x210)<0)
                return o->status=MODEL_DRAW_INVALID;
            const model_draw_state *s=&b->state;
            C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,o->projection_location,&b->projection);
            C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,o->view_location,&b->view);
            C3D_TexEnv *env=C3D_GetTexEnv(0);C3D_TexEnvInit(env);
            C3D_TexEnvSrc(env,C3D_Both,b->texture?GPU_TEXTURE0:GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR,0);
            C3D_TexEnvFunc(env,C3D_Both,b->texture?GPU_MODULATE:GPU_REPLACE);
            C3D_TexEnvScale(env,C3D_RGB,s->rgb_scale==1?GPU_TEVSCALE_1:s->rgb_scale==2?GPU_TEVSCALE_2:GPU_TEVSCALE_4);
            C3D_CullFace(s->cull);C3D_AlphaTest(s->alpha_enabled,s->alpha_test,s->alpha_reference);
            C3D_DepthTest(s->depth_enabled,s->depth_test,s->write_mask);C3D_BlendingColor(s->blend_constant);
            C3D_AlphaBlend(s->color_equation,s->alpha_equation,s->source_color,s->destination_color,
                s->source_alpha,s->destination_alpha);
            if(b->texture)C3D_TexBind(0,b->texture);
            C3D_DrawArrays(GPU_TRIANGLES,0,b->count);o->submitted=1;o->submitted_vertices+=b->count;
        }
        o->cursor++;
    }
    return o->cursor<o->batch_count?1:0;
}
int model_draw_retire(model_draw *o) {
    if(!o||!o->initialized)return MODEL_DRAW_INVALID;
    if(o->submitted) {
        /* FrameSync is only a VBlank wait. FrameBegin(0) actually waits for
         * Citro3D's prior GX queue and rejects callers still inside a frame.
         * Close this empty barrier frame before releasing caller leases.
         * Prior FrameEnd must already have flushed all frame-hook work.
         */
        if(!C3D_FrameBegin(0))return MODEL_DRAW_BUSY;
        C3D_FrameEnd(0);o->submitted=0;
    }
    for(uint32_t i=0;i<o->batch_count;i++) {
        model_draw_batch *b=&o->batches[i];
        if(b->lease.texture.generation) {
            int error=model_texture_cache_release(b->cache,b->lease);if(error)return error;
            b->lease=(model_texture_cache_lease){0};
        }
    }
    o->recording=o->sealed=0;o->batch_count=o->vertex_count=o->cursor=o->submitted_vertices=0;return 0;
}
int model_draw_close(model_draw *o) {
    if(!o)return MODEL_DRAW_INVALID;
    if(!o->initialized)return 0;
    int error=model_draw_retire(o);if(error)return error;
    if(o->program_initialized) {
        if(R_FAILED(shaderProgramFree(&o->program)))return MODEL_DRAW_INVALID;
        o->program_initialized=0;
    }
    if(o->shader)DVLB_Free(o->shader);
    if(o->vertices)linearFree(o->vertices);
    *o=(model_draw){0};return 0;
}
static int prepare_triangle(const model_pose *pose,const model_triangles *topology,
    uint32_t triangle,uint8_t mode,uint32_t resolved_uv,uint32_t scratch,const gool_native_shading_controls *controls,
    const model_material_selection *selection,const model_triangle *resolved,
    model_draw_triangle *output,gool_native_shading_result *shading) {
    if(!pose||!pose->opened||pose->active||pose->status!=MODEL_POSE_DONE||pose->decoded!=pose->count||
       !topology||!topology->opened||topology->active||topology->status!=MODEL_TRIANGLES_DONE||
       !topology->retained||!output||triangle>=topology->count||pose->count!=topology->vertex_count||
       pose->bank!=topology->bank||pose->geometry!=topology->entry||
       pose->geometry_identity.slot!=topology->lease.slot||
       pose->geometry_identity.generation!=topology->lease.generation)return MODEL_DRAW_INVALID;
    switch(mode) {
        case 0:case 1:case 2:case 5:case 6:case 7:break;
        case 3:case 4:if(!controls||!shading)return MODEL_DRAW_SHADING;break;
        default:return MODEL_DRAW_SHADING;
    }
    const model_triangle *source=resolved?resolved:&topology->output[triangle];
    gool_native_shading_result processed={0};int flat=mode==5;uint32_t flat_rgb=0x00204088u;
    if(mode==3||mode==4) {
        uint32_t normal;
        if(source->corners[0].attribute_stored&&!scratch)return MODEL_DRAW_SHADING;
        int error=0;
        if(resolved)normal=source->corners[0].attribute;
        else error=model_material_attribute_read(topology,triangle,0,scratch,&normal);
        if(error)return error;
        error=gool_native_shading_normal(topology->bank->domain,controls,normal,&processed);
        if(error)return error;
        flat=1;flat_rgb=processed.rgb;
    }
    model_draw_triangle next={0};uint32_t uv=source->flags&255u;
    int animated=(source->flags&0x100u)!=0;
    if(mode==5)next.textured=0;
    else if(selection) {
        next.textured=selection->textured;
        if(next.textured){uv=selection->index;next.material=selection->material;}
    }
    else if(animated) {
        if(resolved_uv==UINT32_MAX)return MODEL_DRAW_INVALID;
        next.textured=1;uv=resolved_uv;
    } else {
        if(resolved_uv!=UINT32_MAX)return MODEL_DRAW_INVALID;
        next.textured=uv!=0;if(uv)uv--;
    }
    if(next.textured&&!selection) {
        int error=model_material_read(topology,uv,&next.material);if(error)return error;
    }
    for(uint32_t k=0;k<3;k++) {
        uint32_t i=k;if(i&&!(source->flags&0x20000000u))i=3u-i;
        const model_triangle_corner *corner=&source->corners[i];
        if(corner->vertex>=pose->count)return MODEL_DRAW_INVALID;
        int32_t q5[3];int error=model_pose_components(&pose->output[corner->vertex],q5);if(error)return error;
        model_draw_vertex *v=&next.vertices[k];
        v->x=(float)q5[0];v->y=(float)q5[1];v->z=(float)q5[2];
        uint32_t rgb=flat?flat_rgb:corner->attribute;
        if(!flat&&corner->attribute_stored) {
            if(!scratch)return MODEL_DRAW_SHADING;
            error=model_material_attribute_read(topology,triangle,i,scratch,&rgb);if(error)return error;
        }
        v->r=(float)(rgb&255u)*(1.0f/255.0f);
        v->g=(float)((rgb>>8)&255u)*(1.0f/255.0f);
        v->b=(float)((rgb>>16)&255u)*(1.0f/255.0f);
        if(next.textured) {
            v->u=((float)(next.material.u[i]-next.material.min_u)+0.5f)/MODEL_GPU_TEXTURE_WIDTH;
            v->v=1.0f-((float)(next.material.v[i]-next.material.min_v)+0.5f)/MODEL_GPU_TEXTURE_HEIGHT;
        }
    }
    *output=next;if(shading)*shading=processed;return 0;
}
int model_draw_prepare_triangle(const model_pose *pose,const model_triangles *topology,
    uint32_t triangle,uint8_t mode,uint32_t resolved_uv,model_draw_triangle *output) {
    return prepare_triangle(pose,topology,triangle,mode,resolved_uv,0,NULL,NULL,NULL,output,NULL);
}
int model_draw_prepare_actor_triangle(const model_actor *actor,uint32_t triangle,
    uint32_t resolved_uv,model_draw_triangle *output) {
    if(!actor||!actor->opened||actor->active||actor->status!=MODEL_ACTOR_DONE||!actor->retained)
        return MODEL_DRAW_INVALID;
    return model_draw_prepare_triangle(&actor->pose,&actor->topology,triangle,
        (uint8_t)(actor->render_word>>16),resolved_uv,output);
}
int model_draw_prepare_normal_actor_triangle(const model_actor *actor,uint32_t triangle,
    uint32_t resolved_uv,const gool_native_shading_controls *controls,model_draw_triangle *output,
    gool_native_shading_result *shading) {
    if(!actor||!actor->opened||actor->active||actor->status!=MODEL_ACTOR_DONE||!actor->retained||
       !controls||!shading)return MODEL_DRAW_INVALID;
    uint8_t mode=(uint8_t)(actor->render_word>>16);
    if(mode!=3&&mode!=4)return MODEL_DRAW_SHADING;
    return prepare_triangle(&actor->pose,&actor->topology,triangle,mode,resolved_uv,
        controls->attribute_scratch,controls,NULL,NULL,output,shading);
}
int model_draw_prepare_native_actor_triangle(const model_actor *actor,uint32_t triangle,
    uint32_t resolved_uv,uint32_t scratch,const gool_native_shading_controls *controls,
    model_draw_triangle *output,gool_native_shading_result *shading) {
    if(!actor||!actor->opened||actor->active||actor->status!=MODEL_ACTOR_DONE||!actor->retained)
        return MODEL_DRAW_INVALID;
    return prepare_triangle(&actor->pose,&actor->topology,triangle,(uint8_t)(actor->render_word>>16),
        resolved_uv,scratch,controls,NULL,NULL,output,shading);
}
int model_draw_prepare_timed_actor_triangle(const model_actor *actor,uint32_t triangle,
    uint32_t scratch,const gool_native_shading_controls *controls,
    model_draw_triangle *output,gool_native_shading_result *shading) {
    if(!actor||!actor->opened||actor->active||actor->status!=MODEL_ACTOR_DONE||!actor->retained)
        return MODEL_DRAW_INVALID;
    uint8_t mode=(uint8_t)(actor->render_word>>16);
    model_material_selection selection={0};
    if(mode!=5) {
        int error=model_material_resolve(&actor->topology,triangle,scratch,&selection);if(error)return error;
    }
    return prepare_triangle(&actor->pose,&actor->topology,triangle,mode,UINT32_MAX,
        scratch,controls,&selection,NULL,output,shading);
}
int model_draw_prepare_command_triangle(const model_commands *command,
    const gool_native_shading_controls *controls,model_draw_triangle *output,gool_native_shading_result *shading) {
    if(!command||!command->opened||!command->retained||command->status!=MODEL_COMMANDS_TRIANGLE||
       !command->actor||!command->actor->opened||!command->actor->consumers)return MODEL_DRAW_INVALID;
    const model_actor *actor=command->actor;uint8_t mode=(uint8_t)(actor->render_word>>16);
    model_material_selection selection={0};
    if(mode!=5) {
        int error=model_material_resolve(&actor->topology,command->index,command->scratch,&selection);
        if(error)return error;
    }
    if(!output||((mode==3||mode==4)&&!shading))return MODEL_DRAW_INVALID;
    model_draw_triangle next;gool_native_shading_result processed;
    int error=prepare_triangle(&actor->pose,&actor->topology,command->index,mode,UINT32_MAX,
        command->scratch,controls,&selection,&command->triangle,&next,&processed);
    if(error)return error;
    for(uint32_t k=0;k<3;k++) {
        uint32_t i=k;if(i&&!(command->triangle.flags&0x20000000u))i=3u-i;
        int32_t q5[3];error=model_pose_components(&command->vertices[i],q5);if(error)return error;
        next.vertices[k].x=(float)q5[0];next.vertices[k].y=(float)q5[1];next.vertices[k].z=(float)q5[2];
    }
    *output=next;if(shading)*shading=processed;return 0;
}
int model_draw_record_camera_triangle(model_draw *o,const model_clip_vertex input[3],
    int32_t near,const model_project_controls *controls,const C3D_Mtx *viewport,
    const model_draw_state *state,model_texture_cache *cache,model_texture_handle texture,
    model_draw_clip_result *result) {
    if(!o||!o->initialized||!o->recording||o->sealed||o->status<0||!input||!controls||!viewport||
       !state||!result||!state_ok(state)||!matrix_ok(viewport)||(!cache&&texture.generation))
        return MODEL_DRAW_INVALID;
    for(uint32_t i=0;i<3;i++)for(uint32_t channel=0;channel<3;channel++)
        if(input[i].attributes[channel]<0||input[i].attributes[channel]>65536)return MODEL_DRAW_INVALID;
    model_clip_polygon polygon;
    int error=model_clip_near(input,near,&polygon);if(error)return error;
    model_draw_clip_result next={0};
    if(polygon.count<3u){*result=next;return 0;}
    model_draw_vertex corners[4],vertices[6];
    for(uint32_t i=0;i<polygon.count;i++) {
        model_project_vertex projected;
        error=model_project_camera(controls,polygon.vertices[i].camera,&projected);if(error)return error;
        next.projection_flags|=projected.flags;
        const int32_t *a=polygon.vertices[i].attributes;
        corners[i]=(model_draw_vertex){(float)projected.x,(float)projected.y,(float)projected.depth,
            (float)a[0]*(1.0f/65536.0f),(float)a[1]*(1.0f/65536.0f),(float)a[2]*(1.0f/65536.0f),
            (float)a[3]*(1.0f/65536.0f),(float)a[4]*(1.0f/65536.0f)};
    }
    for(uint32_t i=1;i+1u<polygon.count;i++) {
        vertices[next.vertices++]=corners[0];vertices[next.vertices++]=corners[i];
        vertices[next.vertices++]=corners[i+1u];
    }
    C3D_Mtx identity;Mtx_Identity(&identity);
    error=model_draw_record(o,vertices,next.vertices,viewport,&identity,state,cache,texture);if(error)return error;
    *result=next;return 0;
}
static int32_t packet_xy(uint32_t value) {
    value&=0x7ffu;return value&0x400u?(int32_t)value-2048:(int32_t)value;
}
int model_draw_record_packet_triangle(model_draw *o,const model_packet_command *c,
    const model_material_uv *material,const C3D_Mtx *viewport,float depth,const model_draw_state *state,
    model_texture_cache *cache,model_texture_handle texture) {
    if(!o||!c||c->kind!=MODEL_PACKET_TRIANGLE||!viewport||!state||!finite_word(depth)||
       !state_ok(state)||!matrix_ok(viewport)||c->textured>1u||c->raw_texture>1u||
       state->rgb_scale!=(c->textured&&!c->raw_texture?2u:1u)||
       (!c->textured&&(material||cache||texture.generation||texture.slot))||
       (c->textured&&(!material||!cache)))return MODEL_DRAW_INVALID;
    model_draw_vertex vertices[3]={0};
    for(uint32_t i=0;i<3;i++) {
        vertices[i].x=(float)packet_xy(c->sxy[i]);vertices[i].y=(float)packet_xy(c->sxy[i]>>16);
        vertices[i].z=depth;
        uint32_t rgb=c->textured&&c->raw_texture?0xffffffu:c->colors[i];
        float unit=c->textured&&!c->raw_texture?1.0f/256.0f:1.0f/255.0f;
        vertices[i].r=(float)(rgb&255u)*unit;
        vertices[i].g=(float)((rgb>>8)&255u)*unit;
        vertices[i].b=(float)((rgb>>16)&255u)*unit;
    }
    if(c->textured) {
        uint16_t page=(uint16_t)(c->uv[1]>>16),clut=(uint16_t)(c->uv[0]>>16);
        uint32_t code=(page>>7)&3u;
        if(code==3u||material->texpage!=page||material->clut!=clut||
           material->depth!=(4u<<code)||!(material->texture_eid&1u))return MODEL_DRAW_INVALID;
        model_texture_cache_lease lease={0};C3D_Tex *gpu;
        int error=model_texture_cache_acquire(cache,texture,&gpu,&lease);if(error)return error;
        const model_material_uv *resident=&cache->slots[texture.slot].gpu.material;
        if(resident->texture_eid!=material->texture_eid||resident->clut!=clut||
           (resident->texpage&~0x1c00u)!=(page&~0x1c00u))error=MODEL_DRAW_INVALID;
        for(uint32_t i=0;i<3&&!error;i++)error=model_texture_cache_uv(cache,lease,
            (uint8_t)c->uv[i],(uint8_t)(c->uv[i]>>8),&vertices[i].u,&vertices[i].v);
        int released=model_texture_cache_release(cache,lease);
        if(released)return released;
        if(error)return error;
    }
    C3D_Mtx identity;Mtx_Identity(&identity);
    return model_draw_record(o,vertices,3u,viewport,&identity,state,cache,texture);
}
