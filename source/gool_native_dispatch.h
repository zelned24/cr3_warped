#ifndef GOOL_NATIVE_DISPATCH_H
#define GOOL_NATIVE_DISPATCH_H
#include "gool_native_actor.h"
enum {GOOL_NATIVE_DISPATCH_DONE=0,GOOL_NATIVE_DISPATCH_BUDGET=1,
      GOOL_NATIVE_DISPATCH_SEND=2,GOOL_NATIVE_DISPATCH_STATE=3,GOOL_NATIVE_DISPATCH_GEOMETRY=4,
      GOOL_NATIVE_DISPATCH_SPAWN=5,GOOL_NATIVE_DISPATCH_RESOURCE=6,GOOL_NATIVE_DISPATCH_INVALID=-108};
typedef struct {
    gool_native_actor *actor;
    uint32_t previous_mode,event_mode;
    uint32_t previous_geometry_instruction,previous_geometry_operand;
    int previous_geometry_pending;
    int previous_delay,active,status,error;
    int32_t native_result;
} gool_native_dispatch;
/* Native38E28 over a retained actor: no allocation or inferred frame clock.
 * Caller has completed the original UPDATE/token gate before main mode4.
 * Mode8 requires a real borrowed receiver. Actor must be stopped and healthy;
 * raw PC/FP/SP and constant owners are imported at the dispatch boundary.
 * One owner per active call, including nested delivery; keep cursor stable. */
int gool_native_dispatch_begin(gool_native_dispatch *dispatch,gool_native_actor *actor,uint32_t mode);
/* Budget continuation preserves current mode and native interpreter cache.
 * SEND/STATE/GEOMETRY stop the actor and retain this dispatch. Engine must perform the
 * actual request, including nested signal/state work, then explicitly resume.
 * DONE exposes original native_result, including root deletion -25, failed
 * receive -26 and ordinary frame/handler completion -255. A negative status is
 * a port/provider failure, never a fabricated native engine return word. */
int gool_native_dispatch_step(gool_native_dispatch *dispatch,uint32_t instructions);
/* After the real service completes, choose the native wrapper's actual action.
 * complete0 reloads flow and continues; complete1 returns supplied native_result.
 * Pending SEND/GEOMETRY must have been consumed by their real owner services.
 * State owner decides continuation using event_mode and actual state-entry result;
 * this dispatcher does not guess state success or discard an implicit token. */
int gool_native_dispatch_resume(gool_native_dispatch *dispatch,int complete,int32_t native_result);
/* Release this call's mode/delay ownership after a real provider failure. Does
 * not undo object writes, delete actors, clear VM faults or release resources. */
int gool_native_dispatch_abort(gool_native_dispatch *dispatch,int error);
#endif
