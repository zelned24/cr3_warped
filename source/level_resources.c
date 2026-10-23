#include "level_resources.h"
#include "nsf_resource.h"
#include <stdio.h>

static uint8_t metadata[NSD_RESOURCE_MAX_BYTES];
static int owned,level=-1;
static nsd_resource_index metadata_index;

static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}

int level_resources_level(void) {return level;}
int level_resources_nsd(nsd_resource_index *out) {
    if(!owned||!out)return LEVEL_RESOURCES_INVALID;
    *out=metadata_index;return 0;
}

int level_resources_close(void) {
    if(!owned)return 0;
    int error=nsf_resource_close();
    if(error)return error;
    owned=0;level=-1;metadata_index=(nsd_resource_index){0};
    return 0;
}

int level_resources_open(const char *directory,uint32_t level_id) {
    if(!directory||!*directory||level_id>=64)return LEVEL_RESOURCES_INVALID;
    char nsd_path[256],nsf_path[256];
    int length=snprintf(nsd_path,sizeof(nsd_path),"%s/S%07lX.NSD",directory,(unsigned long)level_id);
    if(length<0||(size_t)length>=sizeof(nsd_path))return LEVEL_RESOURCES_INVALID;
    length=snprintf(nsf_path,sizeof(nsf_path),"%s/S%07lX.NSF",directory,(unsigned long)level_id);
    if(length<0||(size_t)length>=sizeof(nsf_path))return LEVEL_RESOURCES_INVALID;
    int error=level_resources_close();
    if(error)return error;

    FILE *file=fopen(nsd_path,"rb");
    if(!file)return LEVEL_RESOURCES_IO;
    size_t size=fread(metadata,1,sizeof(metadata),file);
    int extra=fgetc(file),failed=ferror(file),closed=fclose(file);
    if(failed||closed)return LEVEL_RESOURCES_IO;
    if(extra!=EOF)return LEVEL_RESOURCES_FORMAT;
    nsd_resource_index index;
    error=nsd_resource_bind(&index,metadata,size);
    if(error)return error;

    /* Worker open itself is asynchronous. Reject missing/truncated raw files
     * at the loading boundary rather than advertising unavailable resources. */
    file=fopen(nsf_path,"rb");
    if(!file)return LEVEL_RESOURCES_IO;
    failed=fseek(file,0,SEEK_END);
    long bytes=failed?-1:ftell(file);
    closed=fclose(file);
    if(failed||bytes<0||closed)return LEVEL_RESOURCES_IO;
    if(!bytes||(uint64_t)bytes%NSF_PAGE_BYTES)return LEVEL_RESOURCES_FORMAT;
    for(uint32_t i=0;i<index.count;i++) {
        uint32_t cid=read32(metadata+0x520+i*8);
        if(((uint64_t)(cid>>1)+1)*NSF_PAGE_BYTES>(uint64_t)bytes)
            return LEVEL_RESOURCES_FORMAT;
    }
    error=nsf_resource_open_raw(nsf_path,metadata,size);
    if(error)return error;
    owned=1;level=(int)level_id;metadata_index=index;
    return 0;
}
