#ifndef GOOL_NATIVE_MATRIX_H
#define GOOL_NATIVE_MATRIX_H
#include "gool_domain.h"

enum {GOOL_NATIVE_MATRIX_INVALID=-128};
/* Original3D470 orientation entry and shared3E7D4..3E8C4 tail. Arguments are
 * actual native addresses: output32 bytes, angles3 words, scale3 words and an
 * optional3-word model scale (zero means absent). Caller retains all mappings,
 * uses a stopped object and excludes concurrent writes. No allocation/yield.
 * Writes full scale at+14/+18/+1C, then five row-major packed signed-halfword
 * rotation words at+0..10 in native order. The fifth word is sign extended.
 * Rotation is Ry*Rx*Rz, with the original clockwise Z convention, and Q12
 * GTE signed IR saturation after every composition. Scale products retain only
 * MULTU's low32 bits before arithmetic shift12. The original OR-based identity
 * shortcut is preserved, including nonuniform/zero scale combinations.
 * Uses the supplied original trig table; no float matrix approximation.
 * This exposes memory results, not PS1 CPU registers/GTE FLAG side effects.
 * Mapping/write failures return directly; completed output stores remain.
 */
int gool_native_matrix_3d470(gool_domain *domain,uint32_t output,uint32_t angles,
                            uint32_t scale,uint32_t model_scale);
/* All four actual orientation-table builders, selected by native code ID:
 * 3D470:Ry(Y)*Rx(X)*Rz(Z);3E754:Rz(Z)*Rx(X);
 * 3F044:Ry(Z)*Rx(X)*Ry(Y-Z);40C20:Rz(Z)*Rx(X)*Ry(Y).
 * Rz uses the original clockwise sign convention. Rx/final compositions are
 * conditional on the original raw angle word, preserving intermediate Q12
 * saturation/rounding. This is synchronous and retains no callback work.
 * Unknown function IDs reject before any native output write. */
int gool_native_matrix_build(gool_domain *domain,uint32_t function,uint32_t output,
                             uint32_t angles,uint32_t scale,uint32_t model_scale);
/* Native1FE98/4F404/4F544 memory-result arithmetic for CPU point transforms.
 * Ry(Z)*Rx(X)*Ry(Y-Z); Q12 IR clamp during composition, then per-column
 * low32 scaled products/SRA12 packed to signed16 WITHOUT an IR clamp.
 * Unlike3F044's shared tail, no OR-based identity shortcut is taken here.
 * Output is caller-local matrix storage, published only after valid reads. */
int gool_native_matrix_1fe98(gool_domain *domain,uint32_t angles,uint32_t scale,int32_t output[3][3]);
/* Original41EF0..41FD4 memory results:load the actual five-word a0 memory
 * rotation as left operand, multiply by columns of the caller's t0..t4
 * five-word rotation, Q12 shift/IR clamp, then native packed sign extension.
 * Inputs are explicit retained snapshots, not inferred camera matrices.
 * Output may alias either input; published after all arithmetic completes.
 * CPU/GTE register/FLAG state is outside this memory-result API. */
int gool_native_matrix_41ef0(const uint32_t current[5],const uint32_t left_rotation[5],uint32_t output[5]);
/* Original41FD8..42054 alternative projection-axis preparation:160/256 signed
 * low32 multiply/shift and native negation/repacking. Preserves all original
 * word operations, including full32 negation of word4; no GTE clamp. */
int gool_native_matrix_41fd8(const uint32_t current[5],uint32_t output[5]);
typedef struct {
    uint32_t packet_flags,position[3],camera_position[3],camera_rotation[5];
    uint32_t frame_offset[2],second_offset[2],geometry_scale[3],object_scale[3];
} gool_native_translation_input;
typedef struct {uint32_t translation[3],source_y;} gool_native_translation;
/* Original42058..4229C memory results. Inputs correspond to the actual render
 * packet flags, frame+8/+C words, TGEO item0 scale, gp60..68 position,
 * gp78..80 scale, scratch34..3C camera position and caller's original matrix.
 * Frame offsets apply only for packet bit30; bit29 selects paired averaging.
 * Preserve native packed-low-half average wrap, signed low32 MULT/MFLO stages,
 * position SRA8 and per-product full64 SRA12 BEFORE wrapping dot additions.
 * source_y is the unshifted adjusted word stored at scratchFC. Translation
 * words are actual TRX/TRY/TRZ inputs; no GTE clamp/flags/projection is inferred.
 * Caller supplies engine snapshots; no global defaults or guessed units.
 */
int gool_native_matrix_translation_42058(const gool_native_translation_input *input,
    gool_native_translation *output);
#endif
