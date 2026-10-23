#ifndef GOOL_NATIVE_MODEL_PREPARE_H
#define GOOL_NATIVE_MODEL_PREPARE_H
#include "gool_native_actor.h"
enum {GOOL_NATIVE_MODEL_DONE=0,GOOL_NATIVE_MODEL_PENDING=1,GOOL_NATIVE_MODEL_INVALID=-133};
typedef struct {
    gool_native_actor *actor;
    model_resources *bank;
    model_resource_lease leases[2];
    uint32_t reference[2],descriptor,frame_word,scale,phase;
    uint32_t descriptor_flags,scale_words[3];
    uint16_t frame_limit;
    uint64_t tick;
    int active,status,actor_retained,retained[2],requested[2];
} gool_native_model_prepare;
/* Stable caller-owned cursor initially{0}. Capture actual type1 E8/EC and
 * descriptor SVTX/TGEO references, retain actor, request missing original EIDs
 * asynchronously, then run native READY preparation exactly once. No heap,
 * synchronous SD I/O, chosen animation or guessed model entry exists here.
 * Actor/model-bank/resource session/timeline stay stable through suspension.
 * Shared requests remain bank-owned after cancellation; this job releases only
 * its acquired model leases. Bank teardown cancels requests when no users remain.
 */
int gool_native_model_prepare_begin(gool_native_model_prepare *owner,gool_native_actor *actor,uint32_t scale);
int gool_native_model_prepare_step(gool_native_model_prepare *owner,uint32_t budget);
/* Release temporary model/actor ownership after success or cancellation.
 * Actor's published frame/TGEO leases remain until replacement/retirement.
 * Failure preserves uncompleted cleanup for retry. No preparation rollback. */
int gool_native_model_prepare_close(gool_native_model_prepare *owner);
#endif
