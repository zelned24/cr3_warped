#ifndef GOOL_NATIVE_GEOMETRY_H
#define GOOL_NATIVE_GEOMETRY_H
#include "gool_native_actor.h"
#include "gool_tree.h"

enum {GOOL_NATIVE_GEOMETRY_INVALID=-130,GOOL_NATIVE_GEOMETRY_UNSUPPORTED=-131};
typedef struct gool_native_geometry_services {
    void *context;
    /* Actual35410 address resolver, including rotating literal scratch and
     * constant/frame/indirect ownership. Direct E00..FFF operands are resolved
     * by apply itself; no fast38B40 scratch-address substitution is assumed. */
    int (*address)(void *context,uint32_t node,uint32_t operand,uint32_t *address);
    /* Remaining whole selector, AFTER operand resolution. Must complete the
     * actual native service, including its stack/results; positive/pending is
     * rejected because this synchronous helper owns no resumable engine job.
     * NULL reports UNSUPPORTED, never a fabricated geometry result. */
    int (*remaining)(void *context,gool_native_registry *registry,uint32_t node,
                      uint32_t instruction,uint32_t operand_address);
    uint32_t hierarchy_scan_limit; /* explicit bound for concrete reparent */
    uint32_t zone_neighbor_limit; /* bound for original28D8C neighbor search */
    /* Actual17CA8 peripheral overlay ABI. entry is an original routine
     * identity, never a host-callable address; arguments contains only native
     * initialized A0/A1 words. Synchronous completion is required. */
    int (*peripheral)(void *context,uint32_t entry,const uint32_t *arguments,
                      uint32_t argument_count,uint32_t *result);
    /* Actual native GP+90 word address for sub15's retained device owner.
     * Explicit because the overlay/global-pointer owner is not inferred. */
    uint32_t peripheral_owner_address;
} gool_native_geometry_services;

/* Original35D08 multiplexer, partial concrete coverage documented below. Native
 * wrapper39378 spills cached SP/FP before calling. Caller must adopt raw BC
 * into its cached SP afterwards, preserve cached PC/FP, and reload constants
 * through object14/10. This helper NEVER reads live-registry BC: executing VM
 * adapters intentionally expose cached SP, which is stale after the first raw
 * helper adjustment. Stack payload uses the actual retained domain overlay.
 * Caller retains object/domain/engine services and excludes concurrent access.
 * Errors preserve completed native writes/pops; do not replay the instruction.
 */
int gool_native_geometry_apply(gool_native_registry *registry,uint32_t node,
    uint32_t instruction,const gool_native_geometry_services *services);
int gool_native_geometry_operand(gool_native_registry *registry,uint32_t node,
    uint32_t instruction,const gool_native_geometry_services *services,uint32_t *address);
/* Entry after the caller already executed native operand-address resolution;
 * does not resolve/pop that operand again. Same wrapper/cache obligations. */
int gool_native_geometry_resolved(gool_native_registry *registry,uint32_t node,
    uint32_t instruction,uint32_t operand_address,const gool_native_geometry_services *services);
/* Concrete selectors:1/6 native vector distance modes;2 native bearing;
 * 3/4 member arrays;5 vector normalization;7 serial search over
 * actual roots3/4 using19AAC predicate;8 occupancy bit2;9 zone lookup;
 * 10 sub0/1/2/3/6/7/8/9;11 occupancy queries;
 * 12 sub2 hierarchy,4 zone,8 pitch,9 engine word,12/13 global word,
 * 14..17 constant owners,19 native no-op,20 serial query.
 * Selector13 nearest search includes native category masks, strict distance
 * improvement, signal255/default routes and state compatibility. The actor
 * wrapper routes F00 to gool_native_geometry_search's resumable1E42C owner;
 * direct synchronous callers still require remaining() for that service.
 * Selector14 includes native invalid-link result0 and concrete non-SVTX
 * fallback bounds/cache/containment and both SVTX hitbox0 kinds with the actual
 * selected frame/matrix and original protected-player padding.
 * Invalid selectors and documented original no-op subselectors return normally
 * after operand effects.
 * Selector15 includes17CA8's no-op return1 commands and sub19/20 packed
 * nine-byte record transfers at8006B0E8, including three-word stack results,
 * byte store order and D4 return. Peripheral sub2/3/5/10/12..18 preserve
 * native overlay call identity/arguments, sub15 owner gate and forced return
 * words through peripheral(); absent that ABI, remaining() is retained.
 * Actual device implementations remain provider-owned, as does synchronous13signalF00,
 * 10sub4/5 and other12 selectors.
 * No float math, dummy engine call or guessed result.
 */
#endif
