#ifndef GOOL_NATIVE_RENDER_H
#define GOOL_NATIVE_RENDER_H
#include "gool_native_matrix.h"
typedef struct {
    uint32_t current_rotation[5],auxiliary_rotation[5],camera_angles[3]; /* camera,object */
    uint32_t light_vectors[2][2],light_matrix[5],projection_distance;
    uint32_t packet_flags,packet_color,packet_word50;
    uint32_t fog_threshold,fog_shift,fog_color;
    gool_native_translation_input translation;
} gool_native_render_input;
typedef struct {
    uint32_t rotation[5],translation[3],light_matrix[5];
    uint32_t packet_flags,packet_color,source_y;
    int light_written,color_written,flags_written,source_y_written;
} gool_native_render_output;
/* Memory-result implementation of every handler in original8005C1F0:3D030,
 * 3D054,3D078,3D09C,3E67C,3D0C0,3CFA8,3CFFC,3CE70,3D3AC,3EBE0,3EC34.
 * Input is an explicit immutable snapshot of the real packet, renderer scratch,
 * gp and prior GTE control values. No context is inferred from a chosen camera.
 * Handles native orientation/translation, alternate flat projection, Y bias,
 * camera-oriented light vectors and decorated fog packet writes. Preserves
 * untouched light words and reports optional stores separately.
 * Does not emulate GTE command/register/FLAG timing, vertex RTPS or lighting
 * of actual normals/RGB. Output unchanged on failure and may alias input.
 */
int gool_native_render_prepare(uint32_t function,const gool_native_render_input *input,
    gool_native_render_output *output);
/* Original1B7A0 type1 packet publication. frame is the actual renderer owner
 * referenced by80061A84; frame2090/2094/2098 own the object list and arena.
 * Retained READY model preparation and1B630 representation must finish first.
 * Preserve untouched packet words and native list order. output is0 when the
 * original non-player arena-pressure gate drops the object. Only ordinary
 * primitive handlers and attribute modes0/1/2/5/6/7 are implemented here;
 * normal modes3/4 and shadow handlers fail before any arena/list writes.
 * No render flags, camera, packet arena or representation are manufactured.
 * Caller freezes object/timeline and retains all actual mappings until their
 * downstream stream/packet readers have finished. No GPU operation occurs.
 */
int gool_native_render_packet_type1(gool_domain *domain,uint32_t node,uint32_t function,
    uint32_t frame,uint32_t *output);
/* Read the real type1 render packet and explicitly mapped renderer gp/scratch.
 * Packet+4 supplies the original handler ID; +14 auxiliary,+30/+34 frames and
 * +6C TGEO item0 supply actual addresses. Prior light controls and raw CFC2 H
 * come from the caller's actual GTE owner. No GP/scratch mapping is invented.
 * Caller retains these mappings and excludes writes through capture/prepare/
 * commit. Entire capture is synchronous; output/function unchanged on failure.
 */
int gool_native_render_capture(gool_domain *domain,uint32_t gp,uint32_t scratch,uint32_t packet,
    const uint32_t prior_light[5],uint32_t projection_distance,
    uint32_t *function,gool_native_render_input *output);
/* Commit optional native memory stores in original order:sourceY scratchFC,
 * fog packet20, then SH packet1C. Require packet flags/color still match the
 * captured input. Rotation/translation/light GTE results remain explicit CPU
 * outputs for the caller's backend. On domain failure earlier writes remain;
 * do not replay an unfinished render invocation as a new packet operation.
 */
int gool_native_render_commit_packet(gool_domain *domain,uint32_t scratch,uint32_t packet,
    const gool_native_render_input *captured,const gool_native_render_output *result);
/* Original3CDD4..3CE1C attribute-only command. Bits0/1 choose actual scratch
 * cache versus TGEO item2 separately for T6/T7; T5 copies resolved T6. Reads
 * occur T6 then T7, all register results publish only after successful reads.
 * attributes/attribute_bytes describe the retained native item2 span. scratch
 * is actual mapped renderer memory. Input command's high16 must be zero.
 * This operates at the live command point, before later vertex-cache writes;
 * no table fallback or reread of carried attributes after another command.
 */
int gool_native_render_attribute_command(gool_domain *domain,uint32_t scratch,uint32_t attributes,
    uint32_t attribute_bytes,uint32_t command,uint32_t output[3]);
typedef struct {uint32_t address,index,record,lookups;int textured;} gool_native_render_uv;
/* Original3CCDC..3CD34. uv_base is the actual saved S1; animation_base is
 * saved S0, never a viewer's adjusted zero/one-based index. Clock is actual
 * scratch1C. Each indirect record chooses another record using low11 after
 * wrapping ADDU; shift5/count7 masks match original instructions exactly.
 * Bit31 has no separate mip-policy branch in this lookup. Bound input spans
 * are the real retained item3/item4 spans. A repeated index at fixed clock is
 * an infinite native loop: reject with INVALID rather than hanging ARM11.
 * Output address is the native record END (primitives read-12/-8/-4), with
 * untextured address==uv_base. A nonzero index maps to item3 index-1.
 * No animation timer, mip distance or UV is guessed.
 */
int gool_native_render_uv_command(gool_domain *domain,uint32_t uv_base,uint32_t uv_bytes,
    uint32_t animation_base,uint32_t animation_bytes,uint32_t command,uint32_t clock,
    gool_native_render_uv *output);
#endif
