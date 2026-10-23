/* Production C regression harness; run through tests/test_gool_c.py. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "runtime_archive.c"
#ifdef NDEBUG
#error Assertions are required
#endif

static void wr32(uint8_t *p,uint32_t n) {
    for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));
}
static void checksum(uint8_t *p,size_t size) {
    wr32(p+size-4,crc32(p,(uint32_t)size-4));
}
static void fixture(uint8_t p[90]) {
    memset(p,0,90);memcpy(p,"CGA1",4);
    wr32(p+4,1);wr32(p+8,11);wr32(p+12,1);wr32(p+16,32);
    wr32(p+20,80);wr32(p+24,3);wr32(p+28,86);
    wr32(p+32,123);wr32(p+36,11);wr32(p+40,3);wr32(p+44,56);
    wr32(p+48,83);wr32(p+52,3);
    wr32(p+56,83);wr32(p+60,2);
    wr32(p+64,85);wr32(p+68,0);
    wr32(p+72,85);wr32(p+76,1);
    memcpy(p+80,"nsdabc",6);checksum(p,90);
}

int main(int argc,char **argv) {
    uint8_t p[90],bad[91];fixture(p);
    cga_archive a={0};
    assert(cga_bind(&a,p,sizeof(p),11)==0);
    assert(!a.owned&&a.count==1&&a.level==11);
    const uint8_t *section=NULL;uint32_t size=0,index=999;
    cga_entry entry={0};
    assert(cga_entry_at(&a,0,&entry)==0&&entry.eid==123&&entry.type==11&&entry.section_count==3);
    assert(cga_find(&a,123,&index)==0&&index==0);
    assert(cga_find(&a,124,&index)==-1);
    assert(cga_section(&a,0,0,&section,&size)==0&&size==2&&!memcmp(section,"ab",2));
    assert(cga_section(&a,0,1,&section,&size)==0&&size==0);
    assert(cga_section(&a,0,2,&section,&size)==0&&size==1&&*section=='c');
    assert(cga_nsd(&a,&section,&size)==0&&size==3&&!memcmp(section,"nsd",3));
    assert(cga_section(&a,1,0,&section,&size)==-1);
    assert(cga_section(&a,0,3,&section,&size)==-1);
    assert(cga_section(&a,0,0,NULL,&size)==-1);
    assert(cga_entry_at(&a,1,&entry)==-1);
    assert(cga_bind(&a,p,90,10)==-1&&a.data==p);
    assert(cga_open(&a,NULL,11)==-1&&a.data==p);
    for(size_t i=0;i<90;i++) {
        assert(cga_bind(&a,p,i,11)==-1&&a.data==p);
        memcpy(bad,p,90);bad[i]^=1;
        assert(cga_bind(&a,bad,90,11)==-1&&a.data==p);
    }
    memcpy(bad,p,90);bad[90]=0;
    assert(cga_bind(&a,bad,91,11)==-1);
    const uint32_t offsets[]={4,8,12,16,20,24,28,36,40,44,48,52,56,60,64,68,72,76};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
        memcpy(bad,p,90);wr32(bad+offsets[i],UINT32_MAX);checksum(bad,90);
        assert(cga_bind(&a,bad,90,11)==-1&&a.data==p);
    }
    /* Unaligned storage is valid and must not require native struct casts. */
    memcpy(bad+1,p,90);
    assert(cga_bind(&a,bad+1,90,11)==0);
    cga_close(&a);cga_close(&a);
    assert(!a.data&&!a.owned&&a.count==0);
    assert(cga_nsd(&a,&section,&size)==-1);
    assert(cga_bind(NULL,p,90,11)==-1);

    for(int arg=1;arg<argc;arg++) {
        /* Read header only to select the expected level for this test. */
        FILE *f=fopen(argv[arg],"rb");assert(f);
        uint8_t header[32];assert(fread(header,1,32,f)==32);assert(fclose(f)==0);
        assert(cga_open(&a,argv[arg],rd32(header+8))==0&&a.owned==a.data);
        const uint8_t *previous=a.data;
        assert(cga_bind(&a,a.data,a.size,a.level)==-1&&a.data==previous);
        assert(cga_open(&a,argv[arg],64)==-1&&a.data==previous);
        for(uint32_t i=0;i<a.count;i++) {
            assert(cga_entry_at(&a,i,&entry)==0);
            assert(cga_find(&a,entry.eid,&index)==0&&index==i);
            for(uint32_t j=0;j<entry.section_count;j++)
                assert(cga_section(&a,i,j,&section,&size)==0);
        }
        assert(cga_open(&a,argv[arg],a.level)==0);
        cga_close(&a);
    }
    puts("PASS CGA reader: validation, sections, unaligned input, ownership and transactional replacement");
    return 0;
}
