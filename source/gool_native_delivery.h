#ifndef GOOL_NATIVE_DELIVERY_H
#define GOOL_NATIVE_DELIVERY_H
#include "gool_send_native.h"
#include "gool_native_state.h"
typedef struct {
    gool_native_factory *factory;gool_native_state *state;
    gool_native_dispatch *outer;gool_native_dispatch dispatch;
    gool_send_native send;
    gool_native_actor *target;
    gool_native_actor *native_sender;
    gool_signal_receiver *previous_receiver;
    gool_send_request request,previous_send;
    gool_domain_lease arguments_lease;
    uint64_t resource_generation;
    uint64_t native_tick;
    int32_t arguments[7];
    uint32_t arguments_address,instruction_budget,phase;
    int bound,active,status,arguments_mapped,resource_retained,sender_retained,target_retained;
    int target_context,previous_send_pending,dispatch_started,finish_outer,wrapper_finished;
    int native_entry;
    int32_t native_result;
} gool_native_delivery;
/* One stable owner from a fixed nested-call pool. state is an independent
 * bound native-state owner for this delivery, using the same factory/registry.
 * argument_address reserves28 nonoverlapping native bytes for copied arguments.
 * No allocation, fabricated ack, implicit target retirement or lost page lease.
 */
int gool_native_delivery_bind(gool_native_delivery *delivery,gool_native_factory *factory,
    gool_native_state *state,uint32_t argument_address,uint32_t instruction_budget);
int gool_native_delivery_begin(gool_native_delivery *delivery,gool_native_dispatch *outer);
/* Direct original1DB38 engine entry, including sender0 retirement signals.
 * Does not fabricate an opcode37EDC caller or execute its wrapper completion.
 * Arguments are the actual mapped native span; caller pins it and the object
 * graph through completion/close. No eager argument copy changes aliasing.
 * Nonzero sender must be an actual registered stopped actor. Native signal
 * dispatch/state handoffs are exposed through pending() and must be routed
 * before continuing this same owner. DONE exposes actual native_result.
 * Up to15 native arguments are supported independently of opcode37EDC's
 * seven-argument copied wrapper. The actual engine tick stays fixed.
 */
int gool_native_delivery_begin_native(gool_native_delivery *delivery,uint32_t sender,
    uint32_t target,int32_t signal,uint32_t argc,uint32_t arguments);
/* BUDGET/WAIT>0 retains exact continuation. Successful0 completes the native
 * service, actual opcode wrapper and outer dispatch resume/exit. Error retains
 * cleanup ownership; close before reusing this delivery owner. */
int gool_native_delivery_step(gool_native_delivery *delivery,uint32_t work_budget);
/* Stopped nested SEND/STATE work, either handler or native state entry.
 * Route it through another retained call owner, then resume that exact cursor. */
gool_native_dispatch *gool_native_delivery_pending(gool_native_delivery *delivery);
/* Cancels child execution and restores borrowed context, releases mapped args
 * and retained actors/code. Does not acknowledge/finish an undelivered SEND,
 * undo target writes, delete actors or abort the caller-owned outer cursor. */
int gool_native_delivery_close(gool_native_delivery *delivery);
#endif
