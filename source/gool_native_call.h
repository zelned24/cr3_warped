#ifndef GOOL_NATIVE_CALL_H
#define GOOL_NATIVE_CALL_H
#include "gool_native_router.h"
enum {GOOL_NATIVE_CALL_DONE=0,GOOL_NATIVE_CALL_PENDING=1,GOOL_NATIVE_CALL_INVALID=-132};
typedef struct {
    gool_native_factory *factory;
    gool_native_actor *actor;
    gool_native_dispatch dispatch;
    gool_native_router router;
    uint32_t node,script,argc,arguments,index,phase;
    uint64_t tick;
    int bound,active,status,actor_retained;
    int32_t native_result;
} gool_native_call;
/* Original1E42C:copy native arguments in order, form argc/FFFF frame, choose
 * BASE code(script&3FFF), dispatch mode3. Caller-owned cursor and route frames
 * stay stable. No native call result, token or activation is fabricated.
 * Caller retains argument mappings/source owners through completion/cleanup.
 * This is a real script invocation, distinct from signal admission/delivery.
 */
int gool_native_call_bind(gool_native_call *owner,gool_native_factory *factory,
    gool_native_route_frame *frames,uint32_t capacity,uint32_t router_arguments_base);
/* Actor may already have stopped outer call owners, but may not be executing,
 * enabled, faulted or carrying an unconsumed SEND. Node is a real pool object.
 * Begin retains the actor; step performs each setup effect once. This checked
 * port entry accepts0..15 arguments; negative/native larger counts reject
 * before setup writes rather than inventing or overrunning argument storage.
 * A completed owner may run again; failed owners require explicit close. */
int gool_native_call_begin(gool_native_call *owner,uint32_t node,uint32_t script,
                           uint32_t argc,uint32_t arguments);
int gool_native_call_step(gool_native_call *owner,uint32_t budget);
/* Deepest-first router cancellation, root-dispatch abort, actor release.
 * Failed cleanup preserves remaining owners; object writes are not undone. */
int gool_native_call_close(gool_native_call *owner);
/* Original1E42C setup only, over an already retained stopped actor. Caller
 * supplies actual arguments, then owns the mode3 dispatch/nested work. Errors
 * preserve partial argument/frame writes; never replay a failed setup. */
int gool_native_call_prepare(gool_native_actor *actor,uint32_t script,
                              uint32_t argc,uint32_t arguments);
#endif
