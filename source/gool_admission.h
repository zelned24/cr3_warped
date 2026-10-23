#ifndef GOOL_ADMISSION_H
#define GOOL_ADMISSION_H
#include <stdint.h>

enum { GOOL_ADMISSION_INVALID=-80,GOOL_ADMISSION_RANGE=-81 };
typedef struct {
    void *context;
    int (*read)(void *context,uint32_t address,uint32_t *value);
    int (*write)(void *context,uint32_t address,uint32_t value);
    /* Original database scalar service32DF8: value0/time0/mode2, other
     * arguments zero. Preserve read order; repeated calls can have effects. */
    int (*scalar)(void *context,uint32_t database,uint32_t key,uint32_t *value);
    /* Original32710 PATH key4B, value0/time0/mode2, no output counts. */
    int (*path)(void *context,uint32_t database,uint32_t *address);
    uint32_t protected_address,usage_address,usage_count;
} gool_admission_services;
typedef struct {
    uint32_t database,previous_chain,protected_slot;
    int admitted;
    int32_t native_result; /* -14 on native rejection;0 only when admitted */
} gool_admission_result;

/* Creation0x80019CE4..0x8001A084 before slot allocation. Resolves placement,
 * protected-slot guard, class34 chain bookkeeping, occupancy and key342 bit0.
 * Explicit providers own original metadata/runtime overlays; no default scalar
 * values or loading behavior are supplied. All callbacks must return0 or a
 * negative error; positive/pending results reject. Inputs are immutable except
 * deliberate provider side effects. On failure output is unchanged, but prior
 * native global writes remain visible. Do not retry after partial effects.
 * This admission is not allocation/init, spawning, path evaluation or a tick.
 * Initialize engine addresses explicitly (audited protected gp+218 and usage
 * table8006B688); usage_count is the actual mapped capacity, never inferred.
 */
int gool_admission(uint32_t entry,uint32_t entity_index,
                   const gool_admission_services *services,gool_admission_result *out);
#endif
