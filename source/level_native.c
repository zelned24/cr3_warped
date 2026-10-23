#include "level_native.h"
#include "level_behaviors.h"
#include "level_resources.h"
#include "gool_native_pool.h"
#include "gool_native_bounds.h"
#include "gool_native_shading.h"
#include <string.h>

static gool_domain domain;
static zdat_native database={.domain=&domain,.chain_limit=ZDAT_NATIVE_CHAIN_MAX};
static gool_native_pool pool;
static zdat_resources zones;
static model_resources models;
static uint8_t model_storage[ZDAT_RESOURCES_SLOTS*ZDAT_RESOURCE_BYTES];
static uint8_t zone_storage[ZDAT_RESOURCES_SLOTS*ZDAT_RESOURCE_BYTES];
static gool_native_registry registry;
static gool_native_actor *actors[GOOL_NATIVE_POOL_COUNT+1];
static uint8_t constants[262144],operand_scratch[8];
static uint8_t bounds_registers[8],bounds_storage[0xa20];
static gool_domain_lease bounds_leases[2];
static uint32_t bounds_mapped;
static gool_domain_lease shading_lease;
static int shading_mapped;
static gool_native_resources resources;
static uint8_t metadata[NSD_RESOURCE_MAX_BYTES],nsd_pointer[4];
static gool_domain_lease metadata_lease,pointer_lease,scratch_lease;
static int level_id=-1,metadata_mapped,pointer_mapped,scratch_mapped;

