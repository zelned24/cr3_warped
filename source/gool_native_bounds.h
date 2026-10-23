#ifndef GOOL_NATIVE_BOUNDS_H
#define GOOL_NATIVE_BOUNDS_H
#include "gool_domain.h"
/* Native3188C point versus transformed AABB. box is six full32-bit words:
 * minX/Y/Z,maxX/Y/Z; point is three full32-bit words. Signed comparisons,
 * inclusive minima and exclusive maxima, no rescaling or extent addition.
 * This differs from zone317C0's origin/extents shifted by8. Both addresses
 * reference retained actual native storage; no host-pointer casts/allocation.
 * Reads preserve original short-circuit order; errors preserve result output.
 * Does not fabricate transformed bounds, GTE state or2D0E8 cache ownership.
 */
int gool_native_bounds_contains(gool_domain *domain,uint32_t point,uint32_t box,int *contains);
/* Original2D8C8 and2D85C cache stack operations. Actual global pointer/count,
 * 72-entry record table and2304-byte arena must be mapped by the engine.
 * All callers/consumers must be quiescent before reset; pop invalidates its
 * last returned record. No guessed cache globals or implicit backing allocation.
 * Malformed pointer/count/type rejects rather than crossing native storage. */
int gool_native_bounds_reset(gool_domain *domain);
int gool_native_bounds_pop(gool_domain *domain);
/* Original2D0E8 non-SVTX branch only, including actual cache publication and
 * default scale-derived box2D250..2D36C. Requires a real nonzero model whose
 * type is not1; model/SVTX callers reject before cache mutation. Other model
 * hitbox branches require original transforms and remain unimplemented here.
 * Returns a borrowed native32-byte record (box atrecord+8). It stays valid
 * until native pop/reset/reuse; caller retains its object/model and mappings.
 * Error retains completed native stores; do not replay failed allocation.
 */
int gool_native_bounds_fallback(gool_domain *domain,uint32_t node,uint32_t *record);
/* Native3148C transformed bounding box, using packed signed16 matrix atmatrix
 * and native position words attranslation. Preserves wrapping MULT/MFLO sums,
 * truncated center scaling, expanded extent math and upper/lower store order.
 * Actual source/output mappings remain caller-owned. No GTE/float substitute.
 * This implements memory results; model-frame selection/cache is separate. */
int gool_native_bounds_transform(gool_domain *domain,uint32_t output,uint32_t box,
                                 uint32_t matrix,uint32_t translation);
/* Native2D164..2D830 hitbox from the actual selected SVTX frame
 * atobject auxiliary+20. Copies the original local box/position and computes
 * its transformed AABB atrecord+2C. No fabricated frame or hitbox selection.
 * Includes nonoriented center transformation, original yaw quadrants, scaled
 * local extents and protected-player mode padding. No unknown frame selection.
 * Out-of-range index returnsrecord0. Errors after reserve retain cache effects.
 * Source frame and auxiliary matrix stay retained and stable; callers pop the
 * returned record only after every reader has completed. */
int gool_native_bounds_svtx(gool_domain *domain,uint32_t node,uint32_t index,uint32_t *record);
#endif
