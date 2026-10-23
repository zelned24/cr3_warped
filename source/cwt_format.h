#ifndef CWT_FORMAT_H
#define CWT_FORMAT_H
#include <stdint.h>
#include <math.h>
#include <string.h>
#define CWT_BATCH_LIMIT 8192u
#define CWT_TEXTURE_LIMIT 2048u
static unsigned rd(const unsigned char *p){return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static float rf(const unsigned char *p){unsigned v=rd(p);float f;memcpy(&f,&v,4);return f;}
static int extent(unsigned offset,unsigned count,unsigned stride,unsigned size){return offset<=size && (uint64_t)count*stride<=size-offset;}

static int cwt_validate(const unsigned char *payload,unsigned size,unsigned id){
    if(!payload || size<64 || size>16u*1024u*1024u)return -1;
    if(memcmp(payload,"CWT1",4)||rd(payload+4)!=1||rd(payload+8)!=id||rd(payload+48)!=size)return -1;
    unsigned vertices=rd(payload+12),indices=rd(payload+16),nt=rd(payload+24);
    unsigned batch_count=rd(payload+20);unsigned vertex_offset=rd(payload+28);unsigned index_offset=rd(payload+32),batch_offset=rd(payload+36);
    unsigned tex_offset=rd(payload+40),pixels=rd(payload+44);
    if(!vertices||!indices||!batch_count||batch_count>CWT_BATCH_LIMIT||!nt||nt>CWT_TEXTURE_LIMIT)return -1;
    unsigned sprite_frames=rd(payload+52);
    if(sprite_frames && (id!=0xCB8 || sprite_frames!=batch_count || sprite_frames>64))return -1;
    if(!extent(vertex_offset,vertices,32,size)||!extent(index_offset,indices,2,size)||!extent(batch_offset,batch_count,56,size)||!extent(tex_offset,nt,32,size))return -1;
    if(batch_offset!=64||tex_offset<batch_offset+batch_count*56||vertex_offset<tex_offset+nt*32||index_offset<vertex_offset+(uint64_t)vertices*32||pixels<index_offset+(uint64_t)indices*2||pixels>size||vertex_offset%16||index_offset%16)return -1;
    if((uint64_t)vertices*32+(uint64_t)indices*2>8u*1024u*1024u||size-pixels>4u*1024u*1024u)return -1;
    for(unsigned i=0;i<vertices;i++)for(unsigned j=0;j<8;j++){float a=rf(payload+vertex_offset+i*32+j*4);if(!isfinite(a)||fabsf(a)>1e8f)return -1;}
    for(unsigned i=0;i<batch_count;i++){
        const unsigned char *b=payload+batch_offset+i*56;
        unsigned base=rd(b),vc=rd(b+4),first=rd(b+8),count=rd(b+12);
        if(!vc||vc>65535||count%3||base>vertices||vc>vertices-base||first>indices||count>indices-first||rd(b+16)>=nt||rd(b+20)>1)return -1;
        for(unsigned j=0;j<6;j++)if(!isfinite(rf(b+32+j*4)))return -1;
        for(unsigned j=0;j<3;j++)if(rf(b+32+j*4)>rf(b+44+j*4))return -1;
        for(unsigned j=0;j<count;j++){
            const unsigned char *p=payload+index_offset+(first+j)*2;
            if((unsigned)(p[0]|p[1]<<8)>=vc)return -1;
        }
    }
    unsigned cursor=pixels;
    for(unsigned i=0;i<nt;i++){
        const unsigned char *t=payload+tex_offset+i*32;
        unsigned w=rd(t),h=rd(t+4),off=rd(t+8),bytes=rd(t+12);
        if(w<8||w>1024||h<8||h>1024||(w&(w-1))||(h&(h-1))||bytes!=w*h*2||off!=cursor||!extent(off,bytes,1,size))return -1;
        cursor+=bytes;
    }
    if(cursor!=size)return -1;
    return 0;
}
#endif