zdat_resources *level_native_zones(void){return level_id>=0&&zones.open&&!zones.closing?&zones:NULL;}
model_resources *level_native_models(void){return level_id>=0&&models.open&&!models.closing?&models:NULL;}
zdat_native *level_native_database(void){return level_id>=0?&database:NULL;}
gool_native_registry *level_native_registry(void){return level_id>=0&&registry.actors?&registry:NULL;}
gool_native_pool *level_native_pool(void){return level_id>=0&&pool.open?&pool:NULL;}
int level_native_level(void){return level_id;}
gool_domain *level_native_domain(void){return level_id>=0?&domain:NULL;}
gool_native_resources *level_native_resources(void) {
    return level_id>=0&&resources.open&&!resources.closing?&resources:NULL;
}
int level_native_close(void) {
    uint32_t references;int error;
    if(shading_mapped) {
        error=gool_domain_references(&domain,shading_lease,&references);if(error)return error;
        if(references!=1)return GOOL_NATIVE_RESOURCE_BUSY;
    }
    for(uint32_t i=0;i<bounds_mapped;i++) {
        error=gool_domain_references(&domain,bounds_leases[i],&references);if(error)return error;
        if(references!=1)return GOOL_NATIVE_RESOURCE_BUSY;
    }
    if(metadata_mapped) {
        error=gool_domain_references(&domain,metadata_lease,&references);if(error)return error;
        if(references!=1)return GOOL_NATIVE_RESOURCE_BUSY;
    }
    if(pointer_mapped) {
        error=gool_domain_references(&domain,pointer_lease,&references);if(error)return error;
        if(references!=1)return GOOL_NATIVE_RESOURCE_BUSY;
    }
    if(scratch_mapped) {
        error=gool_domain_references(&domain,scratch_lease,&references);if(error)return error;
        if(references!=1)return GOOL_NATIVE_RESOURCE_BUSY;
    }
    error=model_resources_can_close(&models);if(error)return error;
    error=zdat_resources_can_close(&zones);if(error)return error;
    error=gool_native_registry_can_close(&registry);if(error)return error;
    error=gool_native_pool_can_close(&pool);if(error)return error;
    error=gool_native_resources_can_close(&resources);if(error)return error;
    uint32_t owned=(uint32_t)metadata_mapped+(uint32_t)pointer_mapped+(uint32_t)scratch_mapped+
        (uint32_t)shading_mapped+pool.mapped+bounds_mapped,live=0;
    for(uint32_t i=0;i<ZDAT_RESOURCES_SLOTS;i++)owned+=zones.slots[i].mapped!=0;
    for(uint32_t i=0;i<ZDAT_RESOURCES_SLOTS;i++)owned+=models.slots[i].mapped!=0;
    for(uint32_t i=0;i<resources.count;i++)for(unsigned part=0;part<4;part++)
        owned+=(resources.behaviors[i].mapped&(1u<<part))!=0;
    for(uint32_t i=0;i<GOOL_DOMAIN_REGIONS;i++)live+=domain.regions[i].references!=0;
    if(live!=owned)return GOOL_NATIVE_RESOURCE_BUSY;
    error=model_resources_close(&models);if(error)return error;
    error=zdat_resources_close(&zones);if(error)return error;
    error=gool_native_registry_close(&registry);if(error)return error;
    error=gool_native_pool_close(&pool);if(error)return error;
    error=gool_native_resources_close(&resources);if(error)return error;
    while(bounds_mapped) {
        error=gool_domain_release(&domain,bounds_leases[bounds_mapped-1]);if(error)return error;
        bounds_mapped--;
    }
    if(shading_mapped) {
        error=gool_domain_release(&domain,shading_lease);if(error)return error;shading_mapped=0;
    }
    if(scratch_mapped) {
        error=gool_domain_release(&domain,scratch_lease);if(error)return error;scratch_mapped=0;
    }
    if(pointer_mapped) {
        error=gool_domain_release(&domain,pointer_lease);if(error)return error;pointer_mapped=0;
    }
    if(metadata_mapped) {
        error=gool_domain_release(&domain,metadata_lease);if(error)return error;metadata_mapped=0;
    }
    if(gool_domain_empty(&domain)!=1)return GOOL_NATIVE_RESOURCE_BUSY;
    level_id=-1;return 0;
}
int level_native_open(uint32_t level) {
    if(level>=64||level_resources_level()!=(int)level||level_behaviors_level()!=(int)level)
        return GOOL_NATIVE_RESOURCE_INVALID;
    int error=level_native_close();if(error)return error;
    nsd_resource_index nsd;error=level_resources_nsd(&nsd);if(error)return error;
    uint64_t tail=0x520u+(uint64_t)nsd.count*8;
    if(nsd.size>sizeof(metadata)||tail+0x210>nsd.size)return GOOL_NATIVE_RESOURCE_INVALID;
    memcpy(metadata,nsd.data,nsd.size);
    uint32_t address=0x88000000u+(uint32_t)tail;
    for(unsigned i=0;i<4;i++)nsd_pointer[i]=(uint8_t)(address>>(i*8));
    error=gool_domain_map_mutable(&domain,0x88000000u,metadata,nsd.size,&metadata_lease);
    if(error)return error;
    metadata_mapped=1;
    error=gool_domain_map_mutable(&domain,0x80061044u,nsd_pointer,sizeof(nsd_pointer),&pointer_lease);
    if(error)goto failed;
    pointer_mapped=1;
    error=level_behaviors_bind_native(&resources,&domain,0x90000000u,constants,sizeof(constants));if(error)goto failed;
    error=gool_domain_map_mutable(&domain,0x1f800018u,operand_scratch,sizeof(operand_scratch),&scratch_lease);
    if(error)goto failed;
    scratch_mapped=1;
    error=gool_native_shading_map(&domain,&shading_lease);if(error)goto failed;
    shading_mapped=1;
    error=gool_native_pool_open(&pool,&domain);if(error)goto failed;
    error=gool_domain_map_mutable(&domain,0x80068ef0u,bounds_registers,sizeof(bounds_registers),&bounds_leases[0]);
    if(error)goto failed;
    bounds_mapped=1;
    error=gool_domain_map_mutable(&domain,0x80069228u,bounds_storage,sizeof(bounds_storage),&bounds_leases[1]);
    if(error)goto failed;
    bounds_mapped=2;
    error=gool_native_bounds_reset(&domain);if(error)goto failed;
    error=gool_native_registry_bind(&registry,&domain,&resources,actors,GOOL_NATIVE_POOL_COUNT+1);
    if(error)goto failed;
    error=zdat_resources_open(&zones,&domain,zone_storage,0xb0000000u);if(error)goto failed;
    registry.zones=&zones;
    error=model_resources_open(&models,&domain,model_storage,0xb1000000u);if(error)goto failed;
    registry.models=&models;
    level_id=(int)level;return 0;
failed: {
        int cleanup=level_native_close();return cleanup?cleanup:error;
    }
}
