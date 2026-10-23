"""Exercise production collision with real Toad Village and geometric fixtures."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from test_level_loader import HARNESS

TEST = HARNESS[:HARNESS.index('int main(')] + r'''
static void synthetic(void) {
    static level_vertex verts[12];
    /* A real vertical wall at x=100, z=-200..200, y=0..200. */
    float xyz[6][3]={{100,0,-200},{100,200,-200},{100,0,200},
                     {100,0,200},{100,200,-200},{100,200,200}};
    for(int i=0;i<6;i++){verts[i].x=xyz[i][0];verts[i].y=xyz[i][1];verts[i].z=xyz[i][2];}
    s_vbo=verts;s_model_count=1;
    memset(s_models,0,sizeof(s_models));
    s_models[0].vert_count=6;
    s_models[0].minx=s_models[0].maxx=100;
    s_models[0].miny=0;s_models[0].maxy=200;
    s_models[0].minz=-200;s_models[0].maxz=200;
    float x=0,z=0;
    assert(level_move_horizontal(&x,&z,0,300,70,10,45,15));
    assert(x<90.1f && x>89.0f && fabsf(z-70)<0.2f); /* no tunnelling; slides */
    x=200;z=0;
    assert(level_move_horizontal(&x,&z,0,-300,0,10,45,15));
    assert(x>109.9f && x<111); /* opposite winding side equally solid */
    x=0;z=0;
    assert(!level_move_horizontal(&x,&z,220,200,0,10,45,15));
    assert(fabsf(x-200)<0.1f); /* above wall stays open */
    s_models[0].is_skybox=1;x=0;z=0;
    assert(!level_move_horizontal(&x,&z,0,200,0,10,45,15));
    assert(fabsf(x-200)<0.1f); /* never collide with sky */
    s_models[0].is_skybox=0;x=0;z=300;
    assert(!level_move_horizontal(&x,&z,0,200,0,10,45,15));
    assert(level_ground_height(x,z,1000)<-999999); /* no invented perimeter/floor */
    s_vbo=NULL;s_model_count=0;
}
int main(int argc,char **argv) {
    assert(argc==2);
    synthetic();
    uint32_t size;
    uint8_t *real=read_file(argv[1],&size);
    active_blob=real;active_size=size;s_parsed_blob=NULL;s_entry_count=0;
    assert(level_load(11)==0);
    float x,y,z;
    assert(level_spawn_point(&x,&y,&z)==0);
    y=level_ground_height(x,z,y+1);
    assert(fabsf(y-188.5815f)<0.01f);
    int contacts=0;
    /* Initial straight road: 300 original world units, sampled densely.
       Test both physics frame displacements, not integer-rounded floor values. */
    for(int i=0;i<60;i++) {
        contacts+=level_move_horizontal(&x,&z,y,0,-5,10,45,15);
        float h=level_ground_height(x,z,y+16);
        assert(h>-999999 && fabsf(h-y)<16);
        y=h;
    }
    printf("Toad first road: x=%.3f y=%.3f z=%.3f, contacts=%d\n",x,y,z,contacts);
    /* Sweep sideways from the walkable center until the original corridor's
       stone wall stops the body, rather than walking through it. */
    float start=x;
    int hit=level_move_horizontal(&x,&z,y,500,0,10,45,15);
    printf("Toad wall sweep: x %.3f -> %.3f, hit=%d\n",start,x,hit);
    assert(hit && x<start+499);
    assert(level_ground_height(NAN,z,y)==-1000000.0f);
    level_unload();free(real);
    puts("PASS: wall sweep, slide, winding, height, sky exclusion, original road and corridor");
    return 0;
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix='crash-collision-') as directory:
        tmp = Path(directory)
        (tmp / '3ds.h').write_text('#include <stdlib.h>\nstatic int force_alloc_fail;\n'
            'static inline void *linearAlloc(size_t n){return force_alloc_fail?NULL:malloc(n);}\n'
            'static inline void linearFree(void*p){free(p);}\n')
        (tmp / 'citro3d.h').write_text('#define GPU_TRIANGLES 0\n'
            'static inline void C3D_DrawArrays(int p,int f,int n){(void)p;(void)f;(void)n;}\n')
        (tmp / 'test.c').write_text(TEST)
        exe = tmp / 'test.exe'
        subprocess.run([sys.executable, '-m', 'ziglang', 'cc', '-std=c11', '-Wall',
            '-Wextra', '-Werror', '-O2', '-UNDEBUG', '-DLEVELS_FROM_SD', '-I'+str(tmp),
            '-I'+str(ROOT / 'source'), str(tmp/'test.c'), '-lm', '-o', str(exe)], check=True)
        subprocess.run([str(exe), str(ROOT/'game_data'/'levels.bin')], check=True)


if __name__ == '__main__':
    main()
