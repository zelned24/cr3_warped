#ifndef GOOL_NATIVE_POOL_H
#define GOOL_NATIVE_POOL_H
#include "gool_domain.h"
#define GOOL_NATIVE_POOL_COUNT 96u
#define GOOL_NATIVE_POOL_STRIDE 0x23cu
#define GOOL_NATIVE_PLAYER_BYTES 0x33cu
#define GOOL_NATIVE_POOL_REGIONS 102u
#define GOOL_NATIVE_POOL_BASE 0xa0000000u
#define GOOL_NATIVE_PLAYER_BASE 0xa0010000u

typedef struct {
    gool_domain *domain;
    gool_domain_lease regions[GOOL_NATIVE_POOL_REGIONS];
    uint32_t mapped;
    int open;
    uint8_t objects[GOOL_NATIVE_POOL_COUNT][GOOL_NATIVE_POOL_STRIDE];
    uint8_t player[GOOL_NATIVE_PLAYER_BYTES];
    uint8_t pointers[8],roots[80],protected_object[4];
    uint8_t player_aux[0x6c],object_aux[GOOL_NATIVE_POOL_COUNT][0x30];
} gool_native_pool;
/* Stable, caller-owned storage initially {0}. Reopening preserves slot/aux
 * bytes except writes proved in SCUS_942.44 bootstrap 8002049C..800205F4.
 * Each object has its own root mapping; stack overlays cannot cross slots.
 * Native allocator addresses become explicit port handles. Original global,
 * root and auxiliary addresses are retained. No heap or unknown-global mapping.
 * This bootstraps storage only: it does not activate or execute placements. */
int gool_native_pool_open(gool_native_pool *pool,gool_domain *domain);
/* Refuses outstanding actor overlays/borrows before releasing any mapping. */
int gool_native_pool_can_close(const gool_native_pool *pool);
int gool_native_pool_close(gool_native_pool *pool);
/* Original1D588 for quiescent raw slots. Refuses actor overlays/borrows;
 * attached VMs must use their synchronized frame service instead. Validates
 * actual behavior stackStart, argument extent and the native slot boundary. */
int gool_native_pool_object_end(void *context,uint32_t node,uint32_t *end);
int gool_native_pool_enter_frame(void *context,uint32_t node,uint32_t argc,uint32_t flags);
/* index96 selects the reserved player; indices0..95 select ordinary slots. */
int gool_native_pool_object(gool_native_pool *pool,uint32_t index,
                            uint32_t *address,gool_domain_lease *lease);
#endif
