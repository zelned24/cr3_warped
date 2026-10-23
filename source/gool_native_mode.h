#ifndef GOOL_NATIVE_MODE_H
#define GOOL_NATIVE_MODE_H
#include "gool_native_actor.h"

/* Native1CA68 post-script mode timers and light selection over the actual
 * object/header domain. Single application thread, stopped actor, no callback
 * or budget yield during the operation. Does not perform movement/collision.
 * Writes remain visible on a provider error; the caller must not replay a
 * failed world step as though it had no effects. No heap or shadow light state.
 */
int gool_native_mode_update(gool_native_registry *registry,uint32_t node);
/* Native1CC74..1CD10 composition, also usable after real engine light changes.
 * Preserves packed six-byte colors and the original override precedence.
 */
int gool_native_light_compose(gool_native_registry *registry,uint32_t node);
#endif
