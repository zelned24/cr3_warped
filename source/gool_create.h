#ifndef GOOL_CREATE_H
#define GOOL_CREATE_H
#include "gool_initialize.h"
#include "gool_slot.h"

enum {GOOL_CREATE_DONE=0,GOOL_CREATE_BUDGET=1,GOOL_CREATE_INVALID=-90,GOOL_CREATE_RANGE=-91};
typedef struct {
    void *context;
    int (*read)(void *context,uint32_t address,uint32_t *value);
    int (*write)(void *context,uint32_t address,uint32_t value);
    int (*scalar)(void *context,uint32_t database,uint32_t key,uint32_t *value);
    int (*path)(void *context,uint32_t database,uint32_t *address);
    /* Native32EDC(db,4B,0,2), native32710(db,A4,0,0,&count,2,0,0,0). */
    int (*path_count)(void *context,uint32_t database,uint32_t *count);
    int (*parameters)(void *context,uint32_t database,uint32_t *address,uint32_t *count);
    int (*load)(void *context,uint32_t class_word_address,uint32_t *native_result);
    int (*enter_state)(void *context,uint32_t node,uint32_t state,uint32_t argc,
                         uint32_t arguments,uint32_t *native_result);
    /* Full20670(node,0,node+60), including its flags/orientation side effects;
     * a position-only helper is insufficient. Return status, not native result. */
    int (*position)(void *context,uint32_t node,uint32_t parameter,uint32_t output_address);
    /* Actual1A844 candidate retirement, notify1 forced by original1AA78.
     * Finish notifications, children, audio and borrowed resources before0. */
    int (*retire)(void *context,uint32_t node,uint32_t notify);
    /* Actual2FF3C(node,80065DD8,1,0) protected-player special initialization. */
    int (*player_event)(void *context,uint32_t node,uint32_t data,uint32_t count,uint32_t argument);
    uint32_t protected_address,usage_address,usage_count,scan_limit,max_reclaims;
} gool_create_services;
typedef struct {
    gool_create_services services;
    gool_admission_result admission;
    gool_slot slot;
    gool_initialize initialize;
    uint32_t entry,entity_index,node,parameters,parameter_count,class_index,object_id,phase;
    int active,status,tree_provider_error;
    int32_t native_result;
} gool_create;

/* Complete original19CE4..1A428 creation orchestration composed with admission,
 * slot selection, concrete19844/19CC0 search, concrete1962C reparent and1A5C8
 * initializer. Includes lighting reset, parameters, native init failure unlink,
 * occupancy publication, full path service boundary, player checkpoint override,
 * category300 reparent and class34 chain/height finalization.
 * Providers must bind actual engine/resource/VM services, not substitute success.
 * Only enter_state may return GOOL_INITIALIZE_PROVIDER_PENDING while
 * continuing the same owned job. Other callbacks must finish with0 or a
 * negative status before returning. Native init/load/state results have separate output words.
 * Caller owns stable cursor, address overlays, graph and all resource leases
 * throughout continuation and object lifetime. No heap allocation or flat spawn.
 * Budget is owner work, not game ticks. Errors are sticky, without rollback.
 * Native DONE result is the created object handle or original negative result;
 * DONE alone does not mean an object was created. Output remains in cursor.
 * This owner is not yet the game's activation/zone scheduling policy. */
int gool_create_begin(gool_create *create,uint32_t entry,uint32_t entity_index,
                        const gool_create_services *services);
int gool_create_step(gool_create *create,uint32_t budget);
#endif
