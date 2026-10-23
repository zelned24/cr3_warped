#ifndef GOOL_NATIVE_ACTIVATION_QUERY_H
#define GOOL_NATIVE_ACTIVATION_QUERY_H
#include "gool_native_activation_path.h"
#include "zdat_resources.h"

typedef struct {
    zdat_native source;
    zdat_resources *bank;
    zdat_resource_lease leases[ZDAT_RESOURCES_SLOTS];
    uint32_t zones[ZDAT_RESOURCES_SLOTS],count;
    int active,status;
} gool_native_activation_query;
/* Resident native32710 mode1 provider. Retains the actual owning zones of
 * every linked database until cancel, including failed partial acquisitions.
 * Caller supplies a stable zero-initialized owner and an open zone bank.
 * No SD fallback, temporary output pointers, default list or fabricated count.
 * Configure source.domain, source.chain_limit and bank before first begin. */
int gool_native_activation_query_begin(void *context,uint32_t database,uint32_t key,
    uint32_t position,uint32_t *list,uint32_t *count);
int gool_native_activation_query_step(void *context,uint32_t budget);
int gool_native_activation_query_cancel(void *context);
#endif
