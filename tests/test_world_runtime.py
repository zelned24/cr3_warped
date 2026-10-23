"""Exercise production world renderer allocation, loading, drawing and cleanup.
GPU calls are recorded, not emulated; visual hardware behavior remains untested.
"""
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1]
SDK=r'''
#ifndef MOCK_C3D
#define MOCK_C3D
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
typedef uint32_t u32;
typedef union{float c[4];struct{float w,z,y,x;};} Vec;
typedef struct{Vec r[4];} C3D_Mtx;
typedef struct{void*data;unsigned size;} C3D_Tex;
typedef struct{int dummy;} C3D_AttrInfo;
typedef struct{int dummy;} C3D_BufInfo;
typedef struct{int dummy;} C3D_TexEnv;
typedef struct{int DVLE[1];} DVLB_s;
typedef struct{void *vertexShader;} shaderProgram_s;
enum{GPU_RGBA5551,GPU_NEAREST,GPU_CLAMP_TO_EDGE,GPU_FLOAT,GPU_VERTEX_SHADER,
C3D_Both,GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_MODULATE,C3D_RGB,GPU_TEVSCALE_2,
GPU_GREATER,GPU_ALWAYS,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_CULL_NONE,
GPU_WRITE_COLOR,GPU_WRITE_ALL,GPU_TRIANGLES,C3D_UNSIGNED_SHORT};
static int live,texture_live,flushes,draws,fail_alloc;
static int tex_fail_after=-1;
static C3D_AttrInfo attrs;
static C3D_BufInfo buffers;
static C3D_TexEnv env;
static void *linearAlloc(size_t n){if(fail_alloc)return NULL;void*p=malloc(n);if(p)live++;return p;}
static void linearFree(void*p){assert(p);live--;free(p);}
static DVLB_s *DVLB_ParseFile(u32*p,unsigned n){(void)p;(void)n;return calloc(1,sizeof(DVLB_s));}
static void DVLB_Free(DVLB_s*p){free(p);}
static void shaderProgramInit(shaderProgram_s*p){memset(p,0,sizeof(*p));}
static void shaderProgramSetVsh(shaderProgram_s*p,void*v){p->vertexShader=v;}
static int shaderInstanceGetUniformLocation(void*p,const char*s){(void)p;(void)s;return 0;}
static void shaderProgramFree(shaderProgram_s*p){(void)p;}
static int C3D_TexInit(C3D_Tex*t,unsigned w,unsigned h,int fmt){(void)fmt;if(tex_fail_after==0)return 0;if(tex_fail_after>0)tex_fail_after--;t->size=w*h*2;t->data=malloc(t->size);if(t->data)texture_live++;return t->data!=NULL;}
static void C3D_TexDelete(C3D_Tex*t){assert(t->data);texture_live--;free(t->data);t->data=NULL;}
static void C3D_TexFlush(C3D_Tex*t){assert(t->data);flushes++;}
static void C3D_TexSetFilter(C3D_Tex*t,int a,int b){(void)t;(void)a;(void)b;}
static void C3D_TexSetWrap(C3D_Tex*t,int a,int b){(void)t;(void)a;(void)b;}
static void GSPGPU_FlushDataCache(void*p,unsigned n){assert(p&&n);}
static void C3D_BindProgram(shaderProgram_s*p){(void)p;}
static C3D_AttrInfo *C3D_GetAttrInfo(void){return &attrs;}
static void AttrInfo_Init(C3D_AttrInfo*p){(void)p;}
static void AttrInfo_AddLoader(C3D_AttrInfo*p,int a,int b,int c){(void)p;(void)a;(void)b;(void)c;}
static void C3D_FVUnifMtx4x4(int a,int b,const C3D_Mtx*p){(void)a;(void)b;(void)p;}
static C3D_TexEnv *C3D_GetTexEnv(int a){(void)a;return &env;}
static void C3D_TexEnvInit(C3D_TexEnv*p){(void)p;}
static void C3D_TexEnvSrc(C3D_TexEnv*p,int a,int b,int c,int d){(void)p;(void)a;(void)b;(void)c;(void)d;}
static void C3D_TexEnvFunc(C3D_TexEnv*p,int a,int b){(void)p;(void)a;(void)b;}
static void C3D_TexEnvScale(C3D_TexEnv*p,int a,int b){(void)p;(void)a;(void)b;}
static void C3D_AlphaTest(int a,int b,int c){(void)a;(void)b;(void)c;}
static void C3D_AlphaBlend(int a,int b,int c,int d,int e,int f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;}
static void C3D_CullFace(int a){(void)a;}
static void C3D_DepthTest(int a,int b,int c){(void)a;(void)b;(void)c;}
static C3D_BufInfo *C3D_GetBufInfo(void){return &buffers;}
static void BufInfo_Init(C3D_BufInfo*p){(void)p;}
static void BufInfo_Add(C3D_BufInfo*p,void*d,int a,int b,int c){(void)p;(void)a;(void)b;(void)c;assert(d);}
static void C3D_TexBind(int a,C3D_Tex*t){(void)a;assert(t->data);}
static void C3D_DrawElements(int a,int n,int c,void*p){(void)a;(void)c;assert(n>0&&n%3==0&&p);draws++;}
static void Mtx_Identity(C3D_Mtx*p){memset(p,0,sizeof(*p));p->r[0].x=1;p->r[1].y=1;p->r[2].z=1;p->r[3].w=1;}
static void Mtx_Translate(C3D_Mtx*p,float x,float y,float z,int a){(void)p;(void)x;(void)y;(void)z;(void)a;}
static void Mtx_RotateY(C3D_Mtx*p,float y,int a){(void)p;(void)y;(void)a;}
static void Mtx_Scale(C3D_Mtx*p,float x,float y,float z){(void)p;(void)x;(void)y;(void)z;}
#endif
'''
TEST=r'''
#include <stdbool.h>
#include "world_texture.c"
const unsigned char world_shader_shbin[4]={0};
const unsigned int world_shader_shbin_size=4;
#ifdef NDEBUG
#error Assertions must be active
#endif
int main(int argc,char**argv){
    assert(argc==11);
    assert(world_texture_init()==0);
    assert(load_asset(11,argv[1])==0);
    assert(live==1 && texture_live>0 && flushes==texture_live);
    C3D_Mtx m={0};
    assert(world_texture_draw(&m,&m,3034,189,25450,16000)>0 && draws>0);
    world_texture_unload();assert(live==0&&texture_live==0);
    tex_fail_after=0;assert(load_asset(11,argv[1])==-1);assert(live==0&&texture_live==0);
    tex_fail_after=5;assert(load_asset(11,argv[1])==-1);assert(live==0&&texture_live==0);
    tex_fail_after=-1;
    fail_alloc=1;assert(load_asset(11,argv[1])==-1);assert(live==0&&texture_live==0);fail_alloc=0;
    assert(load_asset(12,argv[1])==-1);assert(live==0&&texture_live==0);
    current=&actor_asset;assert(load_asset(0xCB3,argv[2])==0);
    unsigned count=rd(payload+12);
    level_vertex *vertices=calloc(count,sizeof(*vertices));assert(vertices);
    for(unsigned i=0;i<count;i++){vertices[i].x=i;vertices[i].y=20;vertices[i].z=30;}
    assert(world_texture_draw_actor(&m,&m,vertices,count)>0);
    assert(rf(actor_asset.bytes+actor_asset.vo+32)==1);
    assert(rf(actor_asset.bytes+actor_asset.vo+4)==20);
    assert(world_texture_draw_actor(&m,&m,vertices,count-1)==-1);
    for(unsigned i=0;i<PROP_ASSET_COUNT;i++){
        current=&prop_assets[i];assert(load_asset(0xCB4+i,argv[3+i])==0);
    }
    props_ready=1;
    unsigned types[]={PROP_CRATE_WOOD,PROP_CRATE_TNT,PROP_CRATE_NITRO,PROP_CRATE_CHECKPOINT,PROP_WUMPA,PROP_CRATE_SPRING,PROP_CRATE_BOUNCE,PROP_AKU_MASK};
    for(unsigned i=0;i<PROP_ASSET_COUNT;i++)assert(world_texture_draw_prop(types[i],&m,&m,1,2,3,0,24,24,24,0)>0);
    current=&prop_assets[6];unload_asset();current=&world_asset;
    assert(world_texture_draw_prop(PROP_CRATE_BOUNCE,&m,&m,0,0,0,0,24,24,24,0)==-1);
    current=&prop_assets[7];unload_asset();current=&world_asset;
    assert(world_texture_draw_prop(PROP_AKU_MASK,&m,&m,0,0,0,0,1,1,1,0)==-1);
    assert(world_texture_draw_prop(PROP_CRYSTAL,&m,&m,0,0,0,0,1,1,1,0)==-1);
    int before=draws;
    assert(world_texture_draw_prop(PROP_WUMPA,&m,&m,1,2,3,0,14,14,14,0.1f)==6);
    assert(draws==before+1 && selected_sprite_frame==-1);
    assert(world_texture_draw_prop(0,&m,&m,0,0,0,0,1,1,1,0)==-1);
    free(vertices);
    world_texture_exit();assert(live==0&&texture_live==0);
    world_texture_exit();
    puts("PASS world runtime: production loader, allocations, materials, draw dispatch, animated vertex upload, cleanup");
    return 0;
}
'''

def main():
    with tempfile.TemporaryDirectory(prefix='crash-world-runtime-')as folder:
        folder=Path(folder)
        (folder/'citro3d.h').write_text(SDK)
        (folder/'3ds.h').write_text('#include "citro3d.h"\n')
        (folder/'test.c').write_text(TEST)
        executable=folder/'test.exe'
        subprocess.run([sys.executable,'-m','ziglang','cc','-O2','-UNDEBUG','-Wall','-Wextra','-Werror','-I'+str(folder),'-I'+str(ROOT/'source'),str(folder/'test.c'),'-o',str(executable)],check=True)
        subprocess.run([str(executable),str(ROOT/'game_data/world/lv_0B.cwt'),str(ROOT/'game_data/characters/crash.cwt')]+[str(ROOT/f'game_data/characters/{name}.cwt')for name in ('wood','tnt','nitro','checkpoint','wumpa','spring','bounce','aku_mask')],check=True)

if __name__=='__main__':main()
