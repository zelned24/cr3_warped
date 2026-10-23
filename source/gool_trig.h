#ifndef GOOL_TRIG_H
#define GOOL_TRIG_H
#include <stdint.h>

/* Original integer sine/cosine: 4096 angles per turn, amplitude 4096. */
int32_t gool_trig_sin(int32_t angle);
int32_t gool_trig_cos(int32_t angle);
#endif
