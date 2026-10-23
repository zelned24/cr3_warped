#ifndef ZDAT_PATH_NATIVE_H
#define ZDAT_PATH_NATIVE_H
#include "zdat_native.h"
enum {ZDAT_PATH_UNSUPPORTED=-118,ZDAT_PATH_ARITHMETIC=-119};
/* Original20670 path, including projection and normal20C6C branches. Reads the
 * real node+F0 database, its zone owner/offset, shift key30E (native default2),
 * signed magnitude index, endpoint fraction, flags/orientation and wrapping
 * interpolation. Stores native fixed-point output; no WGEO scaling or render
 * delta substitution. Output may alias node+60, matching creation's call.
 * Projection bit8000 uses the original integer square root, wrapped dot/cross
 * products, forward segment retry, signed lateral distance, flags and+D4 path.
 * Retry count is bounded by the actual path length; invalid native extents
 * reject rather than read outside the retained database/slot.
 * Non-ZDAT owner paths use native uninitialized CPU temporaries and reject.
 * Provider status and original return count/-14 remain separate. Mapped slot,
 * zone and database storage must remain retained throughout this call.
 * Writes before an angle/domain error remain visible, like other native calls.
 */
int zdat_path_native_position(zdat_native *source,uint32_t node,int32_t parameter,
                               uint32_t output,int32_t *native_result);
/* gool_create position callback: preserves the provider/native-result split,
 * converts a missing database's native-14 into an explicit provider failure. */
int zdat_path_native_create(void *context,uint32_t node,uint32_t parameter,uint32_t output);
#endif
