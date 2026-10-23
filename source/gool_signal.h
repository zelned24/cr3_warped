#ifndef GOOL_SIGNAL_H
#define GOOL_SIGNAL_H
#include "gool_data.h"

typedef enum {
    GOOL_SIGNAL_OUTSIDE_TABLE,GOOL_SIGNAL_NONE,GOOL_SIGNAL_STATE,GOOL_SIGNAL_SCRIPT
} gool_signal_kind;
typedef struct {
    gool_signal_kind kind;
    uint32_t signal,raw,value;
} gool_signal_route;
/* Decode default route, not a delivery/acknowledgement or state transition.
 * Preserve bit-0x8000 code addresses; never reinterpret them as state IDs.
 * No output change on invalid table/state. Outside-table signals are ignored. */
int gool_signal_lookup(const gool_data *base,uint32_t raw_signal,gool_signal_route *out);
/* Original send service uses base behavior code and masks raw with 0x3fff;
 * bit 0x4000 does not select the alternate behavior for this kind of route. */
int gool_signal_script(const gool_data *base,const gool_signal_route *route,
                       gool_data *owner,uint32_t *pc);
#endif
