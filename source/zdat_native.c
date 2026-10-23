#include "zdat_native.h"
#include <limits.h>

static int32_t signed_half(uint16_t value){return value<32768u?(int32_t)value:(int32_t)value-65536;}
static int fits(uint32_t size,uint32_t offset,uint32_t count) {
    return offset<=size&&count<=size-offset;
}
static int half(const zdat_native *s,uint32_t db,uint32_t size,uint32_t offset,uint16_t *value) {
    if(!fits(size,offset,2))return ZDAT_NATIVE_RANGE;
    return gool_domain_read16(s->domain,db+offset,value);
}
static int select_value(const zdat_native *s,uint32_t db,int32_t key,uint32_t value,
                       uint32_t element,int exact,zdat_native_value *out) {
    if(!s||!s->domain||!out||!s->chain_limit||s->chain_limit>ZDAT_NATIVE_CHAIN_MAX||
       !db||(db&3u)||key<INT16_MIN||key>INT16_MAX||(!exact&&value>INT32_MAX)||element>INT32_MAX)
        return ZDAT_NATIVE_INVALID;
    uint32_t visited[ZDAT_NATIVE_CHAIN_MAX],used=0;
    zdat_native_value result={0};
    while(db) {
        if((db&3u)||db>UINT32_MAX-16)return ZDAT_NATIVE_RANGE;
        if(used==s->chain_limit)return ZDAT_NATIVE_CYCLE;
        for(uint32_t i=0;i<used;i++)if(visited[i]==db)return ZDAT_NATIVE_CYCLE;
        visited[used++]=db;
        uint32_t size;int error=gool_domain_read32(s->domain,db,&size);if(error)return error;
        if(size<16||size>65536u||(uint64_t)db+size>UINT32_MAX)return ZDAT_NATIVE_RANGE;
        uint16_t raw_count;error=half(s,db,size,12,&raw_count);if(error)return error;
        int32_t count=signed_half(raw_count);
        if(count>0&&!fits(size,16,(uint32_t)count*8))return ZDAT_NATIVE_RANGE;
        int32_t low=0,high=count-1,found=-1;
        while(low<=high) {
            int32_t middle=low+(high-low)/2;uint16_t raw;
            error=half(s,db,size,16+(uint32_t)middle*8,&raw);if(error)return error;
            int32_t candidate=signed_half(raw);
            if(candidate==key){found=middle;break;}
            if(candidate<key)low=middle+1;else high=middle-1;
        }
        if(found<0) {
            error=gool_domain_read32(s->domain,db+8,&db);if(error)return error;
            continue;
        }
        uint32_t descriptor=16+(uint32_t)found*8;
        uint16_t relative,values;
        error=half(s,db,size,descriptor+2,&relative);if(error)return error;
        error=half(s,db,size,descriptor+6,&values);if(error)return error;
        uint8_t format,stride;
        error=gool_domain_read8(s->domain,db+descriptor+4,&format);if(error)return error;
        error=gool_domain_read8(s->domain,db+descriptor+5,&stride);if(error)return error;
        result.descriptor=db+descriptor;result.end=db+size;result.format=format;result.stride=stride;
        if(!exact&&value>=values){*out=result;return 0;}
        uint32_t sizes=12u+relative,size_words=(format&0x40u)?values:1u;
        if(!fits(size,sizes,size_words*2u))return ZDAT_NATIVE_RANGE;
        uint64_t data=(uint64_t)sizes+size_words*2u;
        if(exact) {
            /* Native32710 mode1 selects only an exact signed sample time.
             * Missing timing metadata or an in-between sample returns null;
             * it does not interpolate, clamp, or search the parent database. */
            if(!(format&0x20u)||!values){*out=result;return 0;}
            if(data>size||!fits(size,(uint32_t)data,(uint32_t)values*2u))return ZDAT_NATIVE_RANGE;
            int32_t sample=(value&0x80000000u)?-1-(int32_t)((UINT32_MAX-value)>>8):(int32_t)(value>>8);
            int32_t first=0,last=(int32_t)values-1,chosen=-1;
            while(first<=last) {
                int32_t middle=first+(last-first)/2;uint16_t raw;
                error=half(s,db,size,(uint32_t)data+(uint32_t)middle*2u,&raw);if(error)return error;
                if(signed_half(raw)<=sample){chosen=middle;first=middle+1;}
                else last=middle-1;
            }
            if(chosen<0){*out=result;return 0;}
            uint16_t raw;error=half(s,db,size,(uint32_t)data+(uint32_t)chosen*2u,&raw);if(error)return error;
            if(signed_half(raw)!=sample){*out=result;return 0;}
            value=(uint32_t)chosen;
        }
        if(format&0x20u)data+=(uint32_t)values*2u;
        data=(data+3u)&~UINT64_C(3);
        if(data>size||!stride)return ZDAT_NATIVE_RANGE;
        uint16_t length;
        error=half(s,db,size,sizes+((format&0x40u)?value*2u:0),&length);if(error)return error;
        if(element>=length){*out=result;return 0;}
        uint64_t offset=element;
        if(format&0x40u) {
            for(uint32_t i=0;i<value;i++) {
                uint16_t previous;error=half(s,db,size,sizes+i*2u,&previous);if(error)return error;
                offset+=previous;
            }
        } else offset+=(uint64_t)value*length;
        offset=data+offset*stride;
        if(offset>size||stride>size-offset)return ZDAT_NATIVE_RANGE;
        result.address=db+(uint32_t)offset;result.length=length;
        *out=result;return 0;
    }
    *out=result;return 0;
}
int zdat_native_select(const zdat_native *s,uint32_t database,int32_t key,uint32_t value,
                       uint32_t element,zdat_native_value *out) {
    return select_value(s,database,key,value,element,0,out);
}
int zdat_native_select_exact(const zdat_native *s,uint32_t database,int32_t key,
                            uint32_t position,zdat_native_value *out) {
    return select_value(s,database,key,position,0,1,out);
}
int zdat_native_scalar(void *context,uint32_t database,uint32_t key,uint32_t *value) {
    zdat_native *s=context;
    if(!value||key>INT16_MAX)return ZDAT_NATIVE_INVALID;
    zdat_native_value selected;
    int error=zdat_native_select(s,database,(int32_t)key,0,0,&selected);if(error)return error;
    if(!selected.address){*value=0;return 0;}
    uint32_t type=selected.format&31u,width=type==1u||type==17u?1u:type==2u||type==18u?2u:4u;
    if(selected.address>selected.end||width>selected.end-selected.address)return ZDAT_NATIVE_RANGE;
    uint32_t result;
    switch(selected.format&31u) {
    case 1:case 17: {
        uint8_t byte;error=gool_domain_read8(s->domain,selected.address,&byte);if(error)return error;
        result=(selected.format&31u)==17u&&byte>=128u?0xffffff00u|byte:byte;break;
    }
    case 2:case 18: {
        uint16_t h;error=gool_domain_read16(s->domain,selected.address,&h);if(error)return error;
        result=(selected.format&31u)==18u&&h>=32768u?0xffff0000u|h:h;break;
    }
    default:error=gool_domain_read32(s->domain,selected.address,&result);if(error)return error;break;
    }
    *value=result;return 0;
}
int zdat_native_geometry(void *context,uint32_t database,uint32_t *value) {
    return zdat_native_scalar(context,database,0x47,value);
}
int zdat_native_path(void *context,uint32_t database,uint32_t *address) {
    if(!address)return ZDAT_NATIVE_INVALID;
    zdat_native_value selected;int error=zdat_native_select(context,database,0x4b,0,0,&selected);
    if(error)return error;
    *address=selected.address;return 0;
}
int zdat_native_path_count(void *context,uint32_t database,uint32_t *count) {
    if(!count)return ZDAT_NATIVE_INVALID;
    zdat_native_value selected;int error=zdat_native_select(context,database,0x4b,0,0,&selected);
    if(error)return error;
    *count=selected.length;return 0;
}
int zdat_native_parameters(void *context,uint32_t database,uint32_t *address,uint32_t *count) {
    if(!address||!count||address==count)return ZDAT_NATIVE_INVALID;
    zdat_native_value selected;int error=zdat_native_select(context,database,0xa4,0,0,&selected);
    if(error)return error;
    *address=selected.address;*count=selected.length;return 0;
}

