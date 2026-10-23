#include <3ds.h>
#include <citro3d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include "world_texture.h"
#include "props.h"
#include "world_shader_shbin.h"
#include "render_frustum.h"
#define FILE_LIMIT (16u*1024u*1024u)
#define BATCH_LIMIT 8192u
#define TEXTURE_LIMIT 2048u
static DVLB_s *dvlb;
static shaderProgram_s program;
static int projection_location,view_location;
#include "cwt_format.h"

typedef struct {
    unsigned char *bytes;
    C3D_Tex *tex;
    unsigned tex_count,batch_count,vo,io,bo;
} texture_asset;
#define PROP_ASSET_COUNT 8u
static texture_asset world_asset,actor_asset,prop_assets[PROP_ASSET_COUNT];
static int props_ready;
static int selected_sprite_frame=-1;
static texture_asset *current=&world_asset;
#define payload (current->bytes)
#define textures (current->tex)
#define texture_count (current->tex_count)
#define batch_count (current->batch_count)
#define vertex_offset (current->vo)
#define index_offset (current->io)
#define batch_offset (current->bo)

static void unload_asset(void){
    if(textures){for(unsigned i=0;i<texture_count;i++)if(textures[i].data)C3D_TexDelete(&textures[i]);free(textures);}
    textures=NULL;texture_count=batch_count=0;
    if(payload)linearFree(payload);
    payload=NULL;
}
int world_texture_init(void){
    dvlb=DVLB_ParseFile((u32*)world_shader_shbin,world_shader_shbin_size);
    if(!dvlb)return -1;
    shaderProgramInit(&program);shaderProgramSetVsh(&program,&dvlb->DVLE[0]);
    projection_location=shaderInstanceGetUniformLocation(program.vertexShader,"projection");
    view_location=shaderInstanceGetUniformLocation(program.vertexShader,"modelView");
    return 0;
}
static int load_asset(unsigned id,const char *path){
    unload_asset();
    if(!dvlb)return -1;
    FILE *f=fopen(path,"rb");if(!f)return -1;
    if(fseek(f,0,SEEK_END)){fclose(f);return -1;}
    long len=ftell(f);if(len<64 || (unsigned long)len>FILE_LIMIT || fseek(f,0,SEEK_SET)){fclose(f);return -1;}
    unsigned size=(unsigned)len;
    payload=linearAlloc(size);if(!payload){fclose(f);return -1;}
    size_t got=fread(payload,1,size,f);int bad=ferror(f);int closed=fclose(f);
    if(got!=size||bad||closed)goto fail;
    if(cwt_validate(payload,size,id)!=0)goto fail;
    batch_count=rd(payload+20);vertex_offset=rd(payload+28);index_offset=rd(payload+32);batch_offset=rd(payload+36);
    unsigned nt=rd(payload+24),tex_offset=rd(payload+40);
    textures=calloc(nt,sizeof(*textures));if(!textures)goto fail;
    texture_count=nt;
    for(unsigned i=0;i<nt;i++){
        const unsigned char *t=payload+tex_offset+i*32;
        if(!C3D_TexInit(&textures[i],rd(t),rd(t+4),GPU_RGBA5551))goto fail;
        C3D_TexSetFilter(&textures[i],GPU_NEAREST, GPU_NEAREST);
        C3D_TexSetWrap(&textures[i],GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
        memcpy(textures[i].data,payload+rd(t+8),rd(t+12));C3D_TexFlush(&textures[i]);
    }
    GSPGPU_FlushDataCache(payload,size);return 0;
fail:unload_asset();return -1;
}
static int draw_asset(const C3D_Mtx *projection,const C3D_Mtx *view,float x,float y,float z,float radius){
    if(!payload||!textures)return -1;
    C3D_BindProgram(&program);
    C3D_AttrInfo *attrs=C3D_GetAttrInfo();AttrInfo_Init(attrs);
    AttrInfo_AddLoader(attrs,0,GPU_FLOAT,3);AttrInfo_AddLoader(attrs,1,GPU_FLOAT,3);AttrInfo_AddLoader(attrs,2,GPU_FLOAT,2);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,projection_location,projection);
    C3D_TexEnv *env=C3D_GetTexEnv(0);C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env,C3D_Both,GPU_TEXTURE0,GPU_PRIMARY_COLOR,0);C3D_TexEnvFunc(env,C3D_Both,GPU_MODULATE);
    C3D_TexEnvScale(env,C3D_RGB,GPU_TEVSCALE_2);
    C3D_AlphaTest(true,GPU_GREATER,0);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
    C3D_CullFace(GPU_CULL_NONE);
    int drawn=0;
    for(int sky=1;sky>=0;sky--){
        C3D_Mtx mv=*view;
        if(sky)mv.r[0].w=mv.r[1].w=mv.r[2].w=0;
        float projection_rows[4][4],view_rows[4][4];double clip[4][4];
        for(int r=0;r<4;r++){
            projection_rows[r][0]=projection->r[r].x;projection_rows[r][1]=projection->r[r].y;
            projection_rows[r][2]=projection->r[r].z;projection_rows[r][3]=projection->r[r].w;
            view_rows[r][0]=mv.r[r].x;view_rows[r][1]=mv.r[r].y;
            view_rows[r][2]=mv.r[r].z;view_rows[r][3]=mv.r[r].w;
        }
        render_frustum_matrix(projection_rows,view_rows,clip);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,view_location,&mv);
        C3D_DepthTest(!sky,sky?GPU_ALWAYS:GPU_GREATER,sky?GPU_WRITE_COLOR:GPU_WRITE_ALL);
        for(unsigned i=0;i<batch_count;i++){
            if(selected_sprite_frame>=0 && (int)i!=selected_sprite_frame)continue;
            const unsigned char *b=payload+batch_offset+i*56;
            if((int)rd(b+20)!=sky)continue;
            if(!sky){float dist=0;float p[3]={x,y,z};for(int j=0;j<3;j++){float lo=rf(b+32+j*4),hi=rf(b+44+j*4);float d=p[j]<lo?lo-p[j]:(p[j]>hi?p[j]-hi:0);dist+=d*d;}if(dist>radius*radius)continue;}
            {
                float low[3],high[3];
                if(current==&actor_asset){
                    /* SVTX writes world-space positions in place. Recompute
                     * bounds from the live batch instead of its exported pose. */
                    for(int j=0;j<3;j++){low[j]=FLT_MAX;high[j]=-FLT_MAX;}
                    int finite=1;
                    for(unsigned vi=0;vi<rd(b+4);vi++){
                        const unsigned char *vertex=payload+vertex_offset+(rd(b)+vi)*32;
                        for(int j=0;j<3;j++){
                            float value=rf(vertex+j*4);
                            if(!isfinite(value)){finite=0;continue;}
                            if(value<low[j])low[j]=value;
                            if(value>high[j])high[j]=value;
                        }
                    }
                    if(!finite){low[0]=NAN;high[0]=NAN;}
                }else{
                    for(int j=0;j<3;j++){low[j]=rf(b+32+j*4);high[j]=rf(b+44+j*4);}
                }
                if(!render_frustum_visible(clip,low,high))continue;
            }
            C3D_BufInfo *buf=C3D_GetBufInfo();BufInfo_Init(buf);BufInfo_Add(buf,payload+vertex_offset+rd(b)*32,32,3,0x210);
            C3D_TexBind(0,&textures[rd(b+16)]);
            C3D_DrawElements(GPU_TRIANGLES,rd(b+12),C3D_UNSIGNED_SHORT,payload+index_offset+rd(b+8)*2);
            drawn+=rd(b+12);
        }
    }
    C3D_AlphaTest(false,GPU_ALWAYS,0);C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_ALL);
    return drawn;
}
void world_texture_unload(void){current=&world_asset;unload_asset();}
int world_texture_load(unsigned id){
    current=&world_asset;
    char path[100];snprintf(path,sizeof(path),"sdmc:/3ds/crashwarped3ds/world/lv_%02X.cwt",id);
    return load_asset(id,path);
}
int world_texture_draw(const C3D_Mtx *p,const C3D_Mtx *v,float x,float y,float z,float r){
    current=&world_asset;return draw_asset(p,v,x,y,z,r);
}
int world_texture_load_actor(void){
    current=&actor_asset;
    int result=load_asset(0xCB3,"sdmc:/3ds/crashwarped3ds/characters/crash.cwt");
    current=&world_asset;return result;
}
int world_texture_draw_actor(const C3D_Mtx *p,const C3D_Mtx *v,const level_vertex *vertices,int count){
    current=&actor_asset;
    if(!payload || rd(payload+12)!=(unsigned)count){current=&world_asset;return -1;}
    for(int i=0;i<count;i++){
        unsigned char *dst=payload+vertex_offset+i*32;
        memcpy(dst,&vertices[i].x,12);
    }
    GSPGPU_FlushDataCache(payload+vertex_offset,count*32);
    int result=draw_asset(p,v,0,0,0,1e9f);
    current=&world_asset;return result;
}
int world_texture_load_props(void){
    static const char *names[PROP_ASSET_COUNT]={"wood","tnt","nitro","checkpoint","wumpa","spring","bounce","aku_mask"};
    props_ready=0;
    for(unsigned i=0;i<PROP_ASSET_COUNT;i++){
        char path[120];snprintf(path,sizeof(path),"sdmc:/3ds/crashwarped3ds/characters/%s.cwt",names[i]);
        current=&prop_assets[i];
        if(load_asset(0xCB4+i,path)!=0 && i<5){
            /* A disabled prop set must not retain earlier allocations or
             * untouched assets from the preceding load attempt. */
            for(unsigned j=0;j<PROP_ASSET_COUNT;j++){
                current=&prop_assets[j];unload_asset();
            }
            current=&world_asset;return -1;
        }
    }
    props_ready=1;current=&world_asset;return 0;
}
int world_texture_props_ready(void){return props_ready;}
int world_texture_draw_prop(unsigned type,const C3D_Mtx *projection,const C3D_Mtx *view,
                           float x,float y,float z,float angle,float sx,float sy,float sz,float seconds){
    int asset_index=props_textured_asset_index(type);
    if(!props_ready || asset_index<0 || !prop_assets[asset_index].bytes)return -1;
    current=&prop_assets[asset_index];
    unsigned frames=rd(payload+52);
    selected_sprite_frame=-1;
    if(type==PROP_WUMPA && frames){
        /* Reduce before converting to int: long sessions and infinity must
         * not overflow the integer frame selector. The 15 Hz presentation
         * clock remains provisional until GOOL owns sprite frame selection. */
        double phase=isfinite(seconds) && seconds>0.0f ?
            fmod((double)seconds*15.0,(double)frames):0.0;
        selected_sprite_frame=(int)phase;
    }
    C3D_Mtx transform;
    if(type==PROP_WUMPA){
        /* FruiC is a GOOL SPRITE: its exported geometry is an XY quad,
         * not a TGEO mesh. Transform its center into camera space but keep
         * the quad's axes aligned with the camera. The former Y rotation
         * compensated only yaw, leaving camera pitch uncompensated. */
        float cx=view->r[0].x*x+view->r[0].y*y+view->r[0].z*z+view->r[0].w;
        float cy=view->r[1].x*x+view->r[1].y*y+view->r[1].z*z+view->r[1].w;
        float cz=view->r[2].x*x+view->r[2].y*y+view->r[2].z*z+view->r[2].w;
        Mtx_Identity(&transform);
        Mtx_Translate(&transform,cx,cy,cz,true);
    }else{
        transform=*view;
        Mtx_Translate(&transform,x,y,z,true);
        Mtx_RotateY(&transform,angle,true);
    }
    Mtx_Scale(&transform,sx,sy,sz);
    int result=draw_asset(projection,&transform,0,0,0,1e9f);
    selected_sprite_frame=-1;
    current=&world_asset;return result;
}
void world_texture_exit(void){props_ready=0;for(unsigned i=0;i<PROP_ASSET_COUNT;i++){current=&prop_assets[i];unload_asset();}current=&actor_asset;unload_asset();current=&world_asset;unload_asset();if(dvlb){shaderProgramFree(&program);DVLB_Free(dvlb);dvlb=NULL;}}
