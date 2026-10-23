#ifndef GOOL_SEARCH_H
#define GOOL_SEARCH_H
#include <stdint.h>
#define GOOL_SEARCH_DEPTH 64u
enum {GOOL_SEARCH_DONE=0,GOOL_SEARCH_BUDGET=1,GOOL_SEARCH_INVALID=-86,
      GOOL_SEARCH_DEPTH_LIMIT=-87,GOOL_SEARCH_CYCLE=-88,GOOL_SEARCH_LIMIT=-89};
typedef int (*gool_search_read)(void *context,uint32_t address,uint32_t *value);
typedef int (*gool_search_predicate)(void *context,uint32_t node,int *matched);
typedef struct {uint32_t node,next_child;int opened;} gool_search_frame;
typedef struct {
    void *context;gool_search_read read;
    gool_search_frame frames[GOOL_SEARCH_DEPTH];
    uint32_t depth,mask,result,visited,max_nodes;
    int active,status,serial_match,predicate_resumable;
    gool_search_predicate predicate;
    void *predicate_context;
} gool_search;
/* Original19844 with concrete predicate19CC0: first object whose +100 intersects
 * mask, preorder. Type2 roots skip the predicate. Child sibling is captured
 * before descent. All handles belong to an explicit stable address domain.
 * No allocation or arbitrary replacement policy. Limits bound malformed graphs.
 * Initialize caller storage to{0}; keep graph stable through budget suspension. */
int gool_search_begin(gool_search *search,uint32_t root,uint32_t mask,uint32_t max_nodes,
                        gool_search_read read,void *context);
int gool_search_step(gool_search *search,uint32_t budget);
/* Explicit resumable predicates may return2 to suspend the current node.
 * The predicate owns its pending job; subsequent polls must resume it before
 * committing matched/child effects. Ordinary predicates still reject2. */
/* Bounded synchronous adapter for creation's synchronous search boundary.
 * Output remains unchanged on failure. Prefer the cursor for frame scheduling. */
int gool_search_mask(uint32_t root,uint32_t mask,uint32_t max_nodes,
                       gool_search_read read,void *context,uint32_t *result);
/* Same19844 traversal with concrete19AAC serial predicate object+B8==serial.
 * Type2 roots still skip predicate evaluation. Matching serial0 is explicit. */
int gool_search_serial(uint32_t root,uint32_t serial,uint32_t max_nodes,
                       gool_search_read read,void *context,uint32_t *result);
/* Concrete synchronous19844 composition for stateful native predicates.
 * Predicate returns0 with matched0 to continue (including nearest searches),
 * matched1 to return the node, or a negative provider error. Type2 skips it.
 * A completed predicate is never replayed; positive status is invalid. */
int gool_search_visit(uint32_t root,uint32_t max_nodes,gool_search_read read,
    void *read_context,gool_search_predicate predicate,void *predicate_context,uint32_t *result);
#endif
