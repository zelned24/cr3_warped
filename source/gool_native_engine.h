#ifndef GOOL_NATIVE_ENGINE_H
#define GOOL_NATIVE_ENGINE_H
#include "gool_domain.h"

/* Native2FC50 and3079C scalar bootstrap. Actual engine storage is mapped and
 * owned by the caller; preserve every byte not written by these routines.
 * Failures preserve prior native stores and must not be replayed silently. */
int gool_native_engine_defaults(gool_domain *domain);
int gool_native_engine_random_init(gool_domain *domain);
/* Ordinary-level scalar prefix210BC..210E4, after the original resource-magic
 * branch excludes6396347F. Caller still owns usage restoration, camera setup,
 * checkpoint selection and215E8 activation; this is not full level entry. */
int gool_native_engine_level_begin(gool_domain *domain);
/* Original frame tail16CB4..16CBC restores effective flags from base flags. */
int gool_native_engine_frame_flags(gool_domain *domain);
/* RAM effects of188EC, including the complete18DA8 transform initializer.
 * Caller must apply4F724(0x120) projection and4F704(0,0) screen offsets to its
 * active renderer. This function neither invokes nor replaces those services. */
int gool_native_engine_camera_reset(gool_domain *domain);
/* Original2FCA0 new-game/reset stores; both real512-byte save/usage banks
 * must be mapped. mode0 only resets the per-level usage bank. */
int gool_native_engine_new_game(gool_domain *domain,uint32_t mode);
/* Readonly original dispatch table8005D120..8005D268. The returned lease is
 * caller-owned and must outlive native scratch initialization/dispatch users. */
int gool_native_engine_map_dispatch(gool_domain *domain,gool_domain_lease *lease);
/* Native1AB64: sample actual phase/frame timer, copy the native halfword
 * dispatch table to scratch60 and publish scratch50/54/58/5C in native order.
 * Caller retains the real timer, engine globals, table and full scratch span.
 * This does not invent a clock, delta, global-array bound or initial phase. */
int gool_native_engine_prepare(gool_domain *domain);
#endif
