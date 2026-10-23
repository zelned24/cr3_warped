"""Render the production bottom_ui.c mesh to a pixel-exact 320x240 PNG.

This preview uses the same glyphs, icons, panel vertices and colors as the
Nintendo 3DS build. Requires Pillow and the ziglang Python package.
"""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include "bottom_ui.h"
#include <stdio.h>
static level_vertex vertices[24576];
int main(int argc,char **argv) {
    if(argc!=2) return 1;
    bottom_ui_state state={
        .level_name="TOAD VILLAGE", .wumpa=86, .boxes=12, .boxes_total=42,
        .lives=4, .crystals=7, .gems=12, .relics=3, .powers=3, .completion=24
    };
    int count=bottom_ui_build(vertices,24576,&state);
    FILE *f=fopen(argv[1],"wb"); if(!f) return 2;
    if(fwrite(vertices,sizeof(level_vertex),count,f)!=(size_t)count) return 3;
    return fclose(f)!=0;
}
'''


def main():
    output=Path(sys.argv[1]) if len(sys.argv)>1 else ROOT / "capturas/bottom_ui.png"
    with tempfile.TemporaryDirectory(prefix="crash-ui-preview-") as folder:
        folder=Path(folder)
        source=folder / "preview.c"
        executable=folder / "preview.exe"
        mesh=folder / "mesh.bin"
        source.write_text(HARNESS)
        subprocess.run([sys.executable,"-m","ziglang","cc","-std=c11","-O2",
                        "-I"+str(ROOT / "source"),str(source),str(ROOT / "source/bottom_ui.c"),
                        "-o",str(executable)],check=True)
        subprocess.run([str(executable),str(mesh)],check=True)
        vertices=list(struct.iter_unpack("<6f",mesh.read_bytes()))
    image=Image.new("RGB",(320,240))
    draw=ImageDraw.Draw(image)
    # Sample triangle coverage at pixel centers, as the framebuffer rasterizer
    # does; Pillow's polygon edges include the right endpoint of rectangles.
    for i in range(0,len(vertices),3):
        triangle=vertices[i:i+3]
        points=[(v[0],240-v[1]) for v in triangle]
        color=tuple(max(0,min(255,round(c*255))) for c in triangle[0][3:6])
        minx=max(0,int(min(p[0] for p in points)))
        maxx=min(319,int(max(p[0] for p in points)))
        miny=max(0,int(min(p[1] for p in points)))
        maxy=min(239,int(max(p[1] for p in points)))
        for y in range(miny,maxy+1):
            for x in range(minx,maxx+1):
                signs=[]
                for j in range(3):
                    a,b=points[j],points[(j+1)%3]
                    signs.append((b[0]-a[0])*(y+.5-a[1])-(b[1]-a[1])*(x+.5-a[0]))
                if all(t>=0 for t in signs) or all(t<=0 for t in signs):
                    draw.point((x,y),fill=color)
    output.parent.mkdir(parents=True,exist_ok=True)
    image.save(output)
    print(f"{output}: 320x240, {len(vertices)} production vertices")


if __name__=="__main__":
    main()
