#ifndef GOOL_NATIVE_ROUTER_H
#define GOOL_NATIVE_ROUTER_H
#include "gool_native_delivery.h"
#include "gool_native_geometry_search.h"
#define GOOL_NATIVE_ROUTER_DEPTH 16u
enum {GOOL_NATIVE_ROUTER_DONE=0,GOOL_NATIVE_ROUTER_BUDGET=1,
      GOOL_NATIVE_ROUTER_INVALID=-122,GOOL_NATIVE_ROUTER_LIMIT=-123};
typedef struct {
    gool_native_state state;
    gool_native_delivery delivery;
    gool_native_geometry_search geometry;
    gool_native_dispatch *outer;
    gool_native_request *request;
    uint64_t request_generation;
    uint32_t kind;
    gool_native_factory *factory;
    gool_initialize spawn_initialize;
    uint32_t spawn_phase,spawn_node,spawn_count,spawn_index,spawn_argc,spawn_drop,spawn_arguments;
    int occupied;
} gool_native_route_frame;
typedef struct {
    gool_native_factory *factory;
    gool_native_dispatch *root;
    gool_native_delivery *engine_delivery;
    gool_native_dispatch *overflow;
    gool_native_request *overflow_request;
    uint64_t overflow_generation;
    gool_native_route_frame *frames;
    uint32_t capacity,depth,arguments_base;
    int bound,active,status,root_retained;
    int32_t native_result;
} gool_native_router;
/* Stable caller-owned fixed call frames initially{0}; no C recursion/heap.
 * Actual engine dispatch drives SEND/STATE/F00 geometry continuations in depth-first
 * order, including nested work inside RESUME/PRE_RUN/on_signal. Each depth
 * owns its native-state/delivery cursors and a disjoint28-byte argument span
 * at arguments_base+depth*256. Capacity<=16 is a checked port work limit.
 * Retains the root actor until completion/explicit cleanup. Factory, source
 * zone/code owners and engine timeline remain stable across continuations.
 * No UPDATE/token gate, frame timing, traversal, rendering or activation is
 * invented here. Same-actor state arguments use distinct retained factory
 * records; cancellation checks the captured request generation and LIFO owner.
 */
int gool_native_router_bind(gool_native_router *router,gool_native_factory *factory,
    gool_native_route_frame *frames,uint32_t capacity,uint32_t arguments_base);
int gool_native_router_begin(gool_native_router *router,gool_native_dispatch *root);
/* Work budget counts one root instruction or one bounded child-service step.
 * Errors are sticky and preserve all current call/resource owners for cleanup. */
int gool_native_router_step(gool_native_router *router,uint32_t budget);
/* Drive a direct1DB38 engine delivery and its real nested SEND/STATE jobs.
 * Router and delivery must use the same factory and dedicated stable owners.
 * Each unit resumes one instruction/service stage; nested dispatch finishes
 * before the parent handler resumes. No opcode37EDC caller, implicit signal
 * acknowledgement, default state or synthetic return is constructed.
 * On failure keep both owners; close the router, then state and delivery in
 * deepest-first order. Do not reuse this router for another active root.
 */
int gool_native_router_engine_delivery_step(gool_native_router *router,
    gool_native_delivery *delivery,uint32_t budget);
/* Unwind deepest-first. BUSY preserves the remaining frames when an external
 * nested request/borrow still owns storage; complete/cancel that owner first.
 * Cancels retained factory requests belonging to unwound STATE frames without
 * popping tokens, reporting success or fabricating a delivery acknowledgement.
 * Caller remains responsible for aborting its root dispatch after cancellation.
 */
int gool_native_router_close(gool_native_router *router);
#endif
