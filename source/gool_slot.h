#ifndef GOOL_SLOT_H
#define GOOL_SLOT_H
#include "gool_admission.h"

enum {GOOL_SLOT_DONE=0,GOOL_SLOT_BUDGET=1,GOOL_SLOT_INVALID=-82,GOOL_SLOT_LIMIT=-83};
typedef struct {
    void *context;
    int (*read)(void *context,uint32_t address,uint32_t *value);
    int (*write)(void *context,uint32_t address,uint32_t value);
    /* Original19844 search with callback19CC0 and caller-supplied flags.
     * Returns the actual candidate handle,0 when not found; no guessed victim. */
    int (*search)(void *context,uint32_t root,uint32_t predicate,uint32_t flags,uint32_t *candidate);
    /* Original1AA78(candidate,0) and1962C(slot,parent). Each callback completes
     * its real engine operation before returning0; errors are provider errors,
     * not their native return words (creation ignores those native words).
     * Resource/VM/DMA lifetime belongs to the retirement provider.1AA78 ignores
     * its second caller argument and forces notify1 into1A844; passing0 here
     * must not disable native deletion notification in that provider. */
    int (*retire)(void *context,uint32_t candidate,uint32_t caller_argument);
    int (*reparent)(void *context,uint32_t slot,uint32_t parent);
    uint32_t protected_address,max_reclaims;
} gool_slot_services;
typedef struct {
    gool_slot_services services;
    gool_admission_result admission;
    uint32_t slot,parent,candidate,root_index,root,reclaims,phase;
    int active,status;
    int32_t native_result; /* -22 when all eight searches find no candidate */
} gool_slot;

/* Original creation1A084..1A154, following a successful admission exactly once.
 * Caller keeps admission/database/engine stable through budget continuation.
 * Selects reserved player slot or current free-list head; searches roots7..0
 * and retires real candidates before re-reading the free list. The original
 * mutable search-mode global is re-read for every root. Reparent completes
 * before DONE; no object initialization or occupancy publication is implied.
 * Cursor remains at stable caller storage, initially{0}; no allocations.
 * Budget counts owner operations, not native ticks. max_reclaims bounds a
 * provider that fails to return a free slot. Errors are sticky; earlier
 * protected-pointer, retirement and hierarchy effects are retained.
 */
int gool_slot_begin(gool_slot *cursor,const gool_admission_result *admission,
                      const gool_slot_services *services);
int gool_slot_step(gool_slot *cursor,uint32_t budget);
#endif
