#ifndef GOOL_NATIVE_ZONE_TRANSITION_H
#define GOOL_NATIVE_ZONE_TRANSITION_H
#include "gool_native_zone_exit.h"
#include "gool_native_activation_path.h"

enum {GOOL_NATIVE_ZONE_TRANSITION_DONE=0,GOOL_NATIVE_ZONE_TRANSITION_PENDING=1,
      GOOL_NATIVE_ZONE_TRANSITION_INVALID=-182,GOOL_NATIVE_ZONE_TRANSITION_LIMIT=-183};
typedef struct {
    uint32_t address,gp,arguments[4],argument_count,stack_arguments[5],stack_count;
} gool_native_zone_transition_call;
typedef struct {
    void *context;
    gool_native_registry *registry;
    gool_zone_retire_services retirement;
    /* Retained original32710 queries used by the internal1AD68 owner.
     * Separate job storage from call_begin: cancellation must not release the
     * database or activation source until registration has finished. */
    gool_native_activation_path_services activation_query;
    uint32_t neighbor_limit,path_limit,activation_capacity;
    /* Actual native callees, identified by native address and actual GP.
     * Begin once, resume the same job with step. The request/result storage
     * stays fixed until mandatory cancel succeeds, including completion/error.
     * Return0 complete,1 pending,negative failure; no synthetic native result.
     * Only argument_count register arguments are semantically supplied; unused
     * array slots are not guesses about clobbered native registers. Caller
     * retains database, gp, global mappings and service ownership. Published
     * resource handles stay retained through their original1440C release;
     * cancel quiesces a call, never prematurely invalidates its returned data.
     */
    int (*call_begin)(void *context,const gool_native_zone_transition_call *call,uint32_t *result);
    int (*call_step)(void *context,uint32_t budget);
    int (*call_cancel)(void *context);
} gool_native_zone_transition_services;
typedef struct {
    gool_native_zone_transition_services services;
    gool_native_registry *registry;
    zdat_resources *bank;
    gool_domain *domain;
    gool_native_zone_exit exit;
    gool_native_activation activation;
    gool_native_activation_path activation_path;
    zone_transition_cursor contains;
    zdat_resource_lease zone_lease,previous_lease,candidate_lease;
    gool_native_zone_transition_call call;
    uint32_t zone,database,position,flags,gp,changes,notify;
    uint32_t previous,previous_header,candidate,index,neighbor_count;
    uint32_t samples,maximum,property,previous_property,integer_position,header_word;
    uint32_t data,header,current_index,difference,list,buffer,direction,path_steps;
    uint32_t scratch[4],player,native_result;
    uint32_t phase,continuation,result;
    int opened,status,zone_retained,previous_retained,candidate_retained;
    int call_owned,call_completed;
} gool_native_zone_transition;

/* Original215E8..21E5C control flow, with real asynchronous native services for
 * called bodies. DONE means this composition returned, not that external
 * resource/camera/activation/creation bodies have been independently verified.
 * native_result is the actual final V0 word, including native void-return paths.
 * position is the raw signed native A2 word (not a vector pointer).
 * gp is the explicit native GP address, retained for the subsequent phase.
 * All global addresses are real mapped engine addresses. No default player,
 * active zone, database, timeline, resource query or retirement is fabricated.
 * Initialize to zero; keep owner/registry/bank/timeline stable through close.
 */
int gool_native_zone_transition_begin(gool_native_zone_transition *owner,
    uint32_t zone,uint32_t database,uint32_t position,uint32_t flags,uint32_t gp,
    const gool_native_zone_transition_services *services);
int gool_native_zone_transition_step(gool_native_zone_transition *owner,uint32_t budget);
/* Quiesce pending calls/exit before releasing candidate, old zone and input
 * zone leases. Failed cleanup preserves remaining ownership for retry. */
int gool_native_zone_transition_close(gool_native_zone_transition *owner);
#endif
