#ifndef LEVEL_NATIVE_H
#define LEVEL_NATIVE_H
#include "gool_native_actor.h"
#include "gool_native_pool.h"
#include "zdat_native.h"
#include "zdat_resources.h"
#include "model_resources.h"
/* Readied behavior bank + original metadata -> level-owned native mappings.
 * Mutable NSD copy is separate from immutable original bytes.80061044 points
 * to its actual directory-end metadata block; code entries map at90000000.
 * Owns the original96-slot/reserved-player pool bootstrap. Unknown globals,
 * timing and activation require their original services.
 * Close VM/actor users before this owner, then behavior bank, then NSF session.
 * BUSY preserves the whole live level. All calls belong to the owner thread. */
int level_native_open(uint32_t level);
int level_native_close(void);
int level_native_level(void);
zdat_resources *level_native_zones(void);
model_resources *level_native_models(void);
zdat_native *level_native_database(void);
gool_native_registry *level_native_registry(void);
gool_native_pool *level_native_pool(void);
gool_domain *level_native_domain(void);
gool_native_resources *level_native_resources(void);
#endif
