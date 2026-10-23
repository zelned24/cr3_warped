#ifndef GOOL_MATH_H
#define GOOL_MATH_H
#include <stdint.h>

/* Native 0x22 and 0x25 result arithmetic. Operands are already evaluated in
 * native order by the VM. No allocation, floating point, or global state. */
int32_t gool_math_approach(int32_t current, int32_t target, int32_t rate);
int32_t gool_math_approach_angle(int32_t current, int32_t target,
                                 int32_t rate, int32_t frame_factor);
/* Native395E0 oscillator updates its stored phase and returns a signed
 * magnitude; native39344 integrates MULT/MFLO followed by SRA10 and ADDU. */
int32_t gool_math_oscillate(int32_t current,int32_t bound,int32_t rate,int32_t *next);
int32_t gool_math_integrate(int32_t current,int32_t velocity,int32_t factor);
#endif
