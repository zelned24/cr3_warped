#ifndef GOOL_ATAN_H
#define GOOL_ATAN_H
#include <stdint.h>
/* Original integer atan2(y,x), signed angle units (4096 per revolution).
 * Returns -1 without modifying result if wrapped original arithmetic would
 * trap or address outside the original ratio table. No floating-point math. */
int gool_atan2(int32_t y,int32_t x,int32_t *result);
#endif
