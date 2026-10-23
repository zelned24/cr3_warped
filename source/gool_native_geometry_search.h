#ifndef GOOL_NATIVE_GEOMETRY_SEARCH_H
#define GOOL_NATIVE_GEOMETRY_SEARCH_H
#include "gool_native_factory.h"
#include "gool_native_dispatch.h"
#include "gool_search.h"

typedef struct {
    gool_native_factory *factory;
    gool_native_dispatch *outer;
    gool_native_dispatch dispatch;
    gool_native_actor *candidate_actor;
    gool_send_request previous_send;
    gool_search search;
    gool_domain_lease argument_lease;
    uint64_t tick;
    uint32_t excluded,mask,best,distance,candidate,candidate_distance;
    uint32_t route,phase,root,saved_pc,saved_fp,argument,argument_address;
    int active,status,argument_mapped,candidate_retained,previous_send_pending;
} gool_native_geometry_search;
/* Native36A44 selector13/F00 through19ACC and1E42C. The VM has consumed
 * operand resolution and advanced its cached PC exactly once; outer is stopped
 * on GEOMETRY with an actual pending operand. Native roots3/4 are traversed in
 * original order, siblings captured before descent, script results read from
 * excluded+D8. No guessed compatibility/result or recursive C execution.
 * Stable owner/frames/tick/domain required across all budget continuations. */
int gool_native_geometry_search_begin(gool_native_geometry_search *owner,
    gool_native_factory *factory,gool_native_dispatch *outer,uint32_t argument_address);
int gool_native_geometry_search_step(gool_native_geometry_search *owner,uint32_t budget);
gool_native_dispatch *gool_native_geometry_search_pending(gool_native_geometry_search *owner);
int gool_native_geometry_search_close(gool_native_geometry_search *owner);
#endif
