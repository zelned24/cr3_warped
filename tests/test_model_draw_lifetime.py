"""Exercise the production PICA queue against an asynchronous hardware boundary.

GPU/cache entry points record ownership; this does not emulate PICA rasterization.
"""
from pathlib import Path
import subprocess
import sys
import tempfile
from test_world_runtime import SDK
from host_native_sources import all_sources

ROOT = Path(__file__).resolve().parents[1]


def sdk():
    source = SDK.replace('static void shaderProgramInit(shaderProgram_s*p){memset(p,0,sizeof(*p));}',
                         'static int shaderProgramInit(shaderProgram_s*p){memset(p,0,sizeof(*p));return 0;}')
    source = source.replace('static void shaderProgramSetVsh(shaderProgram_s*p,void*v){p->vertexShader=v;}',
                            'static int shaderProgramSetVsh(shaderProgram_s*p,void*v){p->vertexShader=v;return 0;}')
    source = source.replace('static void shaderProgramFree(shaderProgram_s*p){(void)p;}',
                            'static int shaderProgramFree(shaderProgram_s*p){(void)p;return 0;}')
    source = source.replace('static void BufInfo_Add(', 'static int BufInfo_Add(')
    source = source.replace('(void)c;assert(d);}', '(void)c;assert(d);return 0;}')
    extra = r'''
#define R_FAILED(value) ((value)<0)
typedef int GPU_CULLMODE;
typedef int GPU_TESTFUNC;
typedef int GPU_WRITEMASK;
typedef int GPU_BLENDEQUATION;
typedef int GPU_BLENDFACTOR;
enum {GPU_CULL_BACK_CCW=40,GPU_GEQUAL=40,GPU_BLEND_MAX=40,
      GPU_SRC_ALPHA_SATURATE=40,GPU_REPLACE=41,GPU_TEVSCALE_1=42,GPU_TEVSCALE_4=43};
static int gpu_pending,frame_open,reject_barrier,barriers,drawn_vertices;
static void C3D_FrameSync(void) { /* VBlank deliberately leaves GPU pending. */ }
static int C3D_FrameBegin(int flags) {
    assert(flags==0);
    if(frame_open||reject_barrier)return 0;
    gpu_pending=0;frame_open=1;barriers++;return 1;
}
static void C3D_FrameEnd(int flags) {assert(!flags&&frame_open);frame_open=0;}
static void C3D_BlendingColor(uint32_t color) {(void)color;}
static void C3D_DrawArrays(int mode,int first,int count) {
    assert(frame_open&&mode==GPU_TRIANGLES&&first==0&&count>0&&count%3==0);
    gpu_pending=1;draws++;drawn_vertices+=count;
}
'''
    return source.replace('#endif', extra + '\n#endif')

