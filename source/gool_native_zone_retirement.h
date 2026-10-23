#ifndef GOOL_NATIVE_ZONE_RETIREMENT_H
#define GOOL_NATIVE_ZONE_RETIREMENT_H
#include "gool_zone_retire.h"
#include "gool_destroy.h"
#include "gool_native_router.h"
enum {GOOL_NATIVE_ZONE_RETIREMENT_TIMELINE=-194};

typedef struct {
    void *context;
    /* Real voice ownership retirement. Return0 quiescent,1 pending,negative
     * failure. cancel must quiesce the same job without reporting success for
     * voices that were never actually stopped. Both callbacks are mandatory. */
    int (*stop_audio)(void *context,uint32_t node);
    int (*cancel_audio)(void *context,uint32_t node);
} gool_native_retirement_audio;

typedef struct {
    gool_native_factory *factory;
    gool_native_route_frame *frames;
    gool_native_state state;
    gool_native_delivery delivery;
    gool_native_router router;
    gool_destroy destroy;
    gool_destroy_services destruction;
    gool_native_retirement_audio audio;
    gool_zone_retire_services services;
    uint64_t tick;
    uint32_t capacity,arguments_base,instruction_budget;
    uint32_t node,notify,signal_node,signal_word,audio_node;
    int bound,operation,signal_owned,audio_owned,status;
} gool_native_zone_retirement;

/* Concrete1AA98/2157C signal/deletion providers. Actual sender0 1DB38 delivery
 * runs through dedicated retained state/router owners and resolves nested
 * SEND/STATE jobs before returning. Actual1A844 performs postorder native
 * destruction, signals and real voice retirement; type0 publication detaches
 * adopted factory actors before slot reuse. Completed1A844 returns native
 * V0=-255 as evidenced by original1A99C, independently of port status0.
 * Storage initially zero; fixed router frames and all graph/code/zone/global
 * owners stay stable through service completion or successful cancellation.
 * arguments_base reserves256 bytes for direct delivery followed by capacity
 * disjoint router spans. It must not alias other live routers/factory spans.
 * destruction supplies actual protected/detached/occupancy/free-root metadata;
 * its links/notify/stop_audio callbacks are replaced by these concrete owners.
 * No engine bootstrap, audio fallback, guessed timing or fake ack is supplied.
 */
int gool_native_zone_retirement_bind(gool_native_zone_retirement *owner,
    gool_native_factory *factory,gool_native_route_frame *frames,uint32_t capacity,
    uint32_t arguments_base,uint32_t instruction_budget,uint32_t max_nodes,
    const gool_destroy_services *destruction,const gool_native_retirement_audio *audio);
int gool_native_zone_retirement_close(gool_native_zone_retirement *owner);
#endif
