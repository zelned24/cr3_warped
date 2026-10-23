#ifndef GOOL_SEND_NATIVE_H
#define GOOL_SEND_NATIVE_H
#include "gool_vm.h"
enum {GOOL_SEND_NATIVE_DONE=0,GOOL_SEND_NATIVE_BUDGET=1,GOOL_SEND_NATIVE_WAIT=2,
      GOOL_SEND_NATIVE_INVALID=-120,GOOL_SEND_NATIVE_RANGE=-121};
#define GOOL_SEND_NATIVE_PROVIDER_PENDING 1
typedef struct {
    void *context;
    int (*read)(void *context,uint32_t address,uint32_t *value);
    int (*read16)(void *context,uint32_t address,uint16_t *value);
    int (*write)(void *context,uint32_t address,uint32_t value);
    int (*object_end)(void *context,uint32_t node,uint32_t *end);
    int (*enter_frame)(void *context,uint32_t node,uint32_t argc,uint32_t flags);
    int (*dispatch)(void *context,uint32_t node,uint32_t mode,gool_signal_receiver *receiver,int32_t *result);
    int (*enter_state)(void *context,uint32_t node,uint32_t state,uint32_t argc,uint32_t arguments,int32_t *result);
} gool_send_native_services;
typedef struct {
    gool_send_native_services services;
    gool_signal_receiver receiver;
    uint32_t sender,target,signal,argc,arguments,phase,selected,index,end,stack_begin;
    int active,status;
    int32_t handler_result,native_result;
} gool_send_native;
/* Original1DB38..1DDF0 delivery, separate from opcode wrapper37EDC. Typed
 * dispatch/state callbacks may be PENDING only while resuming their same job.
 * Preserve actual sender/target storage, arguments, receiver, code/zone leases
 * and nested engine timeline through completion. No implicit acknowledgements,
 * victim deletion, default state or synthesized handler result.
 * Failed on_signal reading an unwritten original ack record rejects explicitly
 * rather than fabricate a value for uninitialized PS1 stack memory.
 */
int gool_send_native_begin(gool_send_native *send,const gool_send_native_services *services,
    uint32_t sender,uint32_t target,int32_t signal,uint32_t argc,uint32_t arguments);
int gool_send_native_step(gool_send_native *send,uint32_t budget);
#endif
