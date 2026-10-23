"""Render the exact CWT vertices/indices/5551 textures in software for visual QA.
Not a screenshot of PICA200 or original gameplay.
"""
from pathlib import Path
import argparse
import math
import struct
import numpy as np
from PIL import Image
from export_world_textures import untile5551
ROOT=Path(__file__).resolve().parents[1]

def render(level=11):
    data=(ROOT/f'game_data/world/lv_{level:02X}.cwt').read_bytes()
    header=struct.unpack_from('<4s15I',data)
    _,version,lid,nv,ni,nb,nt,vo,io,bo,to,po,*_=header
    vertices=np.frombuffer(data,dtype='<f4',count=nv*8,offset=vo).reshape(-1,8)
    indices=np.frombuffer(data,dtype='<u2',count=ni,offset=io)
    textures=[]
    for i in range(nt):
        w,h,off,size,*_=struct.unpack_from('<8I',data,to+i*32)
        textures.append(np.frombuffer(untile5551(data[off:off+size],w,h),dtype=np.uint8).reshape(h,w,4))
    batches=[struct.unpack_from('<8I6f',data,bo+i*56) for i in range(nb)]
    # Same original CWT character surface used by the runtime, idle pose at spawn.
    actor=(ROOT/'game_data/characters/crash.cwt').read_bytes()
    ah=struct.unpack_from('<4s15I',actor)
    av=np.frombuffer(actor,dtype='<f4',count=ah[3]*8,offset=ah[7]).reshape(-1,8).copy()
    av[:,0]=-av[:,0]+3034.1648
    av[:,1]+=189
    av[:,2]=-av[:,2]+25450
    ai=np.frombuffer(actor,dtype='<u2',count=ah[4],offset=ah[8])
    for i in range(ah[6]):
        w,h,off,size,*_=struct.unpack_from('<8I',actor,ah[10]+i*32)
        textures.append(np.frombuffer(untile5551(actor[off:off+size],w,h),dtype=np.uint8).reshape(h,w,4))
    for i in range(ah[5]):
        b=list(struct.unpack_from('<8I6f',actor,ah[9]+i*56))
        b[0]+=nv;b[2]+=ni;b[4]+=nt
        batches.append(b)
    vertices=np.concatenate((vertices,av));indices=np.concatenate((indices,ai))
    # NSD checkpoint is independently tested; the floor beneath is Y=189.
    target=np.array((3034.1648,213,25450))
    pitch=-.38;distance=140
    eye=target+np.array((0,-math.sin(pitch)*distance,math.cos(pitch)*distance))
    forward=(target-eye);forward/=np.linalg.norm(forward)
    right=np.cross(forward,np.array((0,1,0)));right/=np.linalg.norm(right)
    up=np.cross(right,forward)
    image=np.full((240,400,3),(26,32,48),dtype=np.uint8)
    depth=np.full((240,400),np.inf)
    focal=120/math.tan(math.radians(60)/2)
    triangles=0
    for base,vc,first,count,tex,sky,*_ in batches:
        if sky:continue
        vv=vertices[base:base+vc];p=vv[:,:3]-eye
        z=p@forward
        screen=np.stack((200+(p@right)*focal/z,120-(p@up)*focal/z),axis=1)
        for tri in indices[first:first+count].reshape(-1,3):
            zz=z[tri]
            if zz.min()<8:continue # close-plane clipping omitted in this offline QA renderer
            pts=screen[tri]
            lo=np.maximum(np.floor(pts.min(axis=0)).astype(int),(0,0))
            hi=np.minimum(np.ceil(pts.max(axis=0)).astype(int),(399,239))
            if np.any(lo>hi):continue
            a,b,c=pts
            den=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
            if abs(den)<1e-8:continue
            xx,yy=np.meshgrid(np.arange(lo[0],hi[0]+1)+.5,np.arange(lo[1],hi[1]+1)+.5)
            u=((b[1]-c[1])*(xx-c[0])+(c[0]-b[0])*(yy-c[1]))/den
            v=((c[1]-a[1])*(xx-c[0])+(a[0]-c[0])*(yy-c[1]))/den
            w=1-u-v
            mask=(u>=0)&(v>=0)&(w>=0)
            inv=u/zz[0]+v/zz[1]+w/zz[2]
            dd=1/np.maximum(inv,1e-15)
            ds=depth[lo[1]:hi[1]+1,lo[0]:hi[0]+1]
            mask &= dd<ds
            if not mask.any():continue
            weights=np.stack((u/zz[0],v/zz[1],w/zz[2]),axis=-1)/inv[:,:,None]
            values=weights@vv[tri,3:]
            texture=textures[tex];h,w=texture.shape[:2]
            tx=np.clip((values[:,:,3]*w).astype(int),0,w-1)
            ty=np.clip(((1-values[:,:,4])*h).astype(int),0,h-1)
            sampled=texture[ty,tx]
            mask &= sampled[:,:,3]>0
            colors=np.clip(sampled[:,:,:3]*values[:,:,:3]*2,0,255).astype(np.uint8)
            out=image[lo[1]:hi[1]+1,lo[0]:hi[0]+1]
            out[mask]=colors[mask];ds[mask]=dd[mask];triangles+=1
    output=ROOT/'capturas/toad_textured_software.png'
    Image.fromarray(image).save(output)
    print(str(output),triangles,'rasterized triangles')

if __name__=='__main__':render()
