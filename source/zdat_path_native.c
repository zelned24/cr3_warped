#include "zdat_path_native.h"
#include "gool_atan.h"
#include <limits.h>
static int32_t sign(uint32_t v){return v<=INT32_MAX?(int32_t)v:-1-(int32_t)(UINT32_MAX-v);}
static uint32_t asr(uint32_t v,unsigned shift) {
    return (v>>shift)|((v&0x80000000u)?UINT32_MAX<<(32-shift):0);
}
static uint32_t magnitude(uint32_t v){return v&0x80000000u?0u-v:v;}
static uint32_t half_zero(uint32_t v){return asr(v+(v>>31),1);}
static int angle(gool_domain *domain,uint32_t address,uint32_t y,uint32_t x,int negate) {
    int32_t result;if(gool_atan2(sign(y),sign(x),&result))return ZDAT_PATH_ARITHMETIC;
    return gool_domain_write32(domain,address,negate?0u-(uint32_t)result:(uint32_t)result);
}
#define TRY(call) do{int error=(call);if(error)return error;}while(0)
static uint32_t root(uint32_t value) {
    uint32_t result=0,remainder=0;
    for(unsigned i=0;i<16;i++) {
        remainder|=value>>30;value<<=2;
        uint32_t trial=remainder-((result<<2)+1);result<<=1;
        if(sign(trial)>=0){result++;remainder=trial;}
        remainder<<=2;
    }
    return result;
}
static int projection(gool_domain *d,uint32_t node,uint32_t parameter,uint32_t count,uint32_t index,
                       uint32_t output,const uint32_t next[3],uint32_t *fraction,int *retry) {
    uint32_t start[3],object_x,object_z,width,flags;
    for(unsigned i=0;i<3;i++)TRY(gool_domain_read32(d,output+i*4,&start[i]));
    TRY(gool_domain_read32(d,node+0x60,&object_x));TRY(gool_domain_read32(d,node+0x68,&object_z));
    uint32_t dx=object_x-start[0],dz=object_z-start[2];
    int before_last=sign(asr(parameter,8))<(int32_t)(count-1);
    if(before_last) {
        TRY(gool_domain_read32(d,node+0x134,&width));
        if(sign(width<<1)<sign(magnitude(dx))||sign(width<<1)<sign(magnitude(dz))) {
            *retry=1;return 0;
        }
    }
    uint32_t vx=asr(next[0]-start[0],8),vz=asr(next[2]-start[2],8);
    uint32_t length=root(vx*vx+vz*vz),f=*fraction;
    if(!length) {
        TRY(gool_domain_read32(d,node+0xa8,&flags));TRY(gool_domain_write32(d,node+0xa8,flags|0x200u));
        uint32_t path=parameter&~255u;
        if(sign(f)>=255)path+=255;
        else if(sign(f)>=0)path+=f;
        TRY(gool_domain_write32(d,node+0xd4,path));TRY(gool_domain_write32(d,node+0xa0,0));
        return 0;
    }
    uint32_t dot=asr(dx,4)*asr(next[0]-start[0],4)+asr(dz,4)*asr(next[2]-start[2],4);
    int32_t distance=(int32_t)((int64_t)sign(dot)/(int32_t)length);
    f=(uint32_t)(distance/(int32_t)length);*fraction=f;
    if(sign(f)>=256&&before_last){*retry=1;return 0;}
    uint32_t along=asr((uint32_t)distance,4);
    uint32_t projected_x=start[0]+(uint32_t)((int64_t)sign(along*asr(next[0]-start[0],4))/(int32_t)length);
    uint32_t projected_z=start[2]+(uint32_t)((int64_t)sign(along*asr(next[2]-start[2],4))/(int32_t)length);
    TRY(gool_domain_read32(d,node+0x60,&object_x));TRY(gool_domain_read32(d,node+0x68,&object_z));
    uint32_t x=magnitude(projected_x-object_x),z=magnitude(projected_z-object_z);
    uint32_t lateral=sign(x)>sign(z)?x+half_zero(z):z+half_zero(x);
    TRY(gool_domain_write32(d,node+0xa0,lateral));
    TRY(gool_domain_read32(d,node+0xa0,&lateral));TRY(gool_domain_read32(d,node+0x134,&width));
    if(sign(width)<sign(lateral)||(sign(f)<0&&!index)||sign(f)>=256) {
        TRY(gool_domain_read32(d,node+0xa8,&flags));TRY(gool_domain_write32(d,node+0xa8,flags|0x200u));
    }
    /* Original20B88 rereads object and output coordinates after publishing
     * lateral distance. Products/subtractions retain low32 bits. */
    TRY(gool_domain_read32(d,output,&start[0]));TRY(gool_domain_read32(d,output+8,&start[2]));
    TRY(gool_domain_read32(d,node+0x60,&object_x));TRY(gool_domain_read32(d,node+0x68,&object_z));
    uint32_t side=(next[2]-start[2])*asr(object_x,8)-
                  (next[0]-start[0])*asr(object_z,8)-next[2]*asr(start[0],8)+next[0]*asr(start[2],8);
    if(sign(side)<0) {
        TRY(gool_domain_read32(d,node+0xa0,&lateral));TRY(gool_domain_write32(d,node+0xa0,0u-lateral));
    }
    uint32_t path=parameter&~255u;
    if(sign(f)>=255)path+=255;
    else if(sign(f)>=0)path+=f;
    TRY(gool_domain_write32(d,node+0xd4,path));return 0;
}
int zdat_path_native_position(zdat_native *s,uint32_t node,int32_t parameter,
                               uint32_t output,int32_t *result) {
    if(!s||!s->domain||!node||(node&3u)||!output||(output&3u)||!result||
       node>UINT32_MAX-0x138||output>UINT32_MAX-12)return ZDAT_NATIVE_INVALID;
    gool_domain *d=s->domain;uint32_t db,owner,type,offset_address,shift;
    TRY(gool_domain_read32(d,node+0xf0,&db));
    if(!db){*result=-14;return 0;}
    if((db&3u)||db>UINT32_MAX-8)return ZDAT_NATIVE_RANGE;
    TRY(gool_domain_read32(d,db+4,&owner));
    if(!owner||(owner&3u)||owner>UINT32_MAX-0x18)return ZDAT_NATIVE_RANGE;
    TRY(gool_domain_read32(d,owner+8,&type));
    if(type!=7)return ZDAT_PATH_UNSUPPORTED;
    TRY(gool_domain_read32(d,owner+0x14,&offset_address));
    zdat_native_value shift_value;
    TRY(zdat_native_select(s,db,0x30e,0,0,&shift_value));
    if(!shift_value.address)shift=2;
    else TRY(zdat_native_scalar(s,db,0x30e,&shift));
    zdat_native_value path;TRY(zdat_native_select(s,db,0x4b,0,0,&path));
    if(!path.address||!path.length||(path.format&31u)!=6u||path.stride!=6u)return ZDAT_NATIVE_RANGE;
    uint32_t count=path.length,attempts=0;
select_segment: ;
    if(attempts++>count)return ZDAT_NATIVE_CYCLE;
    uint32_t absolute=magnitude((uint32_t)parameter);
    int32_t selected=sign(asr(absolute,8));uint32_t fraction=(uint32_t)parameter&255u;
    if(selected<0||(uint32_t)selected>=count)return ZDAT_NATIVE_RANGE;
    if((uint32_t)selected==count-1&&selected){fraction+=256;selected--;}
    uint32_t motion;TRY(gool_domain_read32(d,node+0xac,&motion));
    uint64_t point_address=(uint64_t)path.address+(uint32_t)selected*6u;
    uint32_t point_bytes=count==1?6u:12u;
    if(point_address>path.end||point_bytes>path.end-point_address||
       !offset_address||(offset_address&3u)||offset_address>UINT32_MAX-12)return ZDAT_NATIVE_RANGE;
    uint32_t offset[3],position[3],next[3],delta[3];
    for(unsigned i=0;i<3;i++) {
        uint16_t point;TRY(gool_domain_read16(d,(uint32_t)point_address+i*2,&point));
        TRY(gool_domain_read32(d,offset_address+i*4,&offset[i]));
        uint32_t signed_point=point<32768u?point:0xffff0000u|point;
        position[i]=((signed_point<<(shift&31u))+offset[i])<<8;
    }
    for(unsigned i=0;i<3;i++)TRY(gool_domain_write32(d,output+i*4,position[i]));
    if(count==1){*result=1;return 0;}
    for(unsigned i=0;i<3;i++) {
        uint16_t point;TRY(gool_domain_read16(d,(uint32_t)point_address+6+i*2,&point));
        uint32_t signed_point=point<32768u?point:0xffff0000u|point;
        next[i]=((signed_point<<(shift&31u))+offset[i])<<8;
        delta[i]=next[i]-position[i];
    }
    if(motion&0x8000u) {
        int retry=0;
        TRY(projection(d,node,(uint32_t)parameter,count,(uint32_t)selected,output,next,&fraction,&retry));
        if(retry){parameter=sign(((uint32_t)selected+1u)<<8);goto select_segment;}
    }
    uint32_t progress,old_flags,flags;
    TRY(gool_domain_read32(d,node+0xf4,&progress));TRY(gool_domain_read32(d,node+0xa8,&old_flags));
    if(sign(progress)<0||sign(asr((uint32_t)parameter,8))>=(int32_t)(count-1))flags=old_flags|4u;
    else flags=old_flags&~4u;
    TRY(gool_domain_write32(d,node+0xa8,flags));
    uint32_t current;TRY(gool_domain_read32(d,node+0xa8,&current));
    if(((current^old_flags)&4u)&&!(current&0x10u))flags=current|0x10u;
    else {TRY(gool_domain_read32(d,node+0xa8,&flags));flags&=~0x10u;}
    TRY(gool_domain_write32(d,node+0xa8,flags));
    TRY(gool_domain_read32(d,node+0xac,&motion));
    if(motion&4u) {
        uint32_t dx=delta[0],dz=delta[2];
        if(motion&2u) {
            TRY(gool_domain_read32(d,node+0xa8,&flags));
            if(flags&4u){dx=0u-dx;dz=0u-dz;}
        }
        TRY(angle(d,node+0x94,dx,dz,0));
        TRY(gool_domain_read32(d,node+0xac,&motion));
        if(motion&0x800u) {
            TRY(angle(d,node+0x74,delta[0],delta[2],0));
            uint32_t x=magnitude(delta[0]),z=magnitude(delta[2]);
            uint32_t horizontal=sign(x)>sign(z)?x+half_zero(z):z+half_zero(x);
            TRY(angle(d,node+0x98,delta[1],horizontal,1));
        }
    } else if(motion&0x800u) {
        uint32_t x=magnitude(delta[0]),z=magnitude(delta[2]);
        uint32_t horizontal=sign(x)>sign(z)?x+half_zero(z):z+half_zero(x);
        TRY(angle(d,node+0x98,delta[1],horizontal,0));
        TRY(angle(d,node+0x74,0u-delta[0],0u-delta[2],0));
    }
    /* Native20E3C rereads output; preserve aliasing effects from the preceding
     * orientation writes before applying wrapped MULT/MFLO interpolation. */
    uint32_t increments[3];
    for(unsigned i=0;i<3;i++) {
        uint32_t now;TRY(gool_domain_read32(d,output+i*4,&now));
        increments[i]=asr((next[i]-now)*fraction,8);
    }
    for(unsigned i=0;i<3;i++) {
        uint32_t now;TRY(gool_domain_read32(d,output+i*4,&now));
        TRY(gool_domain_write32(d,output+i*4,now+increments[i]));
    }
    *result=(int32_t)count;return 0;
}
#undef TRY

int zdat_path_native_create(void *context,uint32_t node,uint32_t parameter,uint32_t output) {
    int32_t result;
    int error=zdat_path_native_position(context,node,sign(parameter),output,&result);
    if(error)return error;
    return result<0?result:0;
}
