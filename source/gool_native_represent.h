#ifndef GOOL_NATIVE_REPRESENT_H
#define GOOL_NATIVE_REPRESENT_H
#include "gool_native_actor.h"
#include "gool_native_matrix.h"

enum {GOOL_NATIVE_REPRESENT_INVALID=-129};
typedef struct {
    gool_native_registry *registry;
    uint32_t *output,*written;
    uint32_t node,function,destination,angles,scale,extra;
    int bound,active,status;
} gool_native_represent;
/* Native1B630 representation preparation, no movement or presentation.
 * Stable caller-owned owner initially{0}. Caller retains the native object,
 * model/code owner, matrix storage and output words during this synchronous
 * operation. Errors retain owner identity until close; stores are not undone.
 * Actual representation function identifiers are preserved as data. Global
 * fallback selectors80067A30/34 must be explicitly mapped when needed.
 * Format indices outside the original16-entry table reject before dispatch.
 */
int gool_native_represent_bind(gool_native_represent *owner,gool_native_registry *registry);
/* Only bits for actually written output words are added to *written. Calls the concrete
 * original integer matrix builder selected by the actual format table. */
int gool_native_represent_run(gool_native_represent *owner,uint32_t node,uint32_t output[2],uint32_t *written);
/* Release owner bookkeeping after completion/error. Matrix work is synchronous
 * and leaves no pending CPU/DMA consumer. Does not undo actual writes. */
int gool_native_represent_close(gool_native_represent *owner);
#endif
