#ifndef GOOL_NATIVE_ACTIVATION_PATH_H
#define GOOL_NATIVE_ACTIVATION_PATH_H
#include "gool_native_activation.h"

typedef struct {
    void *context;
    /* Original32710(database,key,position,0,&count,1,0,0,0).
     * Begin once; write actual native list/count results before returning0.
     * Output storage is stable until cancel. The returned mapped list remains
     * retained through registration, including partial-write failures. Cancel
     * must quiesce pending work before releasing that ownership. */
    int (*query_begin)(void *context,uint32_t database,uint32_t key,uint32_t position,
                       uint32_t *list,uint32_t *count);
    int (*query_step)(void *context,uint32_t budget);
    int (*query_cancel)(void *context);
} gool_native_activation_path_services;

typedef struct {
    gool_native_activation_path_services services;
    gool_native_activation registration;
    gool_domain *domain;
    uint32_t gp,capacity,database,position,direction,list,count,phase;
    int opened,status,query_owned,query_completed;
} gool_native_activation_path;

/* Original1AD68: query the neighboring sample and then the current sample;
 * register their lists with13B and13C respectively. Direction2 uses the prior
 * sample/key13B and current key13C; all other directions use next/key13C and
 * current key13B. Position arithmetic wraps as native ADDIU. This native void
 * routine has no application return value; status reports completion/failure.
 * Retain actual database, table and GP mappings through close. Initialize0;
 * keep the owner and output addresses fixed across asynchronous suspension. */
int gool_native_activation_path_begin(gool_native_activation_path *owner,
    gool_domain *domain,uint32_t gp,uint32_t capacity,uint32_t database,
    uint32_t position,uint32_t direction,const gool_native_activation_path_services *services);
int gool_native_activation_path_step(gool_native_activation_path *owner,uint32_t budget);
int gool_native_activation_path_close(gool_native_activation_path *owner);
#endif
