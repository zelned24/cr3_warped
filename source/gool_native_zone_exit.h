#ifndef GOOL_NATIVE_ZONE_EXIT_H
#define GOOL_NATIVE_ZONE_EXIT_H
#include "gool_native_actor.h"
#include "gool_zone_retire.h"

enum {GOOL_NATIVE_ZONE_EXIT_INVALID=-181};
typedef struct {
    gool_native_registry *registry;
    zdat_resources *bank;
    gool_domain *domain;
    gool_native_actor *player;
    gool_zone_retire_services retire_services;
    gool_zone_retire retirement;
    zone_exit_cursor exit;
    zdat_resource_lease zone_lease;
    uint32_t entry,player_node,player_index;
    int opened,zone_retained,player_retained,status;
} gool_native_zone_exit;

/* Actual2157C ->1AA98 composition over registry/domain/bank owners. Retains the
 * READY exiting zone and the actual stopped registered player read from the
 * engine global. Registry writes transfer object+18's zone lease before the
 * old zone can be evicted. Eight-root retirement is gool_zone_retire itself;
 * actual signal/retire/cancel providers remain mandatory. No default globals,
 * activation, notification, audio quiescence or successful deletion invented.
 * Initialize to zero. Registry, bank, domain, actor slots and provider storage
 * stay stable through close; only native callback graph mutation is permitted.
 */
int gool_native_zone_exit_begin(gool_native_zone_exit *owner,gool_native_registry *registry,
    uint32_t entry,const gool_zone_retire_services *services);
int gool_native_zone_exit_step(gool_native_zone_exit *owner,uint32_t budget);
/* Cancels deepest retirement work before releasing protected player and zone.
 * Busy/error keeps remaining leases and cursor storage for cleanup retry.
 * Player must be stopped before release; completed native writes remain. */
int gool_native_zone_exit_close(gool_native_zone_exit *owner);
#endif
