#ifndef GOOL_TREE_H
#define GOOL_TREE_H
#include <stdint.h>

#define GOOL_TREE_ROOTS 8u
#define GOOL_TREE_DEPTH 64u
enum { GOOL_TREE_DONE=0,GOOL_TREE_BUDGET=1,GOOL_TREE_INVALID=-1,
       GOOL_TREE_PROVIDER_PENDING=2,GOOL_TREE_DEPTH_LIMIT=-2,GOOL_TREE_CYCLE=-3 };
/* Native hierarchy fields: type+0, type2 root child+4, object sibling+0x48,
 * object child+0x4c. Handles identify stable engine objects, not host addresses.
 * All providers are required and return zero or a negative error. visit/erase
 * may return PROVIDER_PENDING to suspend traversal; the owner must resume its
 * existing service job on the next callback, never repeat completed effects.
 * No sibling or child is advanced while that service is pending. visit
 * returns a provider status separately from
 * its original engine return value; only native_result==-25 deletes the node.
 * erase implements the actual engine deletion service and owns child cleanup.
 * No implicit activation, deallocation, physics or deletion is simulated. */
typedef struct {
    void *context;
    int (*read)(void *context,uint32_t object,uint32_t offset,uint32_t *value);
    int (*visit)(void *context,uint32_t object,int32_t *native_result);
    int (*erase)(void *context,uint32_t object);
    int (*write)(void *context,uint32_t object,uint32_t offset,uint32_t value);
} gool_tree_services;
typedef struct {uint32_t node,next_child,phase;} gool_tree_frame;
typedef struct {
    gool_tree_services services;
    uint32_t roots[GOOL_TREE_ROOTS],root,next_root_child,depth;
    gool_tree_frame frames[GOOL_TREE_DEPTH];
    int active,root_loaded,status;
} gool_tree;

/* Initialize to {0}; keep cursor/providers stable for the whole traversal.
 * Engine mutations happen only inside providers. Between budget continuations
 * retain the same timeline and all snapshotted sibling handles. Native order:
 * eight roots; root children pre-order; sibling captured before visit; children
 * read after visit; delete(-25) skips descent. Type0 stops a nested child chain,
 * but root-child enumeration itself does not filter type0 nodes.
 * Fixed depth is a port safety limit, not an inferred original object limit.
 * Provider errors are retained and never reported as successful completion. */
int gool_tree_begin(gool_tree *tree,const uint32_t roots[GOOL_TREE_ROOTS],
                    const gool_tree_services *services);
int gool_tree_step(gool_tree *tree,uint32_t work_budget);
/* Native 0x8001962c: remove from the old parent's child/sibling chain, then
 * prepend under new_parent. Both type2 root+4 and object+0x4c heads supported.
 * Returns original success2 or missing-child -24. Negative provider failures
 * retain any already completed writes. scan_limit bounds malformed sibling
 * cycles; -2 means limit exhausted before unlinking. Requires a valid acyclic
 * graph and stable objects; not callable during unrelated concurrent mutation.
 * Only read/write providers are needed by this entry point. */
int gool_tree_reparent(uint32_t node,uint32_t new_parent,
                       const gool_tree_services *services,uint32_t scan_limit);
#endif
