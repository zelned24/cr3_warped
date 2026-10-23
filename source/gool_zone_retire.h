#ifndef GOOL_ZONE_RETIRE_H
#define GOOL_ZONE_RETIRE_H
#include "gool_domain.h"
#define GOOL_ZONE_RETIRE_DEPTH 64u
enum {GOOL_ZONE_RETIRE_DONE=0,GOOL_ZONE_RETIRE_PENDING=1,
      GOOL_ZONE_RETIRE_INVALID=-179,GOOL_ZONE_RETIRE_LIMIT=-180};
typedef struct {
    void *context;
    gool_domain *domain;
    uint32_t max_nodes;
    /* Actual1DB38(0,node,1A00,0,0), followed when required by1A844(node,0).
     * Return0 completed,1 pending same invocation,negative sticky failure.
     * The retirement provider owns notification/audio/actor/page quiescence;
     * its output is the actual signed native result, distinct from status.
     * Providers retain all callback jobs, graph/resource owners and output
     * storage until completion or successful cancellation. No no-op defaults.
     */
    int (*signal)(void *context,uint32_t node,uint32_t signal);
    int (*retire)(void *context,uint32_t node,uint32_t notify,int32_t *native_result);
    int (*cancel)(void *context,uint32_t node,int retiring);
} gool_zone_retire_services;
typedef struct {uint32_t node,next_child;int phase,root;} gool_zone_retire_frame;
typedef struct {
    gool_zone_retire_services services;
    gool_zone_retire_frame frames[GOOL_ZONE_RETIRE_DEPTH];
    uint32_t entry,root_index,depth,visited,callback_node;
    int32_t native_result;
    int opened,status,callback_active,retiring;
} gool_zone_retire;
/* Original1AA98 ->19A38 eight roots ->1997C/197B0 postorder traversal with
 * predicate1A9A8. Snapshot siblings before descent; callbacks may alter zone,
 * roots and current object fields exactly as native code does. The caller
 * owns the zone mapping and excludes unrelated graph mutation through close.
 * Each budget unit handles one bounded traversal/predicate/provider stage.
 * A completed callback is never replayed. Failure leaves native effects and
 * pending callback storage owned until close cancels/quiesces it. No heap.
 */
int gool_zone_retire_begin(gool_zone_retire *owner,uint32_t entry,
    const gool_zone_retire_services *services);
int gool_zone_retire_step(gool_zone_retire *owner,uint32_t budget);
int gool_zone_retire_close(gool_zone_retire *owner);
#endif
