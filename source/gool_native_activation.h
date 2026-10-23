#ifndef GOOL_NATIVE_ACTIVATION_H
#define GOOL_NATIVE_ACTIVATION_H
#include "gool_domain.h"
enum {GOOL_NATIVE_ACTIVATION_DONE=0,GOOL_NATIVE_ACTIVATION_PENDING=1,
      GOOL_NATIVE_ACTIVATION_INVALID=-184,GOOL_NATIVE_ACTIVATION_CAPACITY=-185};
typedef struct {
    gool_domain *domain;
    uint32_t gp,capacity,marker,list,remaining,index,count,raw_count,value,phase;
    int opened,status,reset;
} gool_native_activation;
/* Native1AC74/1ACAC activation-record operations at80067A38,12-byte rows.
 * capacity is the actual retained table allocation, not a fabricated native
 * constant. Caller owns mapped table, gp+21C and source words through close.
 * Reset writes13B to each live row+8. Registration walks source words in
 * reverse, matches keys under00FFFF00, updates existing rows or appends in
 * native store order. Other row fields stay untouched. No object is spawned.
 * Each budget unit handles one bounded scan/publication stage. Preserve
 * original repeated source/count reads and overlapping-span effects. Sticky
 * faults retain partial writes; never replay a failed invocation. No heap.
 */
int gool_native_activation_reset_begin(gool_native_activation *owner,gool_domain *domain,
    uint32_t gp,uint32_t capacity);
int gool_native_activation_register_begin(gool_native_activation *owner,gool_domain *domain,
    uint32_t gp,uint32_t capacity,uint32_t marker,uint32_t list,uint32_t count);
int gool_native_activation_step(gool_native_activation *owner,uint32_t budget);
int gool_native_activation_close(gool_native_activation *owner);
#endif
