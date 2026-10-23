#include "level_behaviors.h"
#include "level_resources.h"
#include "gool_catalog.h"
#include <stdio.h>

static uint8_t code_pool[LEVEL_BEHAVIOR_SLOTS*GOOL_RESOURCE_SLOT_BYTES];
static gool_resources bank;
static gool_catalog catalog;
static uint32_t next_behavior;
static int level_id=-1,phase=GOOL_RESOURCES_UNAVAILABLE;

int level_behaviors_level(void){return level_id;}
int level_behaviors_close(void) {
    int error=gool_resources_close(&bank);if(error)return error;
    next_behavior=0;level_id=-1;phase=GOOL_RESOURCES_UNAVAILABLE;return 0;
}
int level_behaviors_open(const char *directory,uint32_t level) {
    if(!directory||!*directory||level>=64||level_resources_level()!=(int)level)
        return GOOL_RESOURCES_INVALID;
    char path[256];int length=snprintf(path,sizeof(path),"%s/lv_%02lX.glc",directory,(unsigned long)level);
    if(length<0||(size_t)length>=sizeof(path))return GOOL_RESOURCES_INVALID;
    int error=level_behaviors_close();if(error)return error;
    nsd_resource_index nsd;error=level_resources_nsd(&nsd);if(error)return error;
    error=gool_catalog_load(&catalog,path,level,&nsd);if(error)return error;
    if(!catalog.count||catalog.count>LEVEL_BEHAVIOR_SLOTS)return GOOL_RESOURCES_FULL;
    error=gool_resources_open(&bank,catalog.entries,catalog.count,code_pool,LEVEL_BEHAVIOR_SLOTS);
    if(error)return error;
    next_behavior=0;level_id=(int)level;phase=LEVEL_BEHAVIORS_LOADING;return 0;
}
int level_behaviors_poll(void) {
    if(level_id<0)return GOOL_RESOURCES_UNAVAILABLE;
    if(phase!=LEVEL_BEHAVIORS_LOADING)return phase;
    while(next_behavior<catalog.count) {
        int error=gool_resources_request(&bank,catalog.entries[next_behavior].eid);
        if(error==NSF_STREAM_FULL)break;
        if(error){phase=error;return error;}
        next_behavior++;
    }
    int loading=gool_resources_poll(&bank);
    if(loading<0){phase=loading;return loading;}
    if(next_behavior==catalog.count&&!loading)phase=LEVEL_BEHAVIORS_READY;
    return phase;
}
int level_behaviors_get(uint32_t eid,gool_data *out) {
    if(level_id<0||phase!=LEVEL_BEHAVIORS_READY)return GOOL_RESOURCES_UNAVAILABLE;
    return gool_resources_get(&bank,eid,out);
}
int level_behaviors_retain(uint64_t *generation) {
    if(level_id<0||phase!=LEVEL_BEHAVIORS_READY)return GOOL_RESOURCES_UNAVAILABLE;
    return gool_resources_retain(&bank,generation);
}
int level_behaviors_release(uint64_t generation){return gool_resources_release(&bank,generation);}
int level_behaviors_bind_native(gool_native_resources *resources,gool_domain *domain,uint32_t base,void *constants,uint32_t capacity) {
    if(level_id<0||phase!=LEVEL_BEHAVIORS_READY)return GOOL_RESOURCES_UNAVAILABLE;
    return gool_native_resources_open(resources,&bank,domain,base,constants,capacity);
}
