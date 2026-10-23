#ifndef GOOL_STATE_NATIVE_H
#define GOOL_STATE_NATIVE_H
#include <stdint.h>
enum {GOOL_STATE_NATIVE_DONE=0,GOOL_STATE_NATIVE_BUDGET=1,GOOL_STATE_NATIVE_WAIT=2,
      GOOL_STATE_NATIVE_INVALID=-106,GOOL_STATE_NATIVE_RANGE=-107,GOOL_STATE_NATIVE_ASYNC=-109};
#define GOOL_STATE_NATIVE_PROVIDER_PENDING 1
typedef struct {
    void *context;
    int (*read)(void *context,uint32_t address,uint32_t *value);
    int (*read16)(void *context,uint32_t address,uint16_t *value);
    int (*write)(void *context,uint32_t address,uint32_t value);
    int (*load)(void *context,uint32_t class_word,uint32_t *native_result);
    /* Exclusive end of this exact object slot, not a multi-object pool span.
     * Required before any header/argument/token writes can touch storage. */
    int (*object_end)(void *context,uint32_t node,uint32_t *end);
    /* Actual1D588 must synchronize frame/stack state if a VM is attached. */
    int (*enter_frame)(void *context,uint32_t node,uint32_t argc,uint32_t flags);
    /* Actual38E28(node,mode,0), including nested state changes.
     * Return PROVIDER_PENDING while resuming the same owned dispatch job;
     * do not restart its completed instructions or publish a result yet.
     * Callback status is separate from the signed original engine result. */
    int (*dispatch)(void *context,uint32_t node,uint32_t mode,int32_t *native_result);
} gool_state_native_services;
typedef struct {
    gool_state_native_services services;
    uint32_t node,state,argc,arguments,argument_index;
    uint32_t object_end,stack_begin,resume,record,class_word,phase;
    int active,status;
    int32_t native_result;
} gool_state_native;
/* Original1D2D4..1D584. Stable owner/arguments/providers persist until DONE or
 * error; caller owns all object/resource leases. Only dispatch may be pending.
 * Cursor retains its exact phase across budgets and pending nested execution,
 * so original frame pushes, arguments and global writes occur once in order.
 * Errors retain prior effects; never retry as a fresh state change. */
int gool_state_native_begin(gool_state_native *state,const gool_state_native_services *services,
    uint32_t node,uint32_t index,int32_t argc,uint32_t arguments);
int gool_state_native_step(gool_state_native *state,uint32_t work_budget);
/* Synchronous convenience for providers guaranteed not to return PENDING.
 * An asynchronous provider requires begin/step with caller-owned cursor;
 * this wrapper reports ASYNC rather than discard work and claim success.
 * Error preserves native_result output; previous side effects remain. */
int gool_state_native_enter(const gool_state_native_services *services,
    uint32_t node,uint32_t state,int32_t argc,uint32_t arguments,int32_t *native_result);
#endif
