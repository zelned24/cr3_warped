#include "gool_admission.h"
#include <limits.h>

#define CHAIN_HEAD UINT32_C(0x80069210)
#define CHAIN_HEIGHT UINT32_C(0x80069214)
#define CHAIN_SOURCE UINT32_C(0x80069218)
static int32_t signed_word(uint32_t n) {
    return n<=INT32_MAX?(int32_t)n:-1-(int32_t)(UINT32_MAX-n);
}
static int status(int error){return error>0?GOOL_ADMISSION_INVALID:error;}
static int read_word(const gool_admission_services *s,uint32_t address,uint32_t *out) {
    if(!address||(address&3u))return GOOL_ADMISSION_INVALID;
    return status(s->read(s->context,address,out));
}
static int read_at(const gool_admission_services *s,uint32_t base,uint32_t offset,uint32_t *out) {
    if(!base||offset>UINT32_MAX-base)return GOOL_ADMISSION_INVALID;
    return read_word(s,base+offset,out);
}
static int write_word(const gool_admission_services *s,uint32_t address,uint32_t value) {
    if(!address||(address&3u))return GOOL_ADMISSION_INVALID;
    return status(s->write(s->context,address,value));
}
static int scalar(const gool_admission_services *s,uint32_t db,uint32_t key,uint32_t *out) {
    return status(s->scalar(s->context,db,key,out));
}
static int half(const gool_admission_services *s,uint32_t base,uint32_t offset,int32_t *out) {
    if(!base||(base&1u)||offset>UINT32_MAX-base)return GOOL_ADMISSION_INVALID;
    uint32_t at=base+offset,word;int error=read_word(s,at&~3u,&word);if(error)return error;
    uint32_t value=(word>>((at&2u)*8))&65535u;
    *out=value<32768?(int32_t)value:(int32_t)value-65536;return 0;
}
static uint32_t difference(int32_t a,int32_t b) {
    uint32_t value=(uint32_t)a-(uint32_t)b;
    return value&0x80000000u?(uint32_t)b-(uint32_t)a:value;
}
#define TRY(expression) do{int error=(expression);if(error)return error;}while(0)
static int usage(const gool_admission_services *s,uint32_t index,uint32_t *at,uint32_t *value) {
    if(index>=s->usage_count||(uint64_t)s->usage_address+(uint64_t)index*4>UINT32_MAX)
        return GOOL_ADMISSION_RANGE;
    *at=s->usage_address+index*4;return read_word(s,*at,value);
}
int gool_admission(uint32_t entry,uint32_t entity_index,
                   const gool_admission_services *s,gool_admission_result *out) {
    if(!s||!out||!s->read||!s->write||!s->scalar||!s->path||!s->usage_count)
        return GOOL_ADMISSION_INVALID;
    gool_admission_result result={.native_result=-14};
    if(!entry){*out=result;return 0;}
    uint32_t header,infos,databases,value,index,at;
    TRY(read_at(s,entry,16,&header));TRY(read_at(s,header,0x184,&infos));
    TRY(read_at(s,header,0x188,&databases));
    uint64_t slot=(uint64_t)infos+databases+entity_index;
    if(slot>(UINT32_MAX-16u)/4u)return GOOL_ADMISSION_RANGE;
    TRY(read_at(s,entry,16+(uint32_t)slot*4,&result.database));
    TRY(scalar(s,result.database,0xa9,&value));
    if(!value)result.protected_slot=1;
    else {
        TRY(scalar(s,result.database,0x9f,&value));
        if(signed_word(value)>0) {
            TRY(scalar(s,result.database,0x9f,&value));
            if(signed_word(value)<5)result.protected_slot=1;
        }
    }
    if(result.protected_slot) {
        TRY(read_word(s,s->protected_address,&value));
        if(value){*out=result;return 0;}
    }
    TRY(scalar(s,result.database,0xa9,&value));
    if(value==0x22) {
        uint32_t previous,current_path=0,previous_path=0;int linked=0;
        TRY(read_word(s,CHAIN_SOURCE,&previous));
        if(previous) {
            TRY(status(s->path(s->context,result.database,&current_path)));
            TRY(read_word(s,CHAIN_SOURCE,&previous));
            TRY(status(s->path(s->context,previous,&previous_path)));
            TRY(read_word(s,CHAIN_SOURCE,&previous));
            if(previous) {
                int32_t a,b;TRY(half(s,current_path,0,&a));TRY(half(s,previous_path,0,&b));
                if(signed_word(difference(a,b))<40) {
                    TRY(half(s,current_path,4,&a));TRY(half(s,previous_path,4,&b));
                    if(signed_word(difference(a,b))<40) {
                        TRY(half(s,previous_path,2,&b));TRY(half(s,current_path,2,&a));
                        if(signed_word(difference(a,b+400))<40)linked=1;
                    }
                }
            }
        }
        if(linked)TRY(read_word(s,CHAIN_HEAD,&result.previous_chain));
        else {
            TRY(write_word(s,CHAIN_HEIGHT,0x19000));TRY(write_word(s,CHAIN_HEAD,0));
        }
        TRY(write_word(s,CHAIN_SOURCE,result.database));
    }
    TRY(scalar(s,result.database,0x9f,&value));
    TRY(scalar(s,result.database,0x9f,&index));TRY(usage(s,index,&at,&value));
    int occupied=(value&1u)!=0;
    if(!occupied) {
        TRY(scalar(s,result.database,0x9f,&value));
        TRY(scalar(s,result.database,0x9f,&index));TRY(usage(s,index,&at,&value));
        occupied=(value&2u)!=0;
    }
    if(occupied) {
        TRY(scalar(s,result.database,0xa9,&value));
        if(value==0x22) {
            TRY(read_word(s,CHAIN_HEIGHT,&value));TRY(write_word(s,CHAIN_HEIGHT,value+0x19000u));
        }
        *out=result;return 0;
    }
    TRY(scalar(s,result.database,0x342,&value));
    if(value&1u) {
        TRY(scalar(s,result.database,0x9f,&index));
        if(index>=s->usage_count||(uint64_t)s->usage_address+(uint64_t)index*4>UINT32_MAX)
            return GOOL_ADMISSION_RANGE;
        at=s->usage_address+index*4;
        TRY(scalar(s,result.database,0x9f,&value));TRY(read_word(s,at,&value));
        TRY(write_word(s,at,value|2u));*out=result;return 0;
    }
    result.admitted=1;result.native_result=0;*out=result;return 0;
}
#undef TRY
