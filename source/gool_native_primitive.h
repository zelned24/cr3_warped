#ifndef GOOL_NATIVE_PRIMITIVE_H
#define GOOL_NATIVE_PRIMITIVE_H
#include "gool_domain.h"
enum {GOOL_NATIVE_PRIMITIVE_INVALID=-171};
typedef struct {uint32_t bytes,packet_words;} gool_native_primitive_size;
/* Original40AB8/3EA9C/3DAA0 bucket calculation:unsigned depth MAC>>17,
 * wrapping subtraction from actual F0 depth base, branch before shift2,
 * signed compare of wrapped shift against1FFC. Preserve native overflow
 * behavior; actual table-span validation belongs to link below. */
int gool_native_primitive_bucket(uint32_t table_base,uint32_t depth_base,uint32_t depth_mac,uint32_t *bucket);
/* Original40B10..40B94 RGB primitive bodies. colors are the actual final
 * RGB FIFO words, sxy actual packed SXY0..2. uv_end is native record END;
 * zero selects untextured. No DPCS, texture-page fixup, vertex projection,
 * clipping or ordering policy is invented. Textured native page bits are
 * preserved. Require the complete original primitive allocation within the
 * caller's retained mutable arena. Payload writes follow original order.
 * Does not link a partial body. Output size unchanged on failure, earlier
 * domain writes remain:stop the invocation rather than blindly replaying.
 */
int gool_native_primitive_rgb(gool_domain *domain,uint32_t primitive,uint32_t arena_end,
    const uint32_t sxy[3],const uint32_t colors[3],uint32_t uv_end,gool_native_primitive_size *size);
/* Original3FBAC..3FBEC mode1 body:mask RGB0 CODE, force opcode36,
 * preserve UV/page words and store colors before UV reads and coordinates.
 * Caller owns scratch124..12C fading and all-zero early return. */
int gool_native_primitive_rgb1(gool_domain *domain,uint32_t primitive,uint32_t arena_end,
    const uint32_t sxy[3],const uint32_t colors[3],uint32_t uv_end,gool_native_primitive_size *size);
/* Original3FC18 constant mode5 primitive. */
int gool_native_primitive_constant(gool_domain *domain,uint32_t primitive,uint32_t arena_end,
    const uint32_t sxy[3],gool_native_primitive_size *size);
/* Original40B98..40BB8:read bucket+20, OR packet length, write primitive tag,
 * publish primitive low24 to bucket+20, advance actual arena cursor. bucket is
 * the exact caller's selected V0, not an inferred OTZ bucket. table bounds
 * cover the actual retained ordering span. PS1 low24 tags stay data, never
 * host pointers. Completion output is published only after both native writes.
 * Partial failure retains earlier writes; caller must not link twice.
 */
int gool_native_primitive_link(gool_domain *domain,uint32_t primitive,uint32_t arena_end,
    uint32_t bucket,uint32_t table_begin,uint32_t table_end,gool_native_primitive_size size,uint32_t *next);
/* Original3EAF4..3EBD4 mode2 body AND link. Textured path forces page blend
 * bits to01 and opcode36. Untextured path emits opcode32 plus the original
 * E1000A20/zero state packet, linking state->triangle->old bucket with two
 * native tag lengths. Both paths reserve40 bytes. Writes preserve original
 * order; next remains unchanged on partial failure. No PS1 blend emulation
 * or PICA state is inferred from the generated command words. */
int gool_native_primitive_rgb2(gool_domain *domain,uint32_t primitive,uint32_t arena_end,
    uint32_t bucket,uint32_t table_begin,uint32_t table_end,const uint32_t sxy[3],
    const uint32_t colors[3],uint32_t uv_end,uint32_t *next);
typedef struct {gool_native_primitive_size size;int skipped;} gool_native_normal_primitive;
/* Original3DC08..3DC54 /3EA34..3EA90 normal-mode texture/output tails.
 * rgb is the already-computed native normal/DPCS/highlight result. backface
 * is the actual sign of NCLIP XOR packet flags XOR(command<<2), even for
 * two-sided commands. Coordinates are stored before mode4's early-return test.
 * Preserve mode-specific opcode/page OR rules and skipped allocation behavior.
 * uv_end is the exact native selected pointer, including zero-selector base;
 * this path reads-12/-8/-4 even when it equals saved S1. Caller supplies the
 * actual retained accessible span; no untextured replacement is invented.
 * Caller applies the native backface bucket bias (-8 mode3,-40 mode4) then
 * links only an unskipped result. Partial failure must not be replayed.
 */
int gool_native_primitive_normal(gool_domain *domain,uint32_t primitive,uint32_t arena_end,
    uint32_t mode,uint32_t command,int backface,uint32_t uv_end,const uint32_t sxy[3],
    uint32_t rgb,gool_native_normal_primitive *output);
/* Original3CB74..3CC64 mode7 nonzero-coefficient body:subtract blend setup,
 * monochrome triangle from wrapping(1000-coefficient)>>5, then original RGB
 * triangle with native state restoration/page forcing. One14/15-word packet,
 *60/64 bytes. colors are actual DPCT FIFO output; coefficient is raw low16.
 * Zero-coefficient mode7 uses the ordinary RGB body instead. */
int gool_native_primitive_rgb7(gool_domain *domain,uint32_t primitive,uint32_t arena_end,
    const uint32_t sxy[3],const uint32_t colors[3],uint32_t uv_end,uint16_t coefficient,
    gool_native_primitive_size *output);
#endif
