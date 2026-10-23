#ifndef GOOL_NATIVE_MOVEMENT_H
#define GOOL_NATIVE_MOVEMENT_H
#include "gool_domain.h"

/* Original20184 camera-relative vector: actual engine angles80065D04/65DA4,
 * original trig/Q12 matrix composition, transpose, then18DD4 low32 dots.
 * Input/output are retained caller snapshots; output may alias input. */
int gool_native_movement_camera_vector(gool_domain *domain,const uint32_t input[3],uint32_t output[3]);
/* Complete original1E4BC..1E788 motion controller. Reads actual control words
 * 80065C04/08/0C and frame owner80061A7C; modifies node84/8C/94/104 in native
 * order. parameters is the actual six-word native movement table; previous is
 * the actual prior/checkpoint three-word vector. No host delta/default input,
 * float velocity, synthetic collision response or allocation is supplied. */
int gool_native_movement_control(gool_domain *domain,uint32_t node,uint32_t parameters,uint32_t previous);
#endif