HARNESS = r'''
#include <stdio.h>
#include "model_draw.c"
const unsigned char world_shader_shbin[4]={0};
const unsigned int world_shader_shbin_size=4;
static int references,acquires,releases;
static C3D_Tex resident;
int model_texture_cache_acquire(model_texture_cache *cache,model_texture_handle handle,
        C3D_Tex **texture,model_texture_cache_lease *lease) {
    assert(cache&&handle.generation==1&&handle.slot==0);
    *texture=&resident;
    *lease=(model_texture_cache_lease){.handle=handle,.texture={.generation=1}};
    references++;acquires++;return 0;
}
int model_texture_cache_release(model_texture_cache *cache,model_texture_cache_lease lease) {
    assert(cache&&lease.texture.generation==1&&references>0&&!gpu_pending&&!frame_open);
    references--;releases++;return 0;
}
int model_texture_cache_uv(const model_texture_cache *cache,model_texture_cache_lease lease,
        uint8_t u,uint8_t v,float *s,float *t) {
    (void)cache;(void)lease;(void)u;(void)v;(void)s;(void)t;
    assert(!"UV conversion is outside the queue lifetime harness");return MODEL_TEXTURE_CACHE_INVALID;
}
int main(void) {
    model_draw owner={0};model_texture_cache cache={0};
    resident.data=&resident;
    model_texture_handle texture={.generation=1};
    model_draw_state state={.cull=GPU_CULL_NONE,.alpha_test=GPU_ALWAYS,
        .depth_test=GPU_ALWAYS,.write_mask=GPU_WRITE_ALL,.color_equation=GPU_BLEND_ADD,
        .alpha_equation=GPU_BLEND_ADD,.source_color=GPU_ONE,.destination_color=GPU_ZERO,
        .source_alpha=GPU_ONE,.destination_alpha=GPU_ZERO,.rgb_scale=1};
    model_draw_vertex vertices[3]={{.x=-0.5f,.y=-0.5f},{.x=0.5f,.y=-0.5f},{.y=0.5f}};
    C3D_Mtx identity;Mtx_Identity(&identity);
    assert(model_draw_init(&owner)==0&&live==1);
    assert(model_draw_begin(&owner)==0);
    assert(model_draw_record(&owner,vertices,3,&identity,&identity,&state,&cache,texture)==0);
    assert(model_draw_record(&owner,vertices,3,&identity,&identity,&state,&cache,texture)==0);
    assert(owner.batch_count==1&&owner.vertex_count==6&&references==1&&acquires==2&&releases==1);
    state.alpha_reference=1;
    assert(model_draw_record(&owner,vertices,3,&identity,&identity,&state,&cache,texture)==0);
    assert(owner.batch_count==2&&references==2);
    assert(model_draw_submit(&owner,0)==1&&owner.recording&&draws==0);
    assert(C3D_FrameBegin(0));
    assert(model_draw_submit(&owner,1)==1&&owner.sealed&&owner.cursor==1&&draws==1);
    assert(model_draw_submit(&owner,1)==0&&owner.cursor==2&&draws==2&&drawn_vertices==9);
    assert(model_draw_submit(&owner,1)==0&&draws==2); /* No replay. */
    assert(model_draw_retire(&owner)==MODEL_DRAW_BUSY&&references==2&&owner.submitted);
    C3D_FrameEnd(0);
    C3D_FrameSync();assert(gpu_pending);
    reject_barrier=1;
    assert(model_draw_retire(&owner)==MODEL_DRAW_BUSY&&references==2&&owner.vertex_count==9);
    assert(model_draw_begin(&owner)==MODEL_DRAW_BUSY);
    reject_barrier=0;
    assert(model_draw_retire(&owner)==0&&!gpu_pending&&!frame_open&&references==0);
    assert(owner.vertex_count==0&&barriers==2);
    /* Cancellation before submission needs no GPU barrier and returns leases. */
    assert(model_draw_begin(&owner)==0);
    assert(model_draw_record(&owner,vertices,3,&identity,&identity,&state,&cache,texture)==0);
    int prior=barriers;
    assert(model_draw_retire(&owner)==0&&references==0&&barriers==prior);
    /* Fully culled batches must not mark work submitted. */
    for(unsigned i=0;i<3;i++)vertices[i].x=100;
    assert(model_draw_begin(&owner)==0);
    assert(model_draw_record(&owner,vertices,3,&identity,&identity,&state,&cache,texture)==0);
    assert(C3D_FrameBegin(0));
    assert(model_draw_submit(&owner,1)==0&&!owner.submitted&&draws==2);
    C3D_FrameEnd(0);prior=barriers;
    assert(model_draw_retire(&owner)==0&&references==0&&barriers==prior);
    /* The exact pool limit and invalid data must reject atomically. */
    static model_draw_vertex full[MODEL_DRAW_VERTICES];
    assert(model_draw_begin(&owner)==0);
    uint32_t nan_bits=0x7fc00000u;
    memcpy(&vertices[0].x,&nan_bits,sizeof(nan_bits));
    assert(model_draw_record(&owner,vertices,3,&identity,&identity,&state,&cache,texture)==MODEL_DRAW_INVALID);
    assert(!owner.vertex_count&&!owner.batch_count&&!references);
    assert(model_draw_record(&owner,full,MODEL_DRAW_VERTICES,&identity,&identity,&state,&cache,texture)==0);
    assert(owner.vertex_count==24999&&references==1);
    assert(model_draw_record(&owner,full,3,&identity,&identity,&state,&cache,texture)==MODEL_DRAW_CAPACITY);
    assert(owner.vertex_count==24999&&owner.batch_count==1&&references==1);
    assert(model_draw_retire(&owner)==0&&references==0);
    assert(model_draw_close(&owner)==0&&live==0&&!owner.initialized);
    assert(acquires==releases);
    puts("PASS model draw lifetime: batch merge, continuation, refused/empty barrier, no replay, culling and lease retirement");
    return 0;
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix='crash-model-draw-') as temporary:
        folder=Path(temporary)
        (folder/'citro3d.h').write_text(sdk())
        (folder/'3ds.h').write_text('#include "citro3d.h"\n')
        (folder/'probe.c').write_text(HARNESS)
        executable=folder/'probe.exe'
        sources=[str(ROOT/'source'/name) for name in ('model_pose.c','model_vertices.c',
                 'model_triangles.c','model_clip.c','model_project.c')]
        command=[sys.executable,'-m','ziglang','cc','-O3','-ffast-math',
                 '-fno-finite-math-only','-UNDEBUG',
                 '-I'+str(folder),'-I'+str(ROOT/'source'),
                 str(folder/'probe.c'),*sources,*all_sources(),'-o',str(executable)]
        subprocess.run(command,check=True)
        subprocess.run([str(executable)],check=True)
        # The same assertions must reject treating VBlank as queue completion.
        production=(ROOT/'source/model_draw.c').read_text()
        gate='if(!C3D_FrameBegin(0))return MODEL_DRAW_BUSY;\n        C3D_FrameEnd(0);'
        if production.count(gate)!=1:
            raise AssertionError('GPU retirement barrier changed; review the negative control')
        (folder/'unsafe_model_draw.c').write_text(production.replace(gate,'C3D_FrameSync();'))
        (folder/'probe.c').write_text(HARNESS.replace('#include "model_draw.c"',
                                                   '#include "unsafe_model_draw.c"'))
        subprocess.run(command,check=True)
        rejected=subprocess.run([str(executable)],capture_output=True)
        if rejected.returncode==0:
            raise AssertionError('Lifetime harness accepted the unsafe VBlank retirement mutation')
        print('PASS negative control: VBlank-only retirement rejected')


if __name__=='__main__':
    main()
