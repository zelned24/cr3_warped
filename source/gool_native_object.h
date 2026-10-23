#ifndef GOOL_NATIVE_OBJECT_H
#define GOOL_NATIVE_OBJECT_H
#include "gool_native_update.h"
#include "gool_native_mode.h"
#include "gool_native_represent.h"
#include "gool_native_model_prepare.h"
#include "gool_world.h"

enum {GOOL_NATIVE_OBJECT_DONE=0,GOOL_NATIVE_OBJECT_PENDING=1,
      GOOL_NATIVE_OBJECT_INVALID=-126,GOOL_NATIVE_OBJECT_TIMELINE=-127};
enum {GOOL_NATIVE_OBJECT_PROTECTED=1,GOOL_NATIVE_OBJECT_TRANSFORM=2,
      GOOL_NATIVE_OBJECT_PRESENT=4};
typedef struct {
    void *context;
    /* Real15798 protected-object service, called before current publication. */
    int (*protected_update)(void *context,uint32_t node);
    /* Real1E78C movement/collision path, also used for model types outside
     * the concrete no-motion SVTX path. old_vectors is the native six-word
     * snapshot of object60..74, valid only when has_old_vectors is set.
     * Representation is the original two-word1B630/1B7A0 exchange. Bit0/1 of
     * written identify initialized words; never consume an unwritten word. */
    int (*transform)(void *context,uint32_t node,const uint32_t old_vectors[6],
                     int has_old_vectors,uint32_t representation[2],uint32_t *written);
    /* Actual port1B7A0 presentation, after native presentation admission. */
    int (*present)(void *context,uint32_t node,const uint32_t representation[2],uint32_t written);
    /* Stop/release the identified unfinished service, including its borrowed
     * snapshots/output storage. Return0 only when quiescent,1 while pending.
     * Called on explicit cleanup after a pending callback or provider failure. */
    int (*cancel)(void *context,uint32_t node,uint32_t service);
} gool_native_object_services;
typedef struct {
    gool_native_factory *factory;
    gool_native_actor *actor;
    gool_native_update update;
    gool_native_represent represent;
    gool_native_model_prepare model;
    gool_native_object_services services;
    uint64_t tick;
    uint32_t requested,effective,phase,external_stage;
    uint32_t old_vectors[6],representation[2],written;
    int bound,active,status,actor_retained,has_old_vectors,admitted,presented;
    int32_t native_result;
} gool_native_object;
/* Original1D674..1DB34 composition, stable caller-owned storage initially{0}.
 * All engine services and their cancellation are mandatory. Each returns0
 * only after completing its invocation,1 while its own retained cursor needs
 * polling, or a negative provider error. The same invocation is polled until
 * complete; completed callbacks are never replayed on budget continuation.
 * No default physics, rendering, activation, controller poll or clock exists.
 * Factory, code/zone owners, frames and engine timeline remain stable.
 */
int gool_native_object_bind(gool_native_object *owner,gool_native_factory *factory,
    gool_native_route_frame *frames,uint32_t capacity,uint32_t arguments_base,
    const gool_native_object_services *services);
/* Requires an exclusive stopped factory actor. Retains it across admission,
 * transfers that reference into SCRIPT, and reacquires it before post-script
 * services. requested_update is the actual native caller argument. */
int gool_native_object_begin(gool_native_object *owner,gool_native_actor *actor,uint32_t requested_update);
int gool_native_object_step(gool_native_object *owner,uint32_t budget);
/* Cancel/quiesce the pending external service, unwind SCRIPT, then release the
 * actor reference. BUSY/error retains remaining owners for cleanup retry.
 * No rollback, implicit notification, rendering or object deletion. */
int gool_native_object_close(gool_native_object *owner);
#endif
