#ifndef GOOL_NATIVE_UPDATE_H
#define GOOL_NATIVE_UPDATE_H
#include "gool_native_router.h"

enum {GOOL_NATIVE_UPDATE_DONE=0,GOOL_NATIVE_UPDATE_BUDGET=1,
      GOOL_NATIVE_UPDATE_INVALID=-124,GOOL_NATIVE_UPDATE_TIMELINE=-125};

typedef struct {
    gool_native_factory *factory;
    gool_native_actor *actor;
    gool_native_route_frame *frames;
    gool_native_dispatch dispatch;
    gool_native_router router;
    uint64_t tick;
    uint32_t capacity,arguments_base,phase;
    int bound,active,status,actor_retained;
    int pre_ran,main_ran,waiting,has_result,early_return;
    int32_t native_result;
} gool_native_update;

/* Native1D82C..1D8D8 SCRIPT stage only: no activation, world admission,
 * LAST_UPDATE stamping, physics or presentation. Stable caller-owned owner and
 * frames initially{0}; factory/actor/timeline outlive every continuation.
 * Argument virtual addresses must be unique among simultaneous router owners.
 * The router checks their alignment/range and factory-request separation.
 */
int gool_native_update_bind(gool_native_update *owner,gool_native_factory *factory,
    gool_native_route_frame *frames,uint32_t capacity,uint32_t arguments_base);
/* Requires an already attached, healthy, stopped factory actor with no existing
 * call references: a suspended nested owner must not admit another root.
 * Captures the actual engine tick; domain80068EEC must equal its low32 bits throughout.
 * Begin retains the actor but does not change frames or execute bytecode. */
int gool_native_update_begin(gool_native_update *owner,gool_native_actor *actor);
/* Same admission, but requires exactly one caller-owned call reference and
 * transfers it to UPDATE without incrementing the count. Only a successful
 * BUDGET return transfers ownership; every failure leaves the caller's reference
 * and owner unchanged. DONE/close releases the transferred reference exactly
 * like begin's own reference; a continuing caller must explicitly reacquire. */
int gool_native_update_take(gool_native_update *owner,gool_native_actor *actor);
/* Work budget bounds setup/gate transitions and nested router work. DONE leaves
 * actor stopped. native_result is the last actual dispatch result, valid only
 * when has_result; waiting with no PRE_RUN has no fabricated native result.
 * early_return means the native SCRIPT stage returns result>=-254 immediately.
 * Otherwise the caller still owns the subsequent physics/presentation path.
 * Errors are sticky and keep all unfinished ownership until explicit close. */
int gool_native_update_step(gool_native_update *owner,uint32_t budget);
/* Unwind router before root dispatch, then release the retained actor. BUSY or
 * cleanup failure preserves remaining ownership for retry. No rollback,
 * implicit success, notification, token restoration or object deletion. */
int gool_native_update_close(gool_native_update *owner);
#endif
