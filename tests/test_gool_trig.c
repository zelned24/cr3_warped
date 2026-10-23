#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include "gool_trig.h"
#ifdef NDEBUG
#error Assertions are required
#endif
int main(void) {
    assert(gool_trig_sin(0) == 0 && gool_trig_cos(0) == 4096);
    assert(gool_trig_sin(1024) == 4096 && gool_trig_cos(1024) == 0);
    assert(gool_trig_sin(2048) == 0 && gool_trig_cos(2048) == -4096);
    assert(gool_trig_sin(3072) == -4096 && gool_trig_cos(3072) == 0);
    assert(gool_trig_sin(-1024) == -4096);
    assert(gool_trig_sin(INT_MIN) == 0);
    assert(gool_trig_cos(INT_MAX) == 4096);
    for (int32_t angle = 0; angle < 4096; angle++) {
        int32_t value = gool_trig_sin(angle);
        assert(value >= -4096 && value <= 4096);
        assert(value == -gool_trig_sin(angle+2048));
        assert(value == gool_trig_cos(angle-1024));
        assert(value == gool_trig_sin(angle-4096));
    }
    puts("PASS original integer trig: cardinal angles, full-period symmetry and wrapping");
    return 0;
}
