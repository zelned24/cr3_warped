#include "gool_native_matrix.h"
#include "gool_trig.h"

static int32_t signed_word(uint32_t value) {
    return value<=INT32_MAX?(int32_t)value:(int32_t)((int64_t)value-INT64_C(4294967296));
}
static int32_t signed_half(uint32_t value) {
    value&=0xffffu;
    return value<0x8000u?(int32_t)value:(int32_t)value-65536;
}
static int64_t shift12(int64_t value) {
    return value<0?-1-((-1-value)/4096):value/4096;
}
/* Native41F18 performs three MVMVA(sf=1,mx=rotation,v=0/1/2,cv=zero,lm=0)
 * commands and41FBC reloads the packed results as the rotation matrix.
 * Signed16 operands bound each three-product accumulator below2^32, so GTE's
 * signed44-bit MAC cannot overflow here. IR1..3 clamp after the Q12 shift. */
static void multiply(int32_t left[3][3],const int32_t right[3][3]) {
    int32_t result[3][3];
    for(int row=0;row<3;row++)for(int column=0;column<3;column++) {
        int64_t sum=0;
        for(int k=0;k<3;k++)sum+=(int64_t)left[row][k]*right[k][column];
        sum=shift12(sum);
        result[row][column]=sum>32767?32767:sum<-32768?-32768:(int32_t)sum;
    }
    for(int row=0;row<3;row++)for(int column=0;column<3;column++)
        left[row][column]=result[row][column];
}
static void rotation(int32_t matrix[3][3],uint32_t angle,unsigned axis) {
    int32_t sine=gool_trig_sin(signed_word(angle)),cosine=gool_trig_cos(signed_word(angle));
    const int32_t x[3][3]={{4096,0,0},{0,cosine,-sine},{0,sine,cosine}};
    const int32_t y[3][3]={{cosine,0,sine},{0,4096,0},{-sine,0,cosine}};
    const int32_t z[3][3]={{cosine,sine,0},{-sine,cosine,0},{0,0,4096}};
    for(unsigned r=0;r<3;r++)for(unsigned c=0;c<3;c++)
        matrix[r][c]=axis==0?x[r][c]:axis==1?y[r][c]:z[r][c];
}
int gool_native_matrix_build(gool_domain *domain,uint32_t function,uint32_t output,
                             uint32_t angles,uint32_t scale,uint32_t model_scale) {
    if(function!=0x8003d470u&&function!=0x8003e754u&&function!=0x8003f044u&&function!=0x80040c20u)
        return GOOL_NATIVE_MATRIX_INVALID;
    if(!domain||!output||!angles||!scale||((output|angles|scale|model_scale)&3u)||
       output>UINT32_MAX-31u||angles>UINT32_MAX-11u||scale>UINT32_MAX-11u||
       (model_scale&&model_scale>UINT32_MAX-11u))return GOOL_NATIVE_MATRIX_INVALID;
    uint32_t angle[3],scaled[3],factor[3];int error;
    for(uint32_t i=0;i<3;i++) {
        error=gool_domain_read32(domain,angles+i*4,&angle[i]);if(error)return error;
    }
    int32_t matrix[3][3],next[3][3];
    if(function==0x8003d470u)rotation(matrix,angle[1],1);
    else if(function==0x8003f044u)rotation(matrix,angle[2],1);
    else rotation(matrix,angle[2],2);
    if(angle[0]) {
        rotation(next,angle[0],0);multiply(matrix,next);
    }
    uint32_t final_angle=0;unsigned final_axis=1;
    if(function==0x8003d470u){final_angle=angle[2];final_axis=2;}
    else if(function==0x8003f044u)final_angle=angle[1]-angle[2];
    else if(function==0x80040c20u)final_angle=angle[1];
    if(final_angle) {
        rotation(next,final_angle,final_axis);multiply(matrix,next);
    }
    for(uint32_t i=0;i<3;i++) {
        error=gool_domain_read32(domain,scale+i*4,&scaled[i]);if(error)return error;
    }
    if(model_scale) {
        for(uint32_t i=0;i<3;i++) {
            error=gool_domain_read32(domain,model_scale+i*4,&factor[i]);if(error)return error;
        }
        for(uint32_t i=0;i<3;i++)
            scaled[i]=(uint32_t)shift12(signed_word(scaled[i]*factor[i]));
    }
    for(uint32_t i=0;i<3;i++) {
        error=gool_domain_write32(domain,output+0x14u+i*4,scaled[i]);if(error)return error;
    }
    if(((scaled[0]&0xffffu)|(scaled[1]&0xffffu)|scaled[2])!=0x1000u) {
        const int32_t diagonal[3][3]={{signed_half(scaled[0]),0,0},
            {0,signed_half(scaled[1]),0},{0,0,signed_half(scaled[2])}};
        multiply(matrix,diagonal);
    }
    for(uint32_t i=0;i<4;i++) {
        uint32_t first=i*2,second=first+1;
        uint32_t packed=((uint32_t)matrix[first/3][first%3]&0xffffu)|
                        (((uint32_t)matrix[second/3][second%3]&0xffffu)<<16);
        error=gool_domain_write32(domain,output+i*4,packed);if(error)return error;
    }
    return gool_domain_write32(domain,output+0x10u,(uint32_t)matrix[2][2]);
}
int gool_native_matrix_3d470(gool_domain *domain,uint32_t output,uint32_t angles,
                            uint32_t scale,uint32_t model_scale) {
    return gool_native_matrix_build(domain,0x8003d470u,output,angles,scale,model_scale);
}
int gool_native_matrix_1fe98(gool_domain *domain,uint32_t angles,uint32_t scale,int32_t output[3][3]) {
    if(!domain||!angles||(angles&3u)||angles>UINT32_MAX-8u||!output||
       (scale&&((scale&3u)||scale>UINT32_MAX-8u)))return GOOL_NATIVE_MATRIX_INVALID;
    uint32_t a[3],s[3];int error;
    for(uint32_t i=0;i<3;i++) {
        error=gool_domain_read32(domain,angles+i*4u,&a[i]);if(error)return error;
    }
    int32_t matrix[3][3],next[3][3];
    rotation(matrix,a[2],1);rotation(next,a[0],0);multiply(matrix,next);
    rotation(next,a[1]-a[2],1);multiply(matrix,next);
    if(scale) {
        for(uint32_t i=0;i<3;i++) {
            error=gool_domain_read32(domain,scale+i*4u,&s[i]);if(error)return error;
        }
        for(uint32_t row=0;row<3;row++)for(uint32_t col=0;col<3;col++) {
            uint32_t product=(uint32_t)matrix[row][col]*s[col];
            matrix[row][col]=signed_half((uint32_t)shift12(signed_word(product)));
        }
    }
    for(uint32_t row=0;row<3;row++)for(uint32_t col=0;col<3;col++)output[row][col]=matrix[row][col];
    return 0;
}
int gool_native_matrix_41ef0(const uint32_t current[5],const uint32_t left_rotation[5],uint32_t output[5]) {
    if(!current||!left_rotation||!output)return GOOL_NATIVE_MATRIX_INVALID;
    int32_t left[3][3],right[3][3];
    for(uint32_t i=0;i<9;i++) {
        left[i/3u][i%3u]=signed_half(left_rotation[i/2u]>>((i&1u)*16u));
        right[i/3u][i%3u]=signed_half(current[i/2u]>>((i&1u)*16u));
    }
    multiply(left,right);
    uint32_t packed[5];
    for(uint32_t i=0;i<4;i++) {
        uint32_t a=i*2u,b=a+1u;
        packed[i]=((uint32_t)left[a/3u][a%3u]&0xffffu)|((uint32_t)left[b/3u][b%3u]<<16);
    }
    packed[4]=(uint32_t)left[2][2];
    for(uint32_t i=0;i<5;i++)output[i]=packed[i];
    return 0;
}
static uint32_t axis160(uint32_t half) {
    /* Halfword*160 cannot overflow signed32; retain the native low32
     * representation and arithmetic shift8 before wrapping negation. */
    int32_t product=signed_half(half)*160;
    int32_t shifted=product<0?-1-((-1-product)/256):product/256;
    return 0u-(uint32_t)shifted;
}
int gool_native_matrix_41fd8(const uint32_t current[5],uint32_t output[5]) {
    if(!current||!output)return GOOL_NATIVE_MATRIX_INVALID;
    uint32_t packed[5];
    packed[0]=current[0];
    packed[1]=(current[1]&0xffffu)|(axis160(current[1]>>16)<<16);
    packed[2]=(axis160(current[2])&0xffffu)|(axis160(current[2]>>16)<<16);
    packed[3]=((0u-current[3])&0xffffu)|((0u-(uint32_t)signed_half(current[3]>>16))<<16);
    packed[4]=0u-current[4];
    for(uint32_t i=0;i<5;i++)output[i]=packed[i];
    return 0;
}
static uint32_t sra_word(uint32_t word,uint32_t count) {
    return (word>>count)|((word&0x80000000u)?UINT32_MAX<<(32u-count):0u);
}
int gool_native_matrix_translation_42058(const gool_native_translation_input *in,
    gool_native_translation *output) {
    if(!in||!output)return GOOL_NATIVE_MATRIX_INVALID;
    uint32_t position[3]={in->position[0],in->position[1],in->position[2]};
    if(in->packet_flags&0x40000000u) {
        uint32_t xy=in->frame_offset[0],z=in->frame_offset[1];
        if(in->packet_flags&0x20000000u) {
            uint32_t next=in->second_offset[0];
            uint32_t y=sra_word((uint32_t)signed_half(xy>>16)+(uint32_t)signed_half(next>>16),1);
            /* Native SLL16 + ADDU + SRA17 deliberately wraps before shifting;
             * this is not the average of two independently extended int16s. */
            uint32_t x=sra_word((xy<<16)+(next<<16),17);
            xy=(x&0xffffu)|(y<<16);z=sra_word(z+in->second_offset[1],1);
        }
        if(xy||z) {
            const uint32_t offsets[3]={(uint32_t)signed_half(xy),(uint32_t)signed_half(xy>>16),z};
            /* Original visits Z,Y,X; snapshots are immutable, and each axis
             * result is independent. Every multiplication keeps low32. */
            for(uint32_t i=3;i>0;i--) {
                uint32_t axis=i-1u;
                uint32_t scale=sra_word(in->object_scale[axis]*in->geometry_scale[axis],12);
                position[axis]+=sra_word(offsets[axis]*scale,4);
            }
        }
    }
    gool_native_translation next={.source_y=position[1]};
    uint32_t delta[3];
    for(uint32_t i=0;i<3;i++)delta[i]=sra_word(position[i],8)-in->camera_position[i];
    for(uint32_t row=0;row<3;row++) {
        uint32_t sum=0;
        for(uint32_t col=0;col<3;col++) {
            uint32_t index=row*3u+col;
            int32_t rotation=signed_half(in->camera_rotation[index/2u]>>((index&1u)*16u));
            int64_t product=(int64_t)signed_word(delta[col])*rotation;
            sum+=(uint32_t)shift12(product);
        }
        next.translation[row]=sum;
    }
    *output=next;return 0;
}
