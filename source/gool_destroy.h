#ifndef GOOL_DESTROY_H
#define GOOL_DESTROY_H
#include "gool_tree.h"

enum {GOOL_DESTROY_DONE=0,GOOL_DESTROY_BUDGET=1,GOOL_DESTROY_PROVIDER_PENDING=2,GOOL_DESTROY_INVALID=-1,
      GOOL_DESTROY_DEPTH=-2,GOOL_DESTROY_CYCLE=-3,GOOL_DESTROY_USAGE=-4};
/* Engine-owned globals/pool metadata, not invented object state. read/write
 * address the same stable objects as the hierarchy services. notify delivers
 * original signal0x1a00 with no arguments; stop_audio releases voices owned by
 * the object. Both callbacks are required and return zero, PROVIDER_PENDING,
 * or a negative error. On PENDING the owner resumes its existing job at the
 * next invocation, preserving effects already produced; never restart it.
 * The actual signal/audio bodies are separate services, never implicit no-ops.
 * Original globals gp+0x218 and 0x80068e98 may change inside those callbacks.
 * usage is the original occupancy-word table (0x8006b688), indexed by signed
 * object+0xb8 >>8. free_root is the original free-list root (0x80068ee0).
 * Keep all storage and snapshotted siblings stable through completion/error;
 * quiesce borrowed VM/DMA spans before retirement. No object is heap-freed. */
typedef struct {
    gool_tree_services links;
    uint32_t *protected_object,*detached_object,*usage;
    uint32_t usage_count,free_root,scan_limit;
    int (*notify)(void *context,uint32_t object,uint32_t signal);
    int (*stop_audio)(void *context,uint32_t object);
} gool_destroy_services;
typedef struct {uint32_t node,next_child,phase;} gool_destroy_frame;
typedef struct {
    gool_destroy_services services;
    gool_destroy_frame frames[GOOL_TREE_DEPTH];
    uint32_t depth,notify;
    int active,status;
} gool_destroy;

/* Bounded translation of original 0x8001a844. Initialize cursor to {0}; only
 * mutate engine graph/globals inside providers while it is active. Notification
 * precedes recursive child retirement; audio stop precedes occupancy/type/list
 * changes. Native missing-child -24 from reparent is ignored exactly as in the
 * original deletion routine; provider failures remain sticky, with partial
 * effects retained. DONE is a port completion status, not native return -255.
 * Budget limits are work units, not PS1 ticks. No rollback or automatic retry. */
int gool_destroy_begin(gool_destroy *cursor,uint32_t object,uint32_t notify,
                       const gool_destroy_services *services);
int gool_destroy_step(gool_destroy *cursor,uint32_t work_budget);
#endif
