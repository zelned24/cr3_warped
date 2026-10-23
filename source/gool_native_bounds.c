#include "gool_native_bounds.h"
#include "gool_native_matrix.h"
static int32_t sign(uint32_t value) {
    return value<=INT32_MAX?(int32_t)value:-1-(int32_t)(UINT32_MAX-value);
}
int gool_native_bounds_contains(gool_domain *domain,uint32_t point,uint32_t box,int *contains) {
    if(!domain||!point||!box||((point|box)&3u)||!contains||
       point>UINT32_MAX-8u||box>UINT32_MAX-0x14u)return GOOL_DOMAIN_INVALID;
    uint32_t position[3];
    for(uint32_t i=0;i<3;i++) {
        uint32_t lower;
        int error=gool_domain_read32(domain,point+i*4u,&position[i]);if(error)return error;
        error=gool_domain_read32(domain,box+i*4u,&lower);if(error)return error;
        if(sign(position[i])<sign(lower)){*contains=0;return 0;}
    }
    for(uint32_t i=0;i<3;i++) {
        uint32_t upper;int error=gool_domain_read32(domain,box+0xcu+i*4u,&upper);if(error)return error;
        if(sign(position[i])>=sign(upper)){*contains=0;return 0;}
    }
    *contains=1;return 0;
}
#define CACHE_POINTER 0x80068ef0u
#define CACHE_COUNT 0x80068ef4u
#define CACHE_TABLE 0x80069228u
#define CACHE_START 0x80069348u
#define CACHE_END 0x80069c48u
static uint32_t shift4(uint32_t value) {
    return (value>>4)|((value&0x80000000u)?0xf0000000u:0);
}
static uint32_t shift(uint32_t value,unsigned count) {
    return (value>>count)|((value&0x80000000u)?UINT32_MAX<<(32u-count):0);
}
int gool_native_bounds_transform(gool_domain *domain,uint32_t output,uint32_t box,
                                 uint32_t matrix,uint32_t translation) {
    if(!domain||!output||!box||!matrix||!translation||
       ((output|box|matrix|translation)&3u)||output>UINT32_MAX-0x14u||
       box>UINT32_MAX-0x14u||matrix>UINT32_MAX-0x10u||translation>UINT32_MAX-8u)
        return GOOL_DOMAIN_INVALID;
    uint32_t center[3],extent[3],half[3][3],world[3];int error;
    for(uint32_t i=0;i<3;i++) {
        uint32_t lower,upper;
        error=gool_domain_read32(domain,box+i*4u,&lower);if(error)return error;
        error=gool_domain_read32(domain,box+0xcu+i*4u,&upper);if(error)return error;
        uint32_t midpoint=shift(lower+upper,1);
        if(midpoint&0x80000000u)midpoint+=15u;
        center[i]=shift4(midpoint);
    }
    /* Native18DD4 is CPU MULT/MFLO, not an unbounded dot or GTE IR clamp. */
    for(uint32_t row=0;row<3;row++) {
        uint32_t sum=0;
        for(uint32_t column=0;column<3;column++) {
            uint16_t raw;error=gool_domain_read16(domain,matrix+row*6u+column*2u,&raw);if(error)return error;
            int32_t coefficient=raw<0x8000u?(int32_t)raw:(int32_t)raw-65536;
            sum+=(uint32_t)coefficient*center[column];
        }
        world[row]=shift(sum,12)<<4;
    }
    for(uint32_t i=0;i<3;i++) {
        uint32_t position;error=gool_domain_read32(domain,translation+i*4u,&position);if(error)return error;
        world[i]+=position;
    }
    for(uint32_t column=0;column<3;column++) {
        uint32_t lower,upper;
        error=gool_domain_read32(domain,box+0xcu+column*4u,&upper);if(error)return error;
        error=gool_domain_read32(domain,box+column*4u,&lower);if(error)return error;
        uint32_t width=shift(upper-lower,2);
        for(uint32_t row=0;row<3;row++) {
            uint16_t raw;error=gool_domain_read16(domain,matrix+row*6u+column*2u,&raw);if(error)return error;
            int32_t coefficient=raw<0x8000u?(int32_t)raw:(int32_t)raw-65536;
            uint32_t product=shift(width*(uint32_t)coefficient,12);
            half[row][column]=(product&0x80000000u)?0u-product:product;
        }
    }
    for(uint32_t i=0;i<3;i++)extent[i]=(half[i][0]+half[i][1]+half[i][2])<<1;
    for(uint32_t i=0;i<3;i++) {
        error=gool_domain_write32(domain,output+0xcu+i*4u,world[i]+extent[i]);if(error)return error;
        error=gool_domain_write32(domain,output+i*4u,world[i]-extent[i]);if(error)return error;
    }
    return 0;
}
int gool_native_bounds_reset(gool_domain *domain) {
    if(!domain)return GOOL_DOMAIN_INVALID;
    int error=gool_domain_write32(domain,CACHE_POINTER,CACHE_START);if(error)return error;
    return gool_domain_write32(domain,CACHE_COUNT,0);
}
int gool_native_bounds_pop(gool_domain *domain) {
    if(!domain)return GOOL_DOMAIN_INVALID;
    uint32_t count,pointer,record,flags;
    int error=gool_domain_read32(domain,CACHE_COUNT,&count);if(error)return error;
    if(!count)return 0;
    if(count>72u)return GOOL_DOMAIN_INVALID;
    error=gool_domain_read32(domain,CACHE_TABLE+(count-1u)*4u,&record);if(error)return error;
    if(record<CACHE_START||record>CACHE_END-0x20u||(record&3u))return GOOL_DOMAIN_INVALID;
    error=gool_domain_read32(domain,record,&flags);if(error)return error;
    error=gool_domain_read32(domain,CACHE_POINTER,&pointer);if(error)return error;
    uint32_t size=(flags&7u)==0?0x20u:(flags&7u)==1?0x44u:0;
    if(pointer<CACHE_START||pointer>CACHE_END||(pointer&3u)||pointer-CACHE_START<size)
        return GOOL_DOMAIN_INVALID;
    error=gool_domain_write32(domain,CACHE_COUNT,count-1u);if(error)return error;
    return gool_domain_write32(domain,CACHE_POINTER,pointer-size);
}
static int reserve(gool_domain *domain,uint32_t node,uint32_t kind,uint32_t *record) {
    uint32_t count,pointer,size=kind?0x44u:0x20u;
    int error=gool_domain_read32(domain,CACHE_COUNT,&count);if(error)return error;
    error=gool_domain_read32(domain,CACHE_POINTER,&pointer);if(error)return error;
    if(count>72u||pointer<CACHE_START||pointer>CACHE_END||(pointer&3u))return GOOL_DOMAIN_INVALID;
    if(count==72u||pointer-CACHE_START>=0x900u) {
        error=gool_native_bounds_pop(domain);if(error)return error;
        error=gool_domain_read32(domain,CACHE_COUNT,&count);if(error)return error;
        error=gool_domain_read32(domain,CACHE_POINTER,&pointer);if(error)return error;
    }
    if(count>=72u||pointer>CACHE_END-size)return GOOL_DOMAIN_FULL;
    error=gool_domain_write32(domain,CACHE_POINTER,pointer+size);if(error)return error;
    error=gool_domain_write32(domain,CACHE_TABLE+count*4u,pointer);if(error)return error;
    error=gool_domain_write32(domain,CACHE_COUNT,count+1u);if(error)return error;
    error=gool_domain_write32(domain,pointer+4u,node);if(error)return error;
    error=gool_domain_write32(domain,pointer,kind);if(error)return error;
    *record=pointer;return 0;
}
int gool_native_bounds_fallback(gool_domain *domain,uint32_t node,uint32_t *record) {
    if(!domain||!node||(node&3u)||node>UINT32_MAX-0xe8u||!record)return GOOL_DOMAIN_INVALID;
    uint32_t model,pointer;uint8_t type;
    int error=gool_domain_read32(domain,node+0xe8u,&model);if(error)return error;
    if(!model)return GOOL_DOMAIN_INVALID;
    error=gool_domain_read8(domain,model,&type);if(error)return error;
    if(type==1)return GOOL_DOMAIN_UNBOUND;
    error=reserve(domain,node,0,&pointer);if(error)return error;
    /* Native X uses wrappedABS(scaleX), while Y/Z retain signed scales.
     * Each half extent is the low32 product200*scale followed by SRA4.
     * Preserve the repeated source reads and store order for aliased domains. */
    for(uint32_t axis=0;axis<3;axis++)for(uint32_t upper=0;upper<2;upper++) {
        uint32_t scale,position;
        error=gool_domain_read32(domain,node+0x78u+axis*4u,&scale);if(error)return error;
        if(!axis&&(scale&0x80000000u))scale=0u-scale;
        uint32_t half=shift4(scale*200u);
        error=gool_domain_read32(domain,node+0x60u+axis*4u,&position);if(error)return error;
        uint32_t value=upper?position+half:position-half;
        error=gool_domain_write32(domain,pointer+8u+axis*4u+upper*12u,value);if(error)return error;
    }
    *record=pointer;return 0;
}
static int nonoriented(gool_domain *domain,uint32_t node,uint32_t hitbox,uint32_t *record) {
    uint32_t pointer,yaw,pitch,sx,center[3],scaled[6],scale[3];int error;
    error=reserve(domain,node,0,&pointer);if(error)return error;
    error=gool_domain_read32(domain,node+0x70u,&yaw);if(error)return error;
    error=gool_domain_read32(domain,node+0x6cu,&pitch);if(error)return error;
    error=gool_domain_read32(domain,node+0x78u,&sx);if(error)return error;
    int identity=!yaw&&!pitch&&sx==0x1000u;
    if(identity) {
        for(uint32_t i=0;i<3;i++) {
            uint32_t position,local;
            error=gool_domain_read32(domain,node+0x60u+i*4u,&position);if(error)return error;
            error=gool_domain_read32(domain,hitbox+4u+i*4u,&local);if(error)return error;
            center[i]=position+local;
        }
    } else {
        int32_t matrix[3][3];uint32_t local[3];
        for(uint32_t i=0;i<3;i++) {
            error=gool_domain_read32(domain,hitbox+4u+i*4u,&local[i]);if(error)return error;
            local[i]=shift4(local[i]);
        }
        error=gool_native_matrix_1fe98(domain,node+0x6cu,node+0x78u,matrix);if(error)return error;
        for(uint32_t row=0;row<3;row++) {
            uint32_t sum=0,position;
            for(uint32_t col=0;col<3;col++)sum+=(uint32_t)matrix[row][col]*local[col];
            error=gool_domain_read32(domain,node+0x60u+row*4u,&position);if(error)return error;
            center[row]=(shift(sum,12)<<4)+position;
        }
    }
    for(uint32_t i=0;i<3;i++) {
        error=gool_domain_read32(domain,node+0x78u+i*4u,&scale[i]);if(error)return error;
    }
    if(scale[0]&0x80000000u)scale[0]=0u-scale[0];
    for(uint32_t i=0;i<6;i++) {
        uint32_t value;error=gool_domain_read32(domain,hitbox+0x10u+i*4u,&value);if(error)return error;
        scaled[i]=shift4(shift(value,8)*scale[i%3u]);
    }
    uint32_t lower[3]={scaled[0],scaled[1],scaled[2]},upper[3]={scaled[3],scaled[4],scaled[5]};
    yaw&=0xfffu;
    if(yaw>=0x200u&&yaw<=0xe00u) {
        if(yaw<0x600u) {
            lower[0]=scaled[2];upper[0]=scaled[5];lower[2]=0u-scaled[3];upper[2]=0u-scaled[0];
        } else if(yaw<0xa00u) {
            lower[2]=0u-scaled[5];upper[2]=0u-scaled[2];
        } else {
            lower[0]=0u-scaled[5];upper[0]=0u-scaled[2];
        }
    }
    /* Native quadrant paths differ in intermediate store order; memory output
     * is identical for the actual distinct cache/object/frame source spans. */
    for(uint32_t i=0;i<3;i++) {
        error=gool_domain_write32(domain,pointer+8u+i*4u,center[i]+lower[i]);if(error)return error;
        error=gool_domain_write32(domain,pointer+0x14u+i*4u,center[i]+upper[i]);if(error)return error;
    }
    uint32_t player,status;
    error=gool_domain_read32(domain,0x80060a90u,&player);if(error)return error;
    error=gool_domain_read32(domain,player+0x100u,&status);if(error)return error;
    uint32_t category=status>>28,padding=category==9u||category==11u?30000u:category==14u?100000u:0;
    if(padding)for(uint32_t axis=0;axis<3;axis+=2) {
        uint32_t value;error=gool_domain_read32(domain,pointer+8u+axis*4u,&value);if(error)return error;
        error=gool_domain_write32(domain,pointer+8u+axis*4u,value-padding);if(error)return error;
        error=gool_domain_read32(domain,pointer+0x14u+axis*4u,&value);if(error)return error;
        error=gool_domain_write32(domain,pointer+0x14u+axis*4u,value+padding);if(error)return error;
    }
    *record=pointer;return 0;
}
int gool_native_bounds_svtx(gool_domain *domain,uint32_t node,uint32_t index,uint32_t *record) {
    if(!domain||!node||(node&3u)||node>UINT32_MAX-0xe8u||!record)return GOOL_DOMAIN_INVALID;
    uint32_t model,auxiliary,frame,count,flags,pointer;uint8_t type;
    int error=gool_domain_read32(domain,node+0xe8u,&model);if(error)return error;
    if(!model)return GOOL_DOMAIN_INVALID;
    error=gool_domain_read8(domain,model,&type);if(error)return error;
    if(type!=1)return GOOL_DOMAIN_UNBOUND;
    error=gool_domain_read32(domain,node+8u,&auxiliary);if(error)return error;
    if(!auxiliary||(auxiliary&3u)||auxiliary>UINT32_MAX-0x20u)return GOOL_DOMAIN_INVALID;
    error=gool_domain_read32(domain,auxiliary+0x20u,&frame);if(error)return error;
    if(!frame||(frame&3u)||frame>UINT32_MAX-0x14u)return GOOL_DOMAIN_INVALID;
    error=gool_domain_read32(domain,frame+0x14u,&count);if(error)return error;
    if(index>=count){*record=0;return 0;}
    uint64_t hitbox=(uint64_t)frame+0x1cu+(uint64_t)index*0x28u;
    if(hitbox+0x24u>UINT32_MAX)return GOOL_DOMAIN_INVALID;
    error=gool_domain_read32(domain,(uint32_t)hitbox,&flags);if(error)return error;
    if((flags&7u)!=1u)return nonoriented(domain,node,(uint32_t)hitbox,record);
    error=reserve(domain,node,1,&pointer);if(error)return error;
    for(uint32_t i=0;i<6;i++) {
        uint32_t value;error=gool_domain_read32(domain,(uint32_t)hitbox+0x10u+i*4u,&value);if(error)return error;
        error=gool_domain_write32(domain,pointer+8u+i*4u,value);if(error)return error;
    }
    for(uint32_t i=0;i<3;i++) {
        uint32_t value;error=gool_domain_read32(domain,node+0x60u+i*4u,&value);if(error)return error;
        error=gool_domain_write32(domain,pointer+0x20u+i*4u,value);if(error)return error;
    }
    error=gool_native_bounds_transform(domain,pointer+0x2cu,pointer+8u,auxiliary,pointer+0x20u);
    if(error)return error;
    *record=pointer;return 0;
}
