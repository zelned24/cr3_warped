#ifndef GOOL_NATIVE_FACTORY_H
#define GOOL_NATIVE_FACTORY_H
#include "gool_native_actor.h"
#include "gool_native_pool.h"
#include "gool_create.h"
typedef struct gool_native_factory gool_native_factory;
#define GOOL_NATIVE_REQUEST_COUNT (GOOL_NATIVE_POOL_COUNT+1u)
typedef struct {
    int32_t *globals;uint32_t global_count;
    uint32_t *random_state;
    const int32_t *phase_source,*frame_factor;
    const uint64_t *tick;
    int send_enabled;
    const struct gool_native_geometry_services *geometry;
    const struct audio_native_services *sound;
    const gool_create_services *creation;
    /* Stopped opcode40 owner:1 pending,0 done,negative failure; cancel releases I/O. */
    int (*resource)(void *context,uint32_t node,uint32_t instruction,int cancel);
    void *resource_context;
} gool_native_engine_data;
typedef struct gool_native_request {
    struct gool_native_request *previous;
    gool_domain_lease request_lease;
    uint32_t node,state,argc,arguments_address,request_mode;
    int32_t arguments[15];
    uint64_t generation;
    int mapped;
} gool_native_request;

typedef struct {
    gool_native_actor actor;gool_vm vm;
    gool_native_factory *owner;
    gool_native_request *request;
    uint32_t node;
} gool_native_factory_slot;
struct gool_native_factory {
    gool_native_registry *registry;gool_native_pool *pool;
    gool_native_factory_slot *slots;uint32_t count,request_base;
    gool_native_engine_data engine;
    gool_native_request requests[GOOL_NATIVE_REQUEST_COUNT];
    uint64_t request_generation;
    int open;
};
/* Caller-owned fixed slots initially{0}, one per actual pool object including reserved
 * player. Real shared globals/RNG/counter/factors are required, borrowed and
 * stable. No default seed, synthetic tick or per-object heap allocation.
 * Raw native frames are adopted; no guessed initial state is fabricated.
 * Dispatch samples the borrowed engine tick for each invocation; the caller
 * keeps it fixed while any nested call is pending, including budget yields.
 * request_base reserves GOOL_NATIVE_REQUEST_COUNT*256 virtual bytes. A shared
 * fixed request pool retains independent argument storage and LIFO ownership
 * for reentrant calls on the same actor; outer requests never move or remap.
 * Factory and slots outlive registry and every nested dispatch/state owner.
 */
int gool_native_factory_open(gool_native_factory *factory,gool_native_registry *registry,
    gool_native_pool *pool,gool_native_factory_slot *slots,uint32_t count,
    const gool_native_engine_data *engine,uint32_t request_base);
/* Direct gool_native_state_engine.actor callback; returns a stopped actor. */
int gool_native_factory_actor(void *context,uint32_t node,gool_native_actor **actor);
/* Native37BEC wrapper already popped/copied args and checked compatibility.
 * Pending request storage stays mapped/retained until complete. Call real state
 * owner with state/argc/arguments_address; never acknowledge without running it.
 * complete mirrors original mode-bit2/native-result thresholds and token pop.
 * Returns1 if outer dispatch must finish with native_result,0 to continue. */
gool_native_request *gool_native_factory_request(gool_native_factory *factory,uint32_t node);
int gool_native_factory_complete(gool_native_factory *factory,uint32_t node,int32_t native_result,int *finish);
/* Explicit cancellation only: discard retained request ownership after nested
 * dispatch has stopped. Does not report success, pop a token, undo state writes
 * or resume the caller. Needed when unwinding a failed/cancelled call tree. */
int gool_native_factory_cancel_request(gool_native_factory *factory,uint32_t node);
/* Only after real notification/child/audio/resource retirement. Refuses pending
 * state requests, active dispatch owners, enabled actors or externally borrowed
 * stack storage. Writes final stack bytes back for actual native slot reuse. */
int gool_native_factory_retired(gool_native_factory *factory,uint32_t node);
int gool_native_factory_close(gool_native_factory *factory);
#endif
