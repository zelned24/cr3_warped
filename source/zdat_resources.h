#ifndef ZDAT_RESOURCES_H
#define ZDAT_RESOURCES_H
#include "nsf_resource.h"
#include "zdat_native.h"
#include "zone_transition.h"
#define ZDAT_RESOURCES_SLOTS 4u
#define ZDAT_RESOURCE_BYTES 65536u
enum {ZDAT_RESOURCE_EMPTY=0,ZDAT_RESOURCE_LOADING=1,ZDAT_RESOURCE_READY=2,ZDAT_RESOURCE_FAILED=3,
      ZDAT_RESOURCE_INVALID=-114,ZDAT_RESOURCE_BUSY=-115,ZDAT_RESOURCE_FULL=-116};
typedef struct {
    nsf_resource_ticket ticket;nsf_resource_view view;gool_domain_lease mapping;
    uint64_t generation;uint32_t eid,address,references,size;
    int state,error,has_ticket,mapped;
} zdat_resource_slot;
typedef struct {
    gool_domain *domain;uint8_t *storage;uint32_t base;
    uint64_t generation;uint32_t allowed_types;int open,closing;
    zdat_resource_slot slots[ZDAT_RESOURCES_SLOTS];
} zdat_resources;
typedef struct {uint64_t generation;uint32_t slot;} zdat_resource_lease;
/* Four fixed64KiB CPU copies, independent of the NSF worker's page pool.
 * Original bytes are validated before native item pointers replace offsets in
 * the runtime copy. Mutable zone headers/databases preserve per-zone flags and
 * actual owner/fallback links. No world/texture/audio ownership is implied.
 * Initialize owner to{0}; supply stable4*65536 storage and aligned base with
 * 128KiB virtual stride. Bank/session/domain outlive all consumers. */
int zdat_resources_open(zdat_resources *bank,gool_domain *domain,void *storage,uint32_t base);
/* Shared bounded NSF copy/lease machinery for native SVTX/TGEO entries.
 * Models map readonly and do not run zone database linking. Normal zone open
 * remains type7-only. Storage/address/ownership rules are identical. */
int zdat_resources_open_models(zdat_resources *bank,gool_domain *domain,void *storage,uint32_t base);
int zdat_resources_request(zdat_resources *bank,uint32_t eid);
/* Revalidate/copy a retained original page into an independent native bank.
 * Does not consume the caller's view/ticket; no page borrow survives return. */
int zdat_resources_import(zdat_resources *bank,const nsf_resource_view *view);
/* Advances asynchronous requests; returns pending count or sticky first error. */
int zdat_resources_poll(zdat_resources *bank);
/* Every camera/object/zone-transition user retains its own generation lease.
 * Unborrowed READY entries remain resident until explicit eviction or close. */
int zdat_resources_acquire(zdat_resources *bank,uint32_t eid,uint32_t *address,zdat_resource_lease *lease);
/* Retain an already mapped exact entry handle (object+18 ownership). Never
 * starts I/O or accepts an interior pointer, stale address, or failed slot. */
int zdat_resources_acquire_address(zdat_resources *bank,uint32_t address,zdat_resource_lease *lease);
int zdat_resources_release(zdat_resources *bank,zdat_resource_lease lease);
int zdat_resources_evict(zdat_resources *bank,uint32_t eid);
int zdat_resources_can_close(const zdat_resources *bank);
int zdat_resources_close(zdat_resources *bank);
typedef struct {
    zdat_resources *bank;
    zdat_resource_lease previous_lease,candidate_lease;
    zone_transition_cursor predicate;
    uint32_t previous,candidate;
    int previous_retained,candidate_retained,active,status,entered;
} zdat_zone_prepare;
/* Complete the trailing21334..21460 adjacency/entry-initializer boundary after
 * copy_zone has performed the database links. previous is the actual engine
 * previous-zone handle (zero skips the conditional initializer). Retains both
 * zones throughout budget continuation. Required chain globals must be mapped
 * by their real engine owner; missing globals fail without fabricated values.
 * Successful entry-init is not camera activation, entity spawn or retirement.
 * close releases job leases after completion/error/cancellation. The owner must
 * close this cursor before bank shutdown, even after predicate/provider failure. */
int zdat_zone_prepare_begin(zdat_zone_prepare *prepare,zdat_resources *bank,
                            uint32_t previous,uint32_t candidate,uint32_t scan_limit);
int zdat_zone_prepare_step(zdat_zone_prepare *prepare,uint32_t budget);
int zdat_zone_prepare_close(zdat_zone_prepare *prepare);
#endif
