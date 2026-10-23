#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include "gool_math.h"
#ifdef NDEBUG
#error Assertions are required
#endif
int main(void) {
    assert(gool_math_approach(0, 1000, -300) == 300);
    assert(gool_math_approach(0, -1000, -300) == -300);
    assert(gool_math_approach(100, 101, 256) == 101);
    assert(gool_math_approach(100, 99, 256) == 99);
    assert(gool_math_approach(INT_MAX, INT_MIN, 256) == INT_MIN);
    assert(gool_math_approach(0, INT_MIN, INT_MIN) == INT_MIN);
    assert(gool_math_approach_angle(4090, 10, 4, 1024) == 4094);
    assert(gool_math_approach_angle(10, 4090, 4, 1024) == 6);
    assert(gool_math_approach_angle(4090, 10, 256, 1024) == 10);
    assert(gool_math_approach_angle(0, 4097, 256, 1024) == 4097);
    assert(gool_math_approach_angle(4090, 10, -4, 1024) == 4086);
    assert(gool_math_approach_angle(10, 4090, INT_MIN, 1024) == 10);
    int32_t scalar = -5000, angle = 4000;
    for (unsigned frame = 0; frame < 100; frame++) {
        scalar = gool_math_approach(scalar, 5000, 128);
        angle = gool_math_approach_angle(angle, 100, 4, 1024);
    }
    assert(scalar == 5000 && angle == 100);
    puts("PASS native approach: wrapping boundaries, signed rates, angle seam and convergence");
    return 0;
}
