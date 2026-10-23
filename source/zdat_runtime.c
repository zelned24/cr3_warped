#include "zdat_runtime.h"

static uint32_t u16(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8;}
static uint32_t u32(const uint8_t *p){return u16(p)|u16(p+2)<<16;}
static int32_t s32(uint32_t n){return n<=INT32_MAX?(int32_t)n:-1-(int32_t)(UINT32_MAX-n);}
static int32_t s16(uint32_t n){return n<32768?(int32_t)n:(int32_t)n-65536;}
static unsigned width(unsigned type) {
    switch(type){case 1:case 17:return 1;case 2:case 18:return 2;
    case 3:case 4:case 5:case 19:return 4;case 6:return 6;case 7:return 12;default:return 0;}
}
static int read_series(zdat_span bytes,uint32_t index,uint32_t count,zdat_series *out) {
    const uint8_t *h=bytes.data+16+index*8;
    zdat_series s={.bytes=bytes,.key=(uint16_t)u16(h),.count=(uint16_t)u16(h+6),
        .format=h[4],.stride=h[5],.sizes_offset=12+u16(h+2)};
    if(!width(s.format&31)||s.stride!=width(s.format&31)||s.sizes_offset<16+count*8)
        return ZDAT_FORMAT;
    uint64_t offset=(uint64_t)s.sizes_offset+((s.format&64)?(uint64_t)s.count*2:2);
    if(offset>bytes.size)return ZDAT_FORMAT;
    s.times_offset=(uint32_t)offset;
    if(s.format&32)offset+=(uint64_t)s.count*2;
    offset=(offset+3)&~UINT64_C(3);
    if(offset>bytes.size)return ZDAT_FORMAT;
    s.values_offset=(uint32_t)offset;
    for(uint32_t i=0;i<s.count;i++) {
        offset+=(uint64_t)u16(bytes.data+s.sizes_offset+((s.format&64)?i*2:0))*s.stride;
        if(offset>bytes.size)return ZDAT_FORMAT;
    }
    *out=s;return 0;
}
int zdat_database_bind(zdat_database *out,zdat_span bytes) {
    if(!out||!bytes.data||bytes.size<16||bytes.size>65536)return ZDAT_INVALID;
    uint32_t count=u16(bytes.data+12);
    if(u32(bytes.data)!=bytes.size||16+count*8>bytes.size)return ZDAT_FORMAT;
    for(uint32_t i=0;i<count;i++) {
        zdat_series s;int error=read_series(bytes,i,count,&s);if(error)return error;
        for(uint32_t j=0;j<i;j++)if(u16(bytes.data+16+j*8)==s.key)return ZDAT_FORMAT;
    }
    *out=(zdat_database){bytes,(uint16_t)count};return 0;
}
int zdat_series_at(const zdat_database *db,uint32_t index,zdat_series *out) {
    if(!db||!db->bytes.data||!out||index>=db->count)return ZDAT_INVALID;
    return read_series(db->bytes,index,db->count,out);
}
int zdat_series_find(const zdat_database *db,uint16_t key,zdat_series *out) {
    if(!db||!db->bytes.data||!out)return ZDAT_INVALID;
    for(uint32_t i=0;i<db->count;i++)if(u16(db->bytes.data+16+i*8)==key)return zdat_series_at(db,i,out);
    return ZDAT_MISSING;
}
int zdat_series_length(const zdat_series *s,uint32_t value,uint32_t *length,uint16_t *time) {
    if(!s||!s->bytes.data||!length||!time||value>=s->count)return ZDAT_INVALID;
    *length=u16(s->bytes.data+s->sizes_offset+((s->format&64)?value*2:0));
    *time=(s->format&32)?(uint16_t)u16(s->bytes.data+s->times_offset+value*2):0;return 0;
}
int zdat_series_value(const zdat_series *s,uint32_t value,uint32_t element,int32_t out[3],uint32_t *components) {
    if(!s||!out||!components)return ZDAT_INVALID;
    uint32_t length;uint16_t time;int error=zdat_series_length(s,value,&length,&time);
    if(error)return error;
    if(element>=length)return ZDAT_RANGE;
    uint64_t offset=s->values_offset;
    for(uint32_t i=0;i<value;i++)offset+=(uint64_t)u16(s->bytes.data+s->sizes_offset+((s->format&64)?i*2:0))*s->stride;
    offset+=(uint64_t)element*s->stride;
    if(offset+s->stride>s->bytes.size)return ZDAT_FORMAT;
    const uint8_t *p=s->bytes.data+(uint32_t)offset;
    int32_t next[3]={0,0,0};uint32_t n=1,type=s->format&31;
    switch(type) {
    case 1:next[0]=p[0];break;case 17:next[0]=p[0]<128?p[0]:(int32_t)p[0]-256;break;
    case 2:next[0]=(int32_t)u16(p);break;case 18:next[0]=s16(u16(p));break;
    case 3:case 4:case 5:case 19:next[0]=s32(u32(p));break;
    case 6:n=3;for(unsigned i=0;i<3;i++)next[i]=s16(u16(p+i*2));break;
    case 7:n=3;for(unsigned i=0;i<3;i++)next[i]=s32(u32(p+i*4));break;
    default:return ZDAT_FORMAT;
    }
    for(unsigned i=0;i<3;i++)out[i]=next[i];
    *components=n;return 0;
}
int zdat_zone_bind(zdat_zone *out,const zdat_span *items,uint32_t count) {
    if(!out||!items||count<2||!items[0].data||items[0].size<0x194||items[0].size>65536)return ZDAT_INVALID;
    const uint8_t *h=items[0].data;
    zdat_zone z={.items=items,.item_count=count,.zone_index=u32(h),.sky_eid=u32(h+4),
        .info_count=u32(h+0x184),.database_count=u32(h+0x188),
        .entity_count=u32(h+0x18c),.other_count=u32(h+0x190)};
    if(z.info_count!=2||(uint64_t)2+z.database_count+z.entity_count!=count||
       (uint64_t)0x194+(uint64_t)z.other_count*4>items[0].size)return ZDAT_FORMAT;
    for(unsigned i=0;i<6;i++)z.world_eids[i]=u32(h+0x34+i*0x30);
    if(z.info_count>1) {
        if(!items[1].data||items[1].size<12)return ZDAT_FORMAT;
        z.has_offset=1;for(unsigned i=0;i<3;i++)z.offset[i]=s32(u32(items[1].data+i*4));
    }
    for(uint32_t i=2;i<count;i++) {zdat_database db;int error=zdat_database_bind(&db,items[i]);if(error)return error;}
    *out=z;return 0;
}
int zdat_zone_database(const zdat_zone *z,uint32_t index,zdat_database *out) {
    if(!z||!z->items||index>=z->database_count)return ZDAT_INVALID;
    return zdat_database_bind(out,z->items[2+index]);
}
static int scalar(const zdat_database *db,uint16_t key,int32_t *out,uint32_t *present,uint32_t flag) {
    zdat_series s;int error=zdat_series_find(db,key,&s);if(error==ZDAT_MISSING)return 0;if(error)return error;
    int32_t v[3];uint32_t n;error=zdat_series_value(&s,0,0,v,&n);if(error)return error;
    if(n!=1)return ZDAT_FORMAT;
    *out=v[0];*present|=flag;return 0;
}
int zdat_zone_entity(const zdat_zone *z,uint32_t index,zdat_entity *out) {
    if(!z||!z->items||!out||index>=z->entity_count)return ZDAT_INVALID;
    zdat_entity e={0};int error=zdat_database_bind(&e.database,z->items[2+z->database_count+index]);if(error)return error;
    const uint16_t keys[]={0x2c,0x4b,0xa4};zdat_series *series[]={&e.name,&e.path,&e.parameters};
    for(unsigned i=0;i<3;i++){error=zdat_series_find(&e.database,keys[i],series[i]);if(error&&error!=ZDAT_MISSING)return error;}
    if((e.name.bytes.data&&(e.name.format&31)!=1)||(e.path.bytes.data&&(e.path.format&31)!=6&&
       (e.path.format&31)!=7)||(e.parameters.bytes.data&&width(e.parameters.format&31)>4))return ZDAT_FORMAT;
    if((error=scalar(&e.database,0xa9,&e.file_index,&e.present,ZDAT_ENTITY_FILE))||
       (error=scalar(&e.database,0xaa,&e.object_id,&e.present,ZDAT_ENTITY_OBJECT))||
       (error=scalar(&e.database,0x9f,&e.lookup_index,&e.present,ZDAT_ENTITY_LOOKUP))||
       (error=scalar(&e.database,0x47,&e.geometry_format,&e.present,ZDAT_ENTITY_GEOMETRY)))return error;
    uint32_t shift_present=0;error=scalar(&e.database,0x30e,&e.shift,&shift_present,1);if(error)return error;
    if(e.shift<0||e.shift>30)return ZDAT_RANGE;
    *out=e;return 0;
}
int zdat_zone_other_eid(const zdat_zone *z,uint32_t index,uint32_t *eid) {
    if(!z||!z->items||!eid||index>=z->other_count)return ZDAT_INVALID;
    *eid=u32(z->items[0].data+0x194+index*4);return 0;
}
int zdat_entity_position(const zdat_zone *z,const zdat_entity *e,uint32_t point,int32_t raw[3],int64_t reference[3]) {
    if(!z||!e||!raw||!reference||e->shift<0||e->shift>30)return ZDAT_INVALID;
    int32_t v[3];uint32_t n;int error=zdat_series_value(&e->path,0,point,v,&n);if(error)return error;
    if(n!=3)return ZDAT_FORMAT;
    int64_t positions[3];
    for(unsigned i=0;i<3;i++) {
        int64_t value=(z->has_offset?z->offset[i]:0)+(int64_t)v[i]*(INT64_C(1)<<e->shift);
        if(value>INT64_MAX/256||value<INT64_MIN/256)return ZDAT_RANGE;
        positions[i]=value*256;
    }
    for(unsigned i=0;i<3;i++){raw[i]=v[i];reference[i]=positions[i];}
    return 0;
}
int zdat_class_eid(zdat_span nsd,int32_t index,uint32_t *eid) {
    if(!nsd.data||nsd.size<0x520||!eid||index<0||index>=128)return ZDAT_INVALID;
    uint64_t base=0x520+(uint64_t)u32(nsd.data+0x404)*8+0x10;
    if(base+128*4>nsd.size)return ZDAT_FORMAT;
    *eid=u32(nsd.data+(uint32_t)base+(uint32_t)index*4);return 0;
}
int zdat_entity_state(const zdat_entity *e,zdat_span indices,uint32_t signals,uint32_t states,uint32_t *state) {
    if(!e||!indices.data||!state||!(e->present&ZDAT_ENTITY_OBJECT)||e->object_id<0)return ZDAT_INVALID;
    uint64_t offset=((uint64_t)signals+(uint32_t)e->object_id)*2;
    if(indices.size%2||offset+2>indices.size)return ZDAT_RANGE;
    int32_t mapped=s16(u16(indices.data+(uint32_t)offset));
    if(mapped<0||mapped==255)return 1;
    if((uint32_t)mapped>=states)return ZDAT_RANGE;
    *state=(uint32_t)mapped;return 0;
}
