#ifndef GOOL_NATIVE_STATE_H
#define GOOL_NATIVE_STATE_H
#include "gool_state_native.h"
#include "gool_native_dispatch.h"
#include "gool_native_pool.h"
#include "gool_native_factory.h"
typedef struct {
    void *context;
    /* Supply a real configured actor only when native RESUME/PRE_RUN requires
     * dispatch and creation has not attached it yet. Must retain its native
     * code/zone/stack and actual engine VM services, stopped on return. */
    int (*actor)(void *context,uint32_t node,gool_native_actor **actor);
} gool_native_state_engine;
typedef struct {
    gool_native_registry *registry;gool_native_pool *pool;
    gool_native_state_engine engine;
    gool_state_native state;gool_native_dispatch dispatch;
    gool_native_actor *actor;
    uint64_t resource_generation;
    uint32_t instruction_budget;
    int bound,active,resource_retained,actor_retained,dispatch_started,status;
    gool_native_factory *request_factory;
    gool_native_request *apply_request;
    uint64_t apply_generation;
    gool_native_dispatch *outer_dispatch;
    uint32_t apply_phase,apply_node;
    int32_t apply_result;
    int apply_finish;
} gool_native_state;
/* One stable owner per nested state invocation, normally from a fixed engine
 * call pool. Never reuse this owner while active or while leases remain.
 * Uses concrete domain/registry, native class loader, object extents and frame
 * service. Pending real dispatch retains this cursor and its native resources.
 * Source zone/argument spans remain caller-owned for the entire invocation.
 * This is not a fabricated initial actor or implicit signal-delivery service. */
int gool_native_state_bind(gool_native_state *owner,gool_native_registry *registry,
    gool_native_pool *pool,const gool_native_state_engine *engine,uint32_t instruction_budget);
/* gool_create enter_state-compatible callback:0 publishes actual native result;
 * 1 means keep polling this exact invocation. Different arguments while active
 * reject before mutating the existing job. Errors preserve result output and
 * retain cleanup ownership until close. Native errors remain result words. */
int gool_native_state_enter(void *context,uint32_t node,uint32_t state,uint32_t argc,
                             uint32_t arguments,uint32_t *native_result);
/* Expose stopped SEND/STATE handoff to the real engine. Consume its actual
 * request, finish nested work, then call gool_native_dispatch_resume on it.
 * No automatic acknowledgements, state success or token discard is supplied. */
gool_native_dispatch *gool_native_state_pending(gool_native_state *owner);
/* Route a stopped factory STATE_REQUEST through this real state owner, then
 * perform native37BEC's completion/token rule and resume/finish outer dispatch.
 * Returns1 while state setup or nested execution remains pending,0 when routed.
 * One owner per nested invocation; pending() exposes nested SEND/STATE work.
 * Keep the same factory/outer cursor until completion. No replay of completed
 * state setup or request-release effects on budget continuation. */
int gool_native_state_apply(gool_native_state *owner,gool_native_factory *factory,
                            gool_native_dispatch *outer);
/* On cancellation/error abort pending dispatch and release retained call/code
 * ownership. Prior object writes and actor/zone lifetimes remain intact. */
int gool_native_state_close(gool_native_state *owner);
#endif
