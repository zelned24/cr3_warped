#ifndef ZONE_TRANSITION_H
#define ZONE_TRANSITION_H
#include <stdint.h>
enum {ZONE_TRANSITION_DONE=0,ZONE_TRANSITION_BUDGET=1,
      ZONE_TRANSITION_INVALID=-70,ZONE_TRANSITION_LIMIT=-71};
#define ZONE_TRANSITION_CHAIN_HEAD 0x80069210u
#define ZONE_TRANSITION_CHAIN_HEIGHT 0x80069214u
#define ZONE_TRANSITION_CHAIN_SOURCE 0x80069218u
/* Engine-owned runtime address domain, never unchecked host pointer casts.
 * Entry+0x10 identifies its header overlay; entry+4 is its actual EID.
 * Neighbor words at header+0x194 are odd EIDs or even resolved entry handles.
 * Providers map immutable original data plus explicit mutable runtime overlays.
 * Required accesses return zero or a negative error; no fabricated defaults,
 * implicit resolution/loading, pending callbacks or automatic retry exist.
 * Engine state and handles stay stable through budget continuations. */
typedef struct {
    void *context;
    int (*read)(void *context,uint32_t address,uint32_t *value);
    int (*write)(void *context,uint32_t address,uint32_t value);
} zone_transition_services;
typedef struct {
    zone_transition_services services;
    uint32_t entry,candidate,header,count,candidate_eid,index,scan_limit,phase;
    int active,status,contains;
} zone_transition_cursor;
/* Bounded native0x800214B8 predicate. Initialize cursor to zero. The original
 * signed count<=0 returns false without reading the candidate entry. A count
 * above scan_limit fails before traversing; limit is a port safety bound.
 * DONE exposes cursor.contains; budget is neighbors, plus initialization units.
 * Only the read provider is required. Failure is sticky until a new begin. */
int zone_transition_contains_begin(zone_transition_cursor *cursor,uint32_t entry,
    uint32_t candidate,uint32_t scan_limit,const zone_transition_services *services);
int zone_transition_contains_step(zone_transition_cursor *cursor,uint32_t budget);
/* Native0x80021530 initializer. Requires read/write providers for the complete
 * header overlay and all three explicit engine globals above. If header+0x2BC
 * bit0 is clear: write globals head=0,source=0,height=0x19000, then re-read
 * flags and set bit0. Existing bit0 preserves globals. Provider errors retain
 * completed writes. This does not unload zones or replace native exit0x8002157C,
 * which invokes object retirement0x8001AA98. */
int zone_transition_enter(uint32_t entry,const zone_transition_services *services);

typedef struct {
    zone_transition_services memory;
    /* Retain actual entry/player owners by generation before dereferencing.
     * Success returns a nonzero lease; failure publishes no ownership. The
     * entry lease also pins its header; memory.context pins engine globals. */
    int (*retain)(void *context,uint32_t address,uint64_t *lease);
    int (*release)(void *context,uint64_t lease);
    /* Actual1AA98(entry), including19A38/1997C traversal and1A9A8 callbacks.
     * begin runs once; step resumes the same dedicated service owner. Return
     * DONE/PENDING(1)/negative error. No synthesized retirement result allowed.
     * cancel is mandatory and quiesces/releases this owner after any begin,
     * including failed or completed calls. Failure retains cleanup ownership;
     * retry close, never replay the failed native exit. */
    int (*retire_begin)(void *context,uint32_t entry);
    int (*retire_step)(void *context,uint32_t budget);
    int (*retire_cancel)(void *context);
} zone_exit_services;

typedef struct {
    zone_exit_services services;
    uint64_t entry_lease,player_lease;
    uint32_t entry,player,player_zone,header,flags,phase;
    int active,status,retirement_owned;
} zone_exit_cursor;

/* Original2157C exit: redirect protected player's zone to80065D60 when it
 * equals the exiting entry; call real1AA98 only for header+2BC bit0; then
 * reread flags and clear bits0/1. Retain leases through explicit close even
 * after DONE/error. Initialize to zero; storage/services/timeline stay stable.
 * Registry-aware write provider must transfer actual object+18 zone ownership.
 * A budget unit is one bounded native phase or one retirement service step.
 * External pending returns BUDGET immediately. Failures are sticky and retain
 * completed writes. No object release is inferred from factory_retired(). */
int zone_exit_begin(zone_exit_cursor *cursor,uint32_t entry,const zone_exit_services *services);
int zone_exit_step(zone_exit_cursor *cursor,uint32_t budget);
/* Cancels/quiesces retirement first, then releases player and entry leases.
 * Cleanup failure keeps remaining ownership for retry. No native writes undo.
 * Closing an unfinished exit does not mark it complete or clear zone flags. */
int zone_exit_close(zone_exit_cursor *cursor);
#endif
