#include "gool_catalog.h"
#include <stdio.h>
#include <string.h>
static gool_catalog staging;
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint32_t crc(uint32_t c,const uint8_t *bytes,size_t size) {
    for(size_t i=0;i<size;i++) {
        c^=bytes[i];
        for(unsigned bit=0;bit<8;bit++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));
    }
    return c;
}
int gool_catalog_load(gool_catalog *out,const char *path,uint32_t level,const nsd_resource_index *nsd) {
    if(!out||!path||level>=64||!nsd||!nsd->data)return GOOL_RESOURCES_INVALID;
    FILE *file=fopen(path,"rb");if(!file)return GOOL_RESOURCES_UNAVAILABLE;
    uint8_t header[16],record[8],sum[4];int error=GOOL_RESOURCES_INVALID;
    if(fread(header,1,sizeof(header),file)!=sizeof(header)||memcmp(header,"GLC1",4)||
       rd32(header+4)!=1||rd32(header+8)!=level||rd32(header+12)>1024)goto done;
    staging.level=level;staging.count=rd32(header+12);
    uint32_t checksum=crc(UINT32_MAX,header,sizeof(header));
    for(uint32_t i=0;i<staging.count;i++) {
        if(fread(record,1,sizeof(record),file)!=sizeof(record))goto done;
        checksum=crc(checksum,record,sizeof(record));
        uint32_t eid=rd32(record),lookup=rd32(record+4),cid;
        if(!(eid&1u)||lookup!=i||nsd_resource_find(nsd,eid,&cid))goto done;
        for(uint32_t j=0;j<i;j++)if(staging.entries[j].eid==eid)goto done;
        staging.entries[i]=(gool_resource_catalog){.eid=eid,.lookup_index=lookup};
    }
    if(fread(sum,1,sizeof(sum),file)!=sizeof(sum)||rd32(sum)!=~checksum||fgetc(file)!=EOF||ferror(file))goto done;
    error=0;
done:
    if(fclose(file)&&!error)error=GOOL_RESOURCES_UNAVAILABLE;
    if(!error)*out=staging;
    return error;
}
