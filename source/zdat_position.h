#ifndef ZDAT_POSITION_H
#define ZDAT_POSITION_H
#include "zdat_runtime.h"

/* Native position block0x800207B0..0x800207F4 only: sign-extend each LH,
 * SLLV using shift&31, ADDU the explicit zone offset, SLL8; every operation
 * wraps32 bits. No interpolation, object flags, orientation, activation or
 * checkpoint override is implied. Output is native fixed-point, not WGEO.
 * Inputs may alias output: all values are captured before output writes. */
int zdat_position_native(const int16_t point[3],const int32_t offset[3],
                          uint32_t shift,int32_t out[3]);
/* Bounded raw PATH point access using validated immutable ZDAT views. Only
 * original signed16-vector PATH values are accepted; an explicit zone offset
 * is required. Retain the owning page/archive through this call. The full
 * native service's signed24-bit parameter selection remains the engine's job.
 * Rejection preserves output. This does not change the checked int64 decoder. */
int zdat_entity_position_native(const zdat_zone *zone,const zdat_entity *entity,
                                 uint32_t point,int32_t out[3]);
#endif
