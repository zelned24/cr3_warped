#include "gool_trig.h"
#include "gool_trig_table.h"

static int32_t sine_word(uint32_t angle) {
    uint32_t phase = angle & 0xfffu;
    uint32_t offset = phase & 0x3ffu;
    if (phase & 0x400u) offset = 1024u - offset;
    int32_t value = gool_trig_quarter[offset];
    return phase & 0x800u ? -value : value;
}
int32_t gool_trig_sin(int32_t angle) {
    return sine_word((uint32_t)angle);
}
int32_t gool_trig_cos(int32_t angle) {
    return sine_word((uint32_t)angle + 1024u);
}
