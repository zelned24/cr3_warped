#ifndef GOOL_NATIVE_ADDRESS_H
#define GOOL_NATIVE_ADDRESS_H
#include "gool_native_actor.h"
typedef struct {
    gool_native_registry *registry;
    /* Actual mapped gp+1A8 word selected by the engine, never an inferred gp. */
    uint32_t toggle_address;
} gool_native_address;
/* Original35410 slow operand-address routine. Literal addresses alternate
 * between actual scratch1F800040/44; toggle storage must be mapped explicitly.
 * DirectE00/STACK is resolved by35D08 before this routine is entered. Frame,
 * constant and linked addresses follow raw native memory, not cached VM flow.
 * Retains no work and allocates nothing. Caller owns mappings and timeline. */
int gool_native_address_resolve(void *context,uint32_t node,uint32_t operand,uint32_t *address);
#endif
