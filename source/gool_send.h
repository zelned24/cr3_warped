#ifndef GOOL_SEND_H
#define GOOL_SEND_H
#include "gool_vm.h"
/* Frontend for native wrapper 0x80037edc. Set vm.send_enabled only when an
 * engine pump owns delivery. Active sends yield a stable request; repeated
 * step cannot reevaluate operands or pop arguments while it is pending.
 * No state table lookup, target handler execution or acknowledgement is
 * invented here. Delivery must write TEMP_A at the native points. */
int gool_send_begin(gool_vm *vm,uint32_t instruction);
/* Call only after the engine service returned. Mirrors wrapper cleanup:
 * ordinary sender drops argc; replaced sender keeps the new stack if mode&2
 * or drops its new wait token otherwise. REPLACED requires engine context
 * handoff before another step. This function does not run a new pre_run. */
int gool_send_finish(gool_vm *vm);
#endif
