#include "gool_creation_services.h"
static int read_word(void *context,uint32_t address,uint32_t *value) {
    gool_creation_services *s=context;return gool_native_registry_read(s->registry,address,value);
}
static int write_word(void *context,uint32_t address,uint32_t value) {
    gool_creation_services *s=context;return gool_native_registry_write(s->registry,address,value);
}
static int scalar(void *context,uint32_t database,uint32_t key,uint32_t *value) {
    gool_creation_services *s=context;return zdat_native_scalar(s->database,database,key,value);
}
static int path(void *context,uint32_t database,uint32_t *address) {
    gool_creation_services *s=context;return zdat_native_path(s->database,database,address);
}
static int count(void *context,uint32_t database,uint32_t *value) {
    gool_creation_services *s=context;return zdat_native_path_count(s->database,database,value);
}
static int parameters(void *context,uint32_t database,uint32_t *address,uint32_t *count) {
    gool_creation_services *s=context;return zdat_native_parameters(s->database,database,address,count);
}
static int load(void *context,uint32_t address,uint32_t *value) {
    gool_creation_services *s=context;return gool_native_resources_load(s->registry->resources,address,value);
}
static int state(void *context,uint32_t node,uint32_t index,uint32_t argc,uint32_t args,uint32_t *value) {
    gool_creation_services *s=context;return s->engine.enter_state(s->engine.context,node,index,argc,args,value);
}
static int position(void *context,uint32_t node,uint32_t parameter,uint32_t output) {
    gool_creation_services *s=context;return zdat_path_native_create(s->database,node,parameter,output);
}
static int retire(void *context,uint32_t node,uint32_t notify) {
    gool_creation_services *s=context;return s->engine.retire(s->engine.context,node,notify);
}
static int player(void *context,uint32_t node,uint32_t data,uint32_t count,uint32_t argument) {
    gool_creation_services *s=context;return s->engine.player_event(s->engine.context,node,data,count,argument);
}
int gool_creation_services_bind(gool_creation_services *s,gool_native_registry *registry,
    zdat_native *database,const gool_creation_engine *engine,uint32_t usage_address,
    uint32_t usage_count,uint32_t scan_limit,uint32_t max_reclaims) {
    if(!s||s->registry||!registry||!registry->actors||!registry->resources||!registry->resources->open||
       registry->resources->closing||!database||database->domain!=registry->domain||!engine||
       !engine->enter_state||!engine->retire||!engine->player_event||!usage_address||(usage_address&3u)||
       !usage_count||(uint64_t)usage_address+(uint64_t)usage_count*4>UINT32_MAX||!scan_limit||!max_reclaims)
        return GOOL_CREATE_INVALID;
    gool_creation_engine copy=*engine;
    *s=(gool_creation_services){.registry=registry,.database=database,.engine=copy};
    s->services=(gool_create_services){.context=s,.read=read_word,.write=write_word,.scalar=scalar,
        .path=path,.path_count=count,.parameters=parameters,.load=load,.enter_state=state,
        .position=position,.retire=retire,.player_event=player,.protected_address=0x80060a90u,
        .usage_address=usage_address,.usage_count=usage_count,.scan_limit=scan_limit,.max_reclaims=max_reclaims};
    return 0;
}
