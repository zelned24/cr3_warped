#ifndef GOOL_RESOURCES_H
#define GOOL_RESOURCES_H
#include "gool_data.h"
#include "nsf_resource.h"

#define GOOL_RESOURCES_SLOTS 64u
#define GOOL_RESOURCE_SLOT_BYTES 65536u
enum {GOOL_RESOURCES_INVALID=-97,GOOL_RESOURCES_UNAVAILABLE=-98,
      GOOL_RESOURCES_MISSING=-99,GOOL_RESOURCES_FULL=-100,GOOL_RESOURCES_BUSY=-101};
typedef struct {uint32_t eid,lookup_index;} gool_resource_catalog;
typedef enum {GOOL_RESOURCE_EMPTY,GOOL_RESOURCE_LOADING,GOOL_RESOURCE_READY,GOOL_RESOURCE_FAILED} gool_resource_state;
typedef struct {
    nsf_resource_ticket ticket;
    nsf_resource_view view;
    gool_data data;
    uint32_t eid,lookup_index,entry_size;
    gool_resource_state state;
    int has_ticket,error;
} gool_resource_slot;
typedef struct {
    gool_data_source source;
    gool_resource_slot slots[GOOL_RESOURCES_SLOTS];
    const gool_resource_catalog *catalog;
    uint8_t *storage;
    uint64_t generation;
    uint32_t catalog_count,slot_count,references;
    int open,closing;
} gool_resources;

/* Fixed storage bank for original behavior entries. Initialize owner to{0} and
 * keep it stable. Caller supplies slot_count*65536 bytes, maximum4MiB, normally
 * one static pool; no per-request allocations or complete NSF/CGA blob exists.
 * Catalog contains real level EIDs and stable port code-domain lookup IDs, not
 * request arrival order. Catalog/storage outlive the bank and all VM consumers.
 * Existing NSF resource session belongs to level_resources and must stay open
 * until all requests/views here are released. Poll on the owner application
 * thread; copy complete validated entries into the fixed bank, then return
 * page leases. Resident entries are never evicted while this bank is open.
 * Exhaustion fails explicitly; no borrowed code is overwritten to admit more.
 */
int gool_resources_open(gool_resources *bank,const gool_resource_catalog *catalog,
                          uint32_t catalog_count,void *storage,uint32_t slot_count);
int gool_resources_request(gool_resources *bank,uint32_t eid);
/* Return number loading, or first error after advancing other pending entries.
 * Failed entries retain their error; reopen only after complete bank shutdown. */
int gool_resources_poll(gool_resources *bank);
int gool_resources_get(gool_resources *bank,uint32_t eid,gool_data *out);
/* Complete validated original entry bytes in retained fixed storage. */
int gool_resources_entry(gool_resources *bank,uint32_t eid,const uint8_t **bytes,uint32_t *size);
/* Retain one bank lease for every VM/object group that can hold any copied
 * base/alternate/library view. Release exactly once after complete retirement.
 * Token generation prevents releasing a previous level's bank lifetime. */
int gool_resources_retain(gool_resources *bank,uint64_t *generation);
int gool_resources_release(gool_resources *bank,uint64_t generation);
/* BUSY leaves resident code valid while users remain. Once users are gone,
 * cancel pending I/O and release all resource references before closing NSF.
 * Does not call nsf_resource_close or free caller storage. Retry release errors
 * while NSF remains open; state and ownership flags remain available. */
int gool_resources_close(gool_resources *bank);
#endif
