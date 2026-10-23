#ifndef LEVEL_BEHAVIORS_H
#define LEVEL_BEHAVIORS_H
#include "gool_resources.h"
#include "gool_native_resources.h"
#define LEVEL_BEHAVIORS_DIRECTORY "sdmc:/3ds/crashwarped3ds/runtime"
#define LEVEL_BEHAVIOR_SLOTS 20u
enum {LEVEL_BEHAVIORS_READY=0,LEVEL_BEHAVIORS_LOADING=1};
/* Level-owned fixed1310720-byte behavior pool. Supplied catalogs contain at
 * most18 behaviors; capacity20 is a port budget, not an original format limit.
 * Open original level_resources first. Close/release every VM user before level
 * replacement, then close this bank before the NSF resource session. All calls
 * belong to the owner application thread; no bank/VM storage is moved. */
int level_behaviors_open(const char *directory,uint32_t level);
int level_behaviors_poll(void);
int level_behaviors_get(uint32_t eid,gool_data *out);
int level_behaviors_retain(uint64_t *generation);
int level_behaviors_release(uint64_t generation);
int level_behaviors_close(void);
int level_behaviors_level(void);
/* Retains this level's resident bank through an explicit native mapping owner.
 * Close that owner before closing/replacing this level. */
int level_behaviors_bind_native(gool_native_resources *resources,gool_domain *domain,uint32_t virtual_base,void *constants,uint32_t capacity);
#endif
