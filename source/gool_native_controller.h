#ifndef GOOL_NATIVE_CONTROLLER_H
#define GOOL_NATIVE_CONTROLLER_H
#include "gool_native_router.h"
enum {GOOL_NATIVE_CONTROLLER_DONE=0,GOOL_NATIVE_CONTROLLER_PENDING=1,
      GOOL_NATIVE_CONTROLLER_INVALID=-210,GOOL_NATIVE_CONTROLLER_LIMIT=-211,
      GOOL_NATIVE_CONTROLLER_TIMELINE=-212};
typedef struct {
    void *context;
    /* Synchronous actual port pad services identified by original address:
     * bootstrap4A748/48E68; polling48F00/48FC0/4926C/49234/492B4.
     * arguments contains only the supplied native arguments. The provider
     * owns actual input sampling/output and writes any mapped raw pad bytes.
     * Return0 with the actual native result or negative failure. No successful
     * connection/analog negotiation/rumble result is supplied by this owner. */
    int (*pad)(void *context,uint32_t function,const uint32_t arguments[3],uint32_t argc,uint32_t *native_result);
} gool_native_controller_services;
typedef struct {
    gool_native_factory *factory;
    gool_domain *domain;
    gool_native_controller_services services;
    gool_native_route_frame *frames;
    gool_native_state state;
    gool_native_delivery delivery;
    gool_native_router router;
    gool_domain_lease arguments_lease;
    uint64_t tick;
    uint32_t capacity,arguments_base,instruction_budget,max_controllers,gp;
    uint32_t index,record,port,phase,native_pad_result,target,argument;
    int32_t s2,s3;
    int bound,active,status,signal_owned,arguments_mapped;
} gool_native_controller;
/* Original156E0. Does not clear raw input bytes or manufacture pad devices.
 * Provider calls occur once in native order; an error preserves prior writes
 * and must not be retried as a fresh initialization without owner cleanup. */
int gool_native_controller_initialize(gool_domain *domain,int32_t count,uint32_t maximum,
    const gool_native_controller_services *services);
/* Factory/domain/timeline and raw controller/global mappings remain stable.
 * gp is the actual native GP for demo cursor accesses, not assumed equal to
 * the fixed engine base80068E98. Argument storage reserves256 bytes followed
 * by capacity disjoint router spans, separate from every other live owner. */
int gool_native_controller_bind(gool_native_controller *owner,gool_native_factory *factory,
    uint32_t gp,uint32_t maximum,const gool_native_controller_services *services,
    gool_native_route_frame *frames,uint32_t capacity,uint32_t arguments_base,uint32_t instruction_budget);
/* Original15798 leaves S2/S3 uninitialized on disconnected/type12 paths.
 * Supply the actual incoming native callee-saved registers; they carry between
 * controllers. Connected digital/analog paths assign them before publication.
 * No implicit zero stick values are invented for those native undefined paths. */
int gool_native_controller_begin(gool_native_controller *owner,int32_t incoming_s2,int32_t incoming_s3);
int gool_native_controller_step(gool_native_controller *owner,uint32_t budget);
int gool_native_controller_close(gool_native_controller *owner);
#endif
