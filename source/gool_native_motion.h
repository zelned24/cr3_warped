#ifndef GOOL_NATIVE_MOTION_H
#define GOOL_NATIVE_MOTION_H
#include "gool_domain.h"

/* Original1FAC8..1FC3C angular controller, used by1E4BC's motion path.
 * Reads actual frame owner80061A7C/+18 (unsigned clamp102); actor is either
 * zero, or an actual retained mutable actor for AC smoothing/A8 status bits.
 * Native current/target/rate words are preserved, including signed overflow
 * and target's unmasked immediate-snap result. No presentation delta exists.
 * Errors preserve completed native writes; output changes only on success. */
int gool_native_motion_angle(gool_domain *domain,uint32_t current,uint32_t target,
                             uint32_t rate,uint32_t actor,uint32_t *result);
#endif
