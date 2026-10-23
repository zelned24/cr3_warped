#ifndef GOOL_DELIVERY_H
#define GOOL_DELIVERY_H
#include "gool_instance.h"
#include "gool_signal.h"
#include "gool_send.h"

enum {GOOL_DELIVERY_ROUTE=9};
typedef struct {
    gool_instance *sender,*target;
    gool_send_request request,saved_target_send;
    gool_signal_receiver receiver,*saved_receiver;
    gool_signal_route route;
    uint32_t saved_mode,saved_depth,phase;
    uint32_t current_index;
    uint64_t generation;
    gool_memory *span_memory;
    int32_t span_pointer;
    int saved_delay,saved_pending,context_bound,active,status,replaced;
} gool_delivery;
/* Zero-init and keep stable until complete: target.vm borrows &receiver.
 * Sender/target/context tables/archives must outlive the job; do not recycle
 * entity slots or advance the world tick while a job is paused.
 * target is the engine-resolved pointer, checked against request target and
 * common object table. NULL means no live destination. argument_pointer is
 * an explicit engine handle to the incoming argument span, not a receiver ID.
 * Its memory ABI must be provided before scripts dereference that handle. */
int gool_delivery_begin(gool_delivery *job,gool_instance *sender,gool_instance *target,
                        int32_t argument_pointer,uint64_t tick);
/* Register this stable job's request.arguments as its incoming span. Sender
 * and live target VMs must already share memory. Release is automatic on
 * complete/error/abort; retained across budgets, ROUTE and owner-managed SEND
 * children. Do not release externally or reuse the job while its span exists.
 * Other referenced objects/regions still require their own providers. */
int gool_delivery_begin_mapped(gool_delivery *job,gool_instance *sender,gool_instance *target,
                               gool_memory *memory,uint64_t tick);
/* Executes ON_SIGNAL only, at most budget VM instructions. SEND from handler
 * yields for an owner-managed nested job; do not step this parent until that
 * child request is finished. Invalid/unsupported paths fail explicitly.
 * No route: finishes sender wrapper. ROUTE: restores handler context and
 * returns at native state/script invocation boundary, with SIGNAL_VALUE/
 * collision flags set. Owner then calls gool_delivery_apply with the world's
 * current object index. Selection alone does not execute the route. */
int gool_delivery_step(gool_delivery *job,uint32_t budget);
/* Apply a selected ROUTE using the job's request.arguments span, not the
 * RECEIVE output arguments. current_index is the world's current object,
 * UINT32_MAX if between object updates. State entry runs its pre_run now
 * only for that current object; other objects run it on their next update.
 * Pending old RESUME executes in mode 0x13 before that check, even for a
 * foreign destination; its normal return supplies the native zero token.
 * Explicit base-code routes run in mode 3 and preserve the suspended main.
 * At most budget VM instructions; zero budget causes no application.
 * SEND yields to an owner-managed child. Nested root replacement while a
 * context is borrowed and application into an outer receiver are unsupported
 * explicitly. step() can resume an application with its saved current index.
 * This component is not the world scheduler or the incoming span memory ABI. */
int gool_delivery_apply(gool_delivery *job,uint32_t current_index,uint32_t budget);
/* Restore borrowed VM context on cancellation/failure; negative status only.
 * Script side effects are retained and sender request remains unfinished. */
int gool_delivery_abort(gool_delivery *job,int status);
#endif
