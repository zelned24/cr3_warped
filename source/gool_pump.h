#ifndef GOOL_PUMP_H
#define GOOL_PUMP_H
#include "gool_delivery.h"

#define GOOL_PUMP_DEPTH 16u /* port resource budget, not a PS1 limit */
typedef struct {
    gool_instance **instances,*root,*pending;
    gool_object **objects;
    gool_memory *memory;
    gool_delivery jobs[GOOL_PUMP_DEPTH];
    uint64_t tick;
    uint32_t count,current_index,depth;
    int active,status;
} gool_pump;
/* Zero-init and keep stable: VMs borrow receiver/span storage in jobs[].
 * instances and object tables use the SAME slot indices and outlive the pump.
 * Null object slot means absent destination. A live object lacking an instance
 * provider is an explicit error. Every involved VM must share memory.
 * current_index is the native current UPDATE object, or UINT32_MAX outside
 * UPDATE. Caller owns scheduler ordering/activation/physics; this pump does
 * not invent that ordering or run the sender's main after completing SEND.
 * Begin only reserves the request; budget 0 performs no delivery side effects. */
int gool_pump_begin(gool_pump *pump,gool_instance **instances,uint32_t count,
                    gool_instance *sender,gool_memory *memory,uint64_t tick,uint32_t current_index);
/* One work unit starts a delivery or advances one job by at most one VM
 * instruction. Route selection/application and completion also cost units.
 * Nested SENDs push stable jobs; children complete before parent continuation.
 * Do not update/recycle/rebind objects, registry, memory or ticks while active.
 * RUNNING / SEND_REPLACED are terminal root-wrapper results; BUDGET is a
 * resumable limit, not a fault. Unsupported native modes remain explicit. */
int gool_pump_step(gool_pump *pump,uint32_t work_budget);
/* Failure/cancel unwinds in reverse, restoring borrowed contexts and releasing
 * spans. Script effects/abandoned frames are NOT rolled back and SEND remains
 * unfinished: owner must reset/discard affected instances before retrying.
 * Negative status except BUDGET; sticky until a new valid begin. */
int gool_pump_abort(gool_pump *pump,int status);
#endif
