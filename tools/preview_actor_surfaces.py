"""Render the actual runtime CWT vertices/texels into a software review sheet."""
import struct
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
from export_world_textures import untile5551
from export_object_graphics import preview

ROOT=Path(__file__).resolve().parents[1]
NAMES=('crash','wood','tnt','nitro','checkpoint','wumpa','spring','bounce','aku_mask')

def render(data,size=144):
    nv,ni,nb,nt,vo,io,bo,to=struct.unpack_from('<8I',data,12)
    packed=np.frombuffer(data,dtype='<f4',count=nv*8,offset=vo).reshape(nv,8)
    vertices=[{'pos':v[:3].tolist(),'color':v[3:6].tolist(),'uv':v[6:].tolist()} for v in packed]
    indices=struct.unpack_from('<%dH'%ni,data,io)
    textures={}
    for i in range(nt):
        w,h,off,length=struct.unpack_from('<4I',data,to+i*32)
        # CWT uses normalized bottom-up UV, and the inverse returns top-down.
        pixels=np.frombuffer(untile5551(data[off:off+length],w,h),dtype=np.uint8).reshape(h,w,4)
        textures[i]=(pixels,(0,w-1,0,h-1))
    triangles=[]
    for i in range(nb):
        base,_,first,count,material=struct.unpack_from('<5I',data,bo+i*56)
        if data[52:56]!=bytes(4) and i>0:continue
        for j in range(first,first+count,3):
            ids=[base+indices[k] for k in range(j,j+3)]
            # Preview consumes texel coordinates; duplicate vertices per triangle
            # because separate batches can share their source corners.
            resolved=[]
            image=textures[material][0];h,w=image.shape[:2]
            for k in ids:
                v=vertices[k];uv=v['uv'];resolved.append(len(vertices))
                vertices.append({**v,'uv':[uv[0]*w-.5,(1-uv[1])*h-.5]})
            triangles.append({'indices':resolved,'uv_index':material,'tex_anim':False})
    return preview({'vertices':vertices,'triangles':triangles},textures,size)

def main():
    sheet=Image.new('RGB',(640,((len(NAMES)+3)//4)*176),(20,25,35));draw=ImageDraw.Draw(sheet)
    for i,name in enumerate(NAMES):
        x,y=i%4*160,i//4*176
        sheet.paste(render((ROOT/'game_data/characters'/f'{name}.cwt').read_bytes()),(x+8,y))
        draw.text((x+8,y+148),name,fill='white')
    output=ROOT/'capturas/runtime_actor_surfaces.png';sheet.save(output);print(output)

if __name__=='__main__':main()
