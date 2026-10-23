#include "runtime_archive.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}

static uint32_t crc32(const uint8_t *p,uint32_t size) {
    uint32_t crc=UINT32_MAX;
    for(uint32_t i=0;i<size;i++) {
        crc^=p[i];
        for(unsigned bit=0;bit<8;bit++)crc=(crc>>1)^(0xedb88320u & (0u-(crc&1u)));
    }
    return ~crc;
}

static const uint8_t *record(const cga_archive *a,uint32_t i) {
    return a->data+32u+i*24u;
}

static int validate(cga_archive *out,const void *bytes,size_t length,uint32_t expected) {
    if(!out||!bytes||expected>=64||length<36||length>(size_t)CGA_MAX_BYTES+4)return -1;
    const uint8_t *p=bytes;
    if(memcmp(p,"CGA1",4)||rd32(p+4)!=1||rd32(p+8)!=expected)return -1;
    uint32_t count=rd32(p+12),directory=rd32(p+16),nsd=rd32(p+20);
    uint32_t nsd_size=rd32(p+24),total=rd32(p+28);
    if(count>CGA_MAX_ENTRIES||directory!=32||total!=length-4)return -1;
    if(crc32(p,total)!=rd32(p+total))return -1;
    uint32_t cursor=32u+count*24u;
    if(cursor>total)return -1;
    for(uint32_t i=0;i<count;i++) {
        const uint8_t *r=p+32u+i*24u;
        uint32_t eid=rd32(r),type=rd32(r+4),n=rd32(r+8);
        if((type!=7&&type!=11)||n>CGA_MAX_SECTIONS||rd32(r+12)!=cursor)return -1;
        if(n>(total-cursor)/8u)return -1;
        cursor+=n*8u;
        /* EIDs are unique within a level, not across levels. */
        for(uint32_t j=0;j<i;j++)if(rd32(p+32u+j*24u)==eid)return -1;
    }
    if(nsd!=cursor||nsd_size>total-cursor)return -1;
    cursor+=nsd_size;
    for(uint32_t i=0;i<count;i++) {
        const uint8_t *r=p+32u+i*24u;
        uint32_t n=rd32(r+8),table=rd32(r+12),start=rd32(r+16),size=rd32(r+20);
        if(start!=cursor||size>total-cursor)return -1;
        uint32_t end=start+size;
        for(uint32_t j=0;j<n;j++) {
            const uint8_t *s=p+table+j*8u;
            uint32_t offset=rd32(s),size_s=rd32(s+4);
            if(offset!=cursor||size_s>end-cursor)return -1;
            cursor+=size_s;
        }
        if(cursor!=end)return -1;
    }
    if(cursor!=total)return -1;
    *out=(cga_archive){.data=p,.size=(uint32_t)length,.level=expected,
        .count=count,.nsd_offset=nsd,.nsd_size=nsd_size};
    return 0;
}

void cga_close(cga_archive *a) {
    if(!a)return;
    free(a->owned);
    memset(a,0,sizeof(*a));
}

int cga_bind(cga_archive *a,const void *data,size_t size,uint32_t expected) {
    if(!a)return -1;
    /* A borrowed alias into owned data would dangle after replacement. */
    if(a->owned) {
        uintptr_t p=(uintptr_t)data,base=(uintptr_t)a->owned;
        if(p>=base&&p-base<a->size)return -1;
    }
    cga_archive next={0};
    if(validate(&next,data,size,expected))return -1;
    cga_close(a);
    *a=next;
    return 0;
}

int cga_open(cga_archive *a,const char *path,uint32_t expected) {
    if(!a||!path||expected>=64)return -1;
    FILE *f=fopen(path,"rb");
    if(!f)return -1;
    if(fseek(f,0,SEEK_END)){fclose(f);return -1;}
    long length=ftell(f);
    if(length<36||(unsigned long)length>(unsigned long)CGA_MAX_BYTES+4||fseek(f,0,SEEK_SET)) {
        fclose(f);return -1;
    }
    uint8_t *data=malloc((size_t)length);
    if(!data){fclose(f);return -1;}
    size_t got=fread(data,1,(size_t)length,f);
    int failed=ferror(f),closed=fclose(f);
    cga_archive next={0};
    if(got!=(size_t)length||failed||closed||validate(&next,data,(size_t)length,expected)) {
        free(data);return -1;
    }
    next.owned=data;
    cga_close(a);
    *a=next;
    return 0;
}

int cga_entry_at(const cga_archive *a,uint32_t index,cga_entry *out) {
    if(!a||!a->data||!out||index>=a->count)return -1;
    const uint8_t *r=record(a,index);
    *out=(cga_entry){rd32(r),rd32(r+4),rd32(r+8)};
    return 0;
}

int cga_find(const cga_archive *a,uint32_t eid,uint32_t *index) {
    if(!a||!a->data||!index)return -1;
    for(uint32_t i=0;i<a->count;i++)if(rd32(record(a,i))==eid){*index=i;return 0;}
    return -1;
}

int cga_section(const cga_archive *a,uint32_t entry,uint32_t section,
                const uint8_t **data,uint32_t *size) {
    if(!a||!a->data||!data||!size||entry>=a->count)return -1;
    const uint8_t *r=record(a,entry);
    if(section>=rd32(r+8))return -1;
    const uint8_t *s=a->data+rd32(r+12)+section*8u;
    *data=a->data+rd32(s);*size=rd32(s+4);
    return 0;
}

int cga_nsd(const cga_archive *a,const uint8_t **data,uint32_t *size) {
    if(!a||!a->data||!data||!size)return -1;
    *data=a->data+a->nsd_offset;*size=a->nsd_size;
    return 0;
}
