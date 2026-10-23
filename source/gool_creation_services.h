#ifndef GOOL_CREATION_SERVICES_H
#define GOOL_CREATION_SERVICES_H
#include "gool_create.h"
#include "gool_native_actor.h"
#include "zdat_path_native.h"
typedef struct {
    void *context;
    int (*enter_state)(void *context,uint32_t node,uint32_t state,uint32_t argc,
                         uint32_t arguments,uint32_t *native_result);
    int (*retire)(void *context,uint32_t node,uint32_t notify);
    int (*player_event)(void *context,uint32_t node,uint32_t data,uint32_t count,uint32_t argument);
} gool_creation_engine;
typedef struct {
    gool_native_registry *registry;
    zdat_native *database;
    gool_creation_engine engine;
    gool_create_services services;
} gool_creation_services;
/* Concrete composition: registered object reads/writes and lifetime transfers,
 * original database scalar/path/count/parameters, retained native behavior load,
 * full ZDAT path positioning. State, retirement and player events require the
 * actual engine services and remain explicit (never a no-op success).
 * Owner/callback contexts stay stable across gool_create continuation. Retain
 * the source zone, code bank, pool and timeline through creation and object
 * lifetime; this composition does not acquire ownership from raw pointers.
 * Usage globals must be mapped/initialized by their original engine owner.
 * Only state provider may return PENDING, resuming its existing job.
 */
int gool_creation_services_bind(gool_creation_services *owner,gool_native_registry *registry,
    zdat_native *database,const gool_creation_engine *engine,uint32_t usage_address,
    uint32_t usage_count,uint32_t scan_limit,uint32_t max_reclaims);
/* Do not reset while a caller-owned creation cursor still borrows this owner. */
#endif