static int zone_item(zdat_native *s,uint32_t entry,uint32_t count,uint32_t index,uint32_t *database) {
    if(index>=count||(uint64_t)entry+16+(uint64_t)index*4>UINT32_MAX)return ZDAT_NATIVE_RANGE;
    int error=gool_domain_read32(s->domain,entry+16+index*4,database);if(error)return error;
    if(!*database||(*database&3u)||*database>UINT32_MAX-16)return ZDAT_NATIVE_RANGE;
    uint32_t size;error=gool_domain_read32(s->domain,*database,&size);if(error)return error;
    return size<16u||size>65536u||(uint64_t)*database+size>UINT32_MAX?ZDAT_NATIVE_RANGE:0;
}
int zdat_native_link_zone(zdat_native *s,uint32_t entry) {
    if(!s||!s->domain||!entry||(entry&3u)||entry>UINT32_MAX-20)return ZDAT_NATIVE_INVALID;
    uint32_t count,header,info,databases,entities;
    int error=gool_domain_read32(s->domain,entry+12,&count);if(error)return error;
    error=gool_domain_read32(s->domain,entry+16,&header);if(error)return error;
    if(!header||(header&3u)||header>UINT32_MAX-0x190)return ZDAT_NATIVE_RANGE;
    error=gool_domain_read32(s->domain,header+0x184,&info);if(error)return error;
    error=gool_domain_read32(s->domain,header+0x188,&databases);if(error)return error;
    error=gool_domain_read32(s->domain,header+0x18c,&entities);if(error)return error;
    if(info>count)return ZDAT_NATIVE_RANGE;
    /* Negative native counts skip their loops. Reject impossible directory
     * arithmetic instead of allowing a later entity address to wrap. */
    uint32_t camera_count=(databases&0x80000000u)?0:databases;
    uint32_t entity_count=(entities&0x80000000u)?0:entities;
    if((uint64_t)info+camera_count>count||
       (entity_count&&((databases&0x80000000u)||(uint64_t)info+databases+entity_count>count)))
        return ZDAT_NATIVE_RANGE;
    for(uint32_t group=0;group<camera_count/3;group++) {
        uint32_t root;
        error=zone_item(s,entry,count,info+group*3,&root);if(error)return error;
        error=gool_domain_write32(s->domain,root+4,entry);if(error)return error;
        for(uint32_t i=1;i<3;i++) {
            uint32_t child;
            error=zone_item(s,entry,count,info+group*3+i,&child);if(error)return error;
            error=gool_domain_write32(s->domain,child+4,entry);if(error)return error;
            error=gool_domain_write32(s->domain,child+8,root);if(error)return error;
        }
    }
    for(uint32_t i=0;i<entity_count;i++) {
        uint32_t entity;
        error=zone_item(s,entry,count,info+databases+i,&entity);if(error)return error;
        error=gool_domain_write32(s->domain,entity+4,entry);if(error)return error;
    }
    return 0;
}
