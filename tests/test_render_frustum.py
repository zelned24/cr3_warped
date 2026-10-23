"""Exercise production homogeneous culling and original exported box contracts."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TEST = r'''
#include <assert.h>
#include <stdio.h>
#include "render_frustum.h"
int main(void) {
    float projection[4][4]={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
    float view[4][4]={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
    double clip[4][4];
    float low[3]={-.5f,-.5f,-.5f},high[3]={.5f,.5f,.5f};
    render_frustum_matrix(projection,view,clip);
    assert(render_frustum_visible(clip,low,high));
    low[0]=2;high[0]=3;assert(!render_frustum_visible(clip,low,high));
    low[0]=.5f;assert(render_frustum_visible(clip,low,high));
    low[0]=-3;high[0]=3;assert(render_frustum_visible(clip,low,high));
    low[0]=1;assert(render_frustum_visible(clip,low,high));
    low[0]=1.000001f;assert(render_frustum_visible(clip,low,high));
    low[0]=NAN;assert(render_frustum_visible(clip,low,high));
    low[0]=2;high[0]=3;clip[0][0]=INFINITY;
    assert(render_frustum_visible(clip,low,high));
    /* Named matrix rows: translation occurs before the tilted projection. */
    view[0][3]=-2.5f;projection[0][0]=0;projection[0][1]=1;
    projection[1][0]=-1;projection[1][1]=0;
    render_frustum_matrix(projection,view,clip);
    assert(render_frustum_visible(clip,low,high));
    view[0][3]=0;render_frustum_matrix(projection,view,clip);
    assert(!render_frustum_visible(clip,low,high));
    /* Perspective w=-z: a box wholly behind the eye is outside. */
    projection[3][2]=-1;projection[3][3]=0;
    low[0]=low[1]=-.1f;high[0]=high[1]=.1f;low[2]=1;high[2]=2;
    render_frustum_matrix(projection,view,clip);
    assert(!render_frustum_visible(clip,low,high));
    low[2]=-2;assert(render_frustum_visible(clip,low,high));
    /* No speculative near/far plane rejection. */
    projection[3][2]=0;projection[3][3]=1;
    low[2]=100000;high[2]=200000;
    render_frustum_matrix(projection,view,clip);
    assert(render_frustum_visible(clip,low,high));
    puts("PASS frustum: homogeneous sides, row convention, tilt, straddles, behind-eye and nonfinite fallback");
}
'''


def main():
    count = 0
    for directory in ("world", "characters"):
        for path in (ROOT / "game_data" / directory).glob("*.cwt"):
            if path.name == "crash.cwt":
                continue  # In-place SVTX morphing invalidates its exported bounds.
            data = path.read_bytes()
            vertex_offset = struct.unpack_from("<I", data, 28)[0]
            batches = struct.unpack_from("<I", data, 20)[0]
            batch_offset = struct.unpack_from("<I", data, 36)[0]
            for index in range(batches):
                offset = batch_offset + index * 56
                base, vertices = struct.unpack_from("<II", data, offset)
                bounds = struct.unpack_from("<6f", data, offset + 32)
                for vertex in range(base, base + vertices):
                    position = struct.unpack_from("<3f", data, vertex_offset + vertex * 32)
                    assert all(bounds[a] - 1e-3 <= position[a] <= bounds[a + 3] + 1e-3
                               for a in range(3)), (path.name, index, position, bounds)
                count += 1
    with tempfile.TemporaryDirectory(prefix="crash-frustum-") as directory:
        source = Path(directory) / "test.c"
        executable = Path(directory) / "test.exe"
        source.write_text(TEST)
        subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O3",
                        "-ffast-math", "-fno-finite-math-only", "-Wall", "-Wextra",
                        "-Werror", "-UNDEBUG", "-I" + str(ROOT / "source"), str(source),
                        "-lm", "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
    print(f"PASS frustum bounds: {count} original world/prop batches contain their vertices")


if __name__ == "__main__":
    main()
